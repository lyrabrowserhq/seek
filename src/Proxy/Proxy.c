#include "Proxy.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

Proxy *proxy_list = NULL;
int proxy_count = 0;
int max_proxy_retries = 3;
int randomize_username = 0;
int randomize_password = 0;
char proxy_url[512] = {0};
char proxy_noproxy[256] = "localhost,127.0.0.1,::1,lyra-index,lyra-seek";
static pthread_mutex_t proxy_mutex = PTHREAD_MUTEX_INITIALIZER;
static int proxy_fail_streak = 0;

static const char RAND_CHARS[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static void generate_random_string(char *buf, size_t len) {
  for (size_t i = 0; i < len - 1; i++) {
    buf[i] = RAND_CHARS[rand() % (sizeof(RAND_CHARS) - 1)];
  }
  buf[len - 1] = '\0';
}

void set_proxy_config(const char *proxy_str, int rand_user, int rand_pass) {
  if (proxy_str && proxy_str[0]) {
    strncpy(proxy_url, proxy_str, sizeof(proxy_url) - 1);
    proxy_url[sizeof(proxy_url) - 1] = '\0';
  }
  randomize_username = rand_user;
  randomize_password = rand_pass;
}

void proxy_apply_env(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = '\0';
  const char *proxy = getenv("SEEK_PROXY");
  const char *mullvad = getenv("SEEK_MULLVAD");
  const char *noproxy = getenv("SEEK_NOPROXY");
  if (noproxy && noproxy[0]) {
    snprintf(proxy_noproxy, sizeof(proxy_noproxy), "%s", noproxy);
  }
  if (proxy && proxy[0]) {
    snprintf(out, out_sz, "%s", proxy);
    return;
  }
  if (mullvad && (mullvad[0] == '1' || !strcasecmp(mullvad, "true") ||
                  !strcasecmp(mullvad, "yes"))) {
    snprintf(out, out_sz, "socks5h://10.64.0.1:1080");
  }
}

static Proxy parse_proxy_line(const char *line) {
  Proxy proxy = {.type = PROXY_SOCKS5,
                 .port = 0,
                 .username[0] = '\0',
                 .password[0] = '\0',
                 .failures = 0};
  const char *host_start = NULL;
  const char *port_start = NULL;

  size_t len = strlen(line);
  if (len == 0)
    return proxy;

  if (strncmp(line, "http://", 7) == 0) {
    proxy.type = PROXY_HTTP;
    host_start = line + 7;
  } else if (strncmp(line, "socks5h://", 10) == 0) {
    proxy.type = PROXY_SOCKS5H;
    host_start = line + 10;
  } else if (strncmp(line, "socks5://", 9) == 0) {
    proxy.type = PROXY_SOCKS5;
    host_start = line + 9;
  } else if (strncmp(line, "socks4://", 9) == 0) {
    proxy.type = PROXY_SOCKS4;
    host_start = line + 9;
  } else {
    host_start = line;
  }

  const char *at = strchr(host_start, '@');
  if (at) {
    char cred_buf[128];
    size_t cred_len = at - host_start;
    if (cred_len >= sizeof(cred_buf))
      cred_len = sizeof(cred_buf) - 1;
    strncpy(cred_buf, host_start, cred_len);
    cred_buf[cred_len] = '\0';

    char *colon = strchr(cred_buf, ':');
    if (colon) {
      size_t user_len = colon - cred_buf;
      if (user_len >= sizeof(proxy.username))
        user_len = sizeof(proxy.username) - 1;
      strncpy(proxy.username, cred_buf, user_len);
      proxy.username[user_len] = '\0';
      strncpy(proxy.password, colon + 1, sizeof(proxy.password) - 1);
      proxy.password[sizeof(proxy.password) - 1] = '\0';
    }
    host_start = at + 1;
  }

  port_start = strchr(host_start, ':');
  if (port_start) {
    char host_buf[256];
    size_t host_len = port_start - host_start;
    if (host_len >= sizeof(host_buf))
      host_len = sizeof(host_buf) - 1;
    strncpy(host_buf, host_start, host_len);
    host_buf[host_len] = '\0';
    snprintf(proxy.host, sizeof(proxy.host), "%.*s", (int)host_len, host_buf);
    proxy.port = atoi(port_start + 1);
  } else {
    snprintf(proxy.host, sizeof(proxy.host), "%.*s",
             (int)sizeof(proxy.host) - 1, host_start);
  }

  return proxy;
}

int load_proxy_list(const char *filename) {
  if (!filename || filename[0] == '\0') {
    return 0;
  }

  FILE *file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "[WARN] Could not open proxy list file: %s\n", filename);
    return -1;
  }

  int temp_capacity = 16;
  Proxy *temp_list = (Proxy *)malloc(temp_capacity * sizeof(Proxy));
  if (!temp_list) {
    fclose(file);
    return -1;
  }
  int temp_count = 0;

  char line[512];
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = 0;

    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;

    char *end = p + strlen(p) - 1;
    while (end > p && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }

    if (p[0] == '\0')
      continue;

    Proxy proxy = parse_proxy_line(p);
    if (proxy.port == 0) {
      continue;
    }

    if (temp_count >= temp_capacity) {
      temp_capacity *= 2;
      Proxy *new_list = (Proxy *)realloc(temp_list, temp_capacity * sizeof(Proxy));
      if (!new_list) {
        free(temp_list);
        fclose(file);
        return -1;
      }
      temp_list = new_list;
    }

    temp_list[temp_count++] = proxy;
  }

  fclose(file);

  pthread_mutex_lock(&proxy_mutex);

  if (proxy_list) {
    free(proxy_list);
  }
  proxy_list = temp_list;
  proxy_count = temp_count;

  fprintf(stderr, "[INFO] Loaded %d proxies from %s\n", proxy_count, filename);
  pthread_mutex_unlock(&proxy_mutex);
  return proxy_count;
}

void free_proxy_list(void) {
  pthread_mutex_lock(&proxy_mutex);
  if (proxy_list) {
    free(proxy_list);
    proxy_list = NULL;
  }
  proxy_count = 0;
  pthread_mutex_unlock(&proxy_mutex);
}

Proxy *get_random_proxy(void) {
  pthread_mutex_lock(&proxy_mutex);
  if (proxy_count == 0) {
    pthread_mutex_unlock(&proxy_mutex);
    return NULL;
  }

  int start = rand() % proxy_count;
  int checked = 0;
  Proxy *selected = NULL;

  while (checked < proxy_count) {
    int idx = (start + checked) % proxy_count;
    if (proxy_list[idx].failures < max_proxy_retries) {
      selected = &proxy_list[idx];
      break;
    }
    checked++;
  }

  if (!selected) {
    for (int i = 0; i < proxy_count; i++) {
      proxy_list[i].failures = 0;
    }
    selected = &proxy_list[rand() % proxy_count];
  }

  pthread_mutex_unlock(&proxy_mutex);
  return selected;
}

void record_proxy_failure(Proxy *proxy) {
  if (!proxy)
    return;
  pthread_mutex_lock(&proxy_mutex);
  proxy->failures++;
  proxy_fail_streak++;
  pthread_mutex_unlock(&proxy_mutex);
}

void record_proxy_success(Proxy *proxy) {
  pthread_mutex_lock(&proxy_mutex);
  if (proxy)
    proxy->failures = 0;
  proxy_fail_streak = 0;
  pthread_mutex_unlock(&proxy_mutex);
}

void proxy_heal(void) {
  pthread_mutex_lock(&proxy_mutex);
  if (proxy_count > 0) {
    int all_dead = 1;
    for (int i = 0; i < proxy_count; i++) {
      if (proxy_list[i].failures < max_proxy_retries) {
        all_dead = 0;
        break;
      }
    }
    if (all_dead || proxy_fail_streak >= max_proxy_retries * 2) {
      for (int i = 0; i < proxy_count; i++)
        proxy_list[i].failures = 0;
      proxy_fail_streak = 0;
    }
  }
  pthread_mutex_unlock(&proxy_mutex);
}

static long proxy_curl_type(const char *url) {
  if (strncmp(url, "socks5h://", 10) == 0)
    return CURLPROXY_SOCKS5_HOSTNAME;
  if (strncmp(url, "socks5://", 9) == 0)
    return CURLPROXY_SOCKS5_HOSTNAME;
  if (strncmp(url, "socks4://", 9) == 0)
    return CURLPROXY_SOCKS4A;
  return CURLPROXY_HTTP;
}

void apply_proxy_settings(CURL *curl) {
  curl_easy_setopt(curl, CURLOPT_NOPROXY, proxy_noproxy);
  if (proxy_url[0] != '\0') {
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy_url);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, proxy_curl_type(proxy_url));

    if (randomize_username || randomize_password) {
      char userpwd[256];
      char username[32] = {0};
      char password[32] = {0};

      if (randomize_username)
        generate_random_string(username, sizeof(username));
      if (randomize_password)
        generate_random_string(password, sizeof(password));

      snprintf(userpwd, sizeof(userpwd), "%s:%s", username, password);
      curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, userpwd);
    }
  } else if (proxy_count > 0) {
    Proxy *proxy = get_random_proxy();
    if (proxy) {
      char proxy_url_buf[512];
      const char *scheme = "socks5h";
      if (proxy->type == PROXY_HTTP)
        scheme = "http";
      else if (proxy->type == PROXY_SOCKS4)
        scheme = "socks4";
      else if (proxy->type == PROXY_SOCKS5)
        scheme = "socks5h";
      snprintf(proxy_url_buf, sizeof(proxy_url_buf), "%s://%s:%d", scheme,
               proxy->host, proxy->port);
      curl_easy_setopt(curl, CURLOPT_PROXY, proxy_url_buf);
      if (proxy->type == PROXY_HTTP) {
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
      } else if (proxy->type == PROXY_SOCKS4) {
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4A);
      } else {
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
      }

      if (proxy->username[0] != '\0' || proxy->password[0] != '\0') {
        char userpwd[128];
        snprintf(userpwd, sizeof(userpwd), "%s:%s", proxy->username,
                 proxy->password);
        curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, userpwd);
      }
    }
  }
}
