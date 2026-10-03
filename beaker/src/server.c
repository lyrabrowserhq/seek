#include "../beaker.h"
#include "beaker_globals.h"
#include "security_headers.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#define MAX_PENDING_CONNECTIONS 128
#define BEAKER_PORT_FALLBACK_MAX 64

#ifndef BEAKER_WORKER_STACK_BYTES
#define BEAKER_WORKER_STACK_BYTES (4U * 1024U * 1024U)
#endif

static volatile sig_atomic_t g_shutdown_requested = 0;

static bool validate_request_headers(const char *request_buffer);

static void signal_handler(int sig) {
  (void)sig;
  g_shutdown_requested = 1;
}

typedef struct {
  _Atomic(size_t) sequence;
  int socket;
} WorkSlot;

typedef struct {
  _Atomic(size_t) head;
  _Atomic(size_t) tail;
  _Atomic(int) shutdown;
  WorkSlot slots[MAX_PENDING_CONNECTIONS];
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} WorkQueue;

static WorkQueue g_work_queue;

static void work_queue_init(WorkQueue *queue) {
  atomic_store(&queue->head, 0);
  atomic_store(&queue->tail, 0);
  atomic_store(&queue->shutdown, 0);
  for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
    atomic_store(&queue->slots[i].sequence, (size_t)i);
  }
  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->cond, NULL);
}

static void work_queue_destroy(WorkQueue *queue) {
  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->cond);
}

static int work_queue_push(WorkQueue *queue, int client_socket) {
  size_t tail = atomic_load(&queue->tail);

  for (;;) {
    WorkSlot *slot = &queue->slots[tail % MAX_PENDING_CONNECTIONS];
    size_t seq = atomic_load(&slot->sequence);
    intptr_t diff = (intptr_t)seq - (intptr_t)tail;

    if (diff == 0) {
      if (atomic_compare_exchange_weak(&queue->tail, &tail, tail + 1)) {
        slot->socket = client_socket;
        atomic_store(&slot->sequence, tail + 1);
        pthread_cond_signal(&queue->cond);
        return 0;
      }
    } else if (diff < 0) {
      return -1;
    } else {
      tail = atomic_load(&queue->tail);
    }
  }
}

static int work_queue_pop(WorkQueue *queue) {
  size_t head = atomic_load(&queue->head);

  for (;;) {
    if (atomic_load(&queue->shutdown))
      return -1;

    WorkSlot *slot = &queue->slots[head % MAX_PENDING_CONNECTIONS];
    size_t seq = atomic_load(&slot->sequence);
    intptr_t diff = (intptr_t)seq - (intptr_t)(head + 1);

    if (diff == 0) {
      if (atomic_compare_exchange_weak(&queue->head, &head, head + 1)) {
        int fd = slot->socket;
        atomic_store(&slot->sequence, head + MAX_PENDING_CONNECTIONS);
        return fd;
      }
    } else if (diff < 0) {
      pthread_mutex_lock(&queue->mutex);
      if (!atomic_load(&queue->shutdown) && atomic_load(&queue->head) == head) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
      }
      pthread_mutex_unlock(&queue->mutex);
      head = atomic_load(&queue->head);
    } else {
      head = atomic_load(&queue->head);
    }
  }
}

static int get_optimal_thread_count(void) {
  long cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1)
    cores = 1;
  return (int)(cores * 2);
}

void handle_client_connection(int new_socket);

static void *worker_thread(void *arg) {
  (void)arg;
  while (1) {
    int client_socket = work_queue_pop(&g_work_queue);
    if (client_socket < 0) {
      break;
    }
    handle_client_connection(client_socket);
  }
  return NULL;
}

static int initialize_server_socket(const char *ip, int port,
                                    int *server_fd_out,
                                    struct sockaddr_in *address_out) {
  memset(address_out, 0, sizeof(*address_out));

  if ((*server_fd_out = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    beaker_log_errno_format(
        "ERROR", "initialize_server_socket: Failed to create socket.\n");
    return -1;
  }

  int opt = 1;

  if (setsockopt(*server_fd_out, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    beaker_log_errno("setsockopt SO_REUSEADDR failed");
  }

// Needed for FreeBSD support. On macOS this allows multiple processes to
// bind the same TCP port, which is surprising for a single-instance server.
#if defined(__FreeBSD__) && defined(SO_REUSEPORT)
  if (setsockopt(*server_fd_out, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    beaker_log_errno_format(
        "ERROR", "initialize_server_socket: Failed to set SO_REUSEPORT.\n");
    close(*server_fd_out);
    return -1;
  }
#endif

  address_out->sin_family = AF_INET;
  address_out->sin_addr.s_addr = inet_addr(ip);
  address_out->sin_port = htons(port);

  if (bind(*server_fd_out, (struct sockaddr *)address_out,
           sizeof(*address_out)) < 0) {
    int bind_err = errno;
    close(*server_fd_out);
    if (bind_err == EADDRINUSE)
      return -2;
    errno = bind_err;
    beaker_log_errno_format(
        "ERROR", "initialize_server_socket: Failed to bind socket to %s:%d.\n",
        ip, port);
    return -1;
  }

  if (listen(*server_fd_out, 10) < 0) {
    beaker_log_errno_format(
        "ERROR", "initialize_server_socket: Failed to listen on socket.\n");
    close(*server_fd_out);
    return -1;
  }

  int flags = fcntl(*server_fd_out, F_GETFL, 0);
  if (flags < 0 || fcntl(*server_fd_out, F_SETFL, flags | O_NONBLOCK) < 0) {
    beaker_log_errno("fcntl O_NONBLOCK failed");
    close(*server_fd_out);
    return -1;
  }

  beaker_log("INFO", "listening on %s:%d", ip, port);
  return 0;
}

static void log_request_end(const char *method, const char *path,
                            const struct timespec *started_at) {
  struct timespec finished_at;
  clock_gettime(CLOCK_MONOTONIC, &finished_at);
  double duration_ms = (finished_at.tv_sec - started_at->tv_sec) * 1000.0 +
                       (finished_at.tv_nsec - started_at->tv_nsec) / 1000000.0;
  beaker_log_request(current_request_info.remote_addr, method, path,
                     current_response_status, current_response_size,
                     duration_ms);
  beaker_clear_response_cookies();
}

static const char *find_header_end(const char *buf, size_t len) {
  if (len < 4)
    return NULL;
  for (size_t i = 0; i + 4 <= len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
        buf[i + 3] == '\n') {
      return buf + i;
    }
  }
  return NULL;
}

static int request_allows_keep_alive(const char *request_line) {
  const char *conn = beaker_get_header("Connection");
  int wants_close = 0;
  int wants_alive = 0;
  if (conn) {
    char lower[256];
    int i = 0;
    for (; conn[i] && i < (int)sizeof(lower) - 1; i++) {
      char c = conn[i];
      lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    lower[i] = '\0';
    wants_close = strstr(lower, "close") != NULL;
    wants_alive = strstr(lower, "keep-alive") != NULL;
  }

  const char *clen = beaker_get_header("Content-Length");
  if (clen && clen[0] != '\0')
    return 0;

  if (strstr(request_line, "HTTP/1.0") != NULL)
    return wants_alive && !wants_close;
  return !wants_close;
}

void handle_client_connection(int new_socket) {
  current_client_socket = new_socket;
  char buffer[BUFFER_SIZE] = {0};
  char method[16] = "-";
  char log_path[MAX_PATH_LEN] = "-";
  size_t pending = 0;
  int first_request = 1;

  memset(&current_request_info, 0, sizeof(RequestInfo));
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  if (getpeername(new_socket, (struct sockaddr *)&client_addr, &client_len) !=
          0 ||
      inet_ntop(AF_INET, &client_addr.sin_addr,
                current_request_info.remote_addr,
                sizeof(current_request_info.remote_addr)) == NULL) {
    strcpy(current_request_info.remote_addr, "-");
  }

  for (;;) {
    struct timespec started_at;
    clock_gettime(CLOCK_MONOTONIC, &started_at);
    beaker_clear_response_cookies();
    current_response_status = 0;
    current_response_size = 0;
    beaker_reset_write_deadline();
    current_keep_alive = 0;

    size_t got = 0;
    BeakerRequestReadResult read_result;
    /* A pipelined request may already be fully buffered. */
    if (find_header_end(buffer, pending) != NULL) {
      read_result = BEAKER_REQUEST_READ_OK;
    } else {
      read_result = beaker_read_request_headers_ex(
          new_socket, buffer + pending, sizeof(buffer) - pending, &got,
          first_request ? BEAKER_REQUEST_HEADER_TIMEOUT_MS
                        : BEAKER_KEEPALIVE_IDLE_TIMEOUT_MS);
    }
    size_t bytes_read = pending + got;

    if (read_result == BEAKER_REQUEST_READ_TIMEOUT) {
      if (first_request) {
        beaker_log("WARN",
                   "handle_client_connection: Request header timed out.\n");
        send_status("408 Request Timeout");
      }
      break;
    }
    if (read_result == BEAKER_REQUEST_READ_TOO_LARGE) {
      beaker_log("WARN",
                 "handle_client_connection: Request headers too large.\n");
      send_status("431 Request Header Fields Too Large");
      break;
    }
    if (read_result == BEAKER_REQUEST_READ_CLOSED) {
      if (bytes_read > 0) {
        send_status("400 Bad Request");
      }
      break;
    }
    if (read_result == BEAKER_REQUEST_READ_ERROR) {
      beaker_log_errno_format(
          "ERROR",
          "handle_client_connection: Failed to read from client socket.\n");
      break;
    }

    char *header_end = (char *)find_header_end(buffer, bytes_read);
    if (!header_end) {
      send_status("400 Bad Request");
      break;
    }
    size_t request_len = (size_t)(header_end - buffer) + 4;
    size_t carry = bytes_read > request_len ? bytes_read - request_len : 0;
    if (request_len >= sizeof(buffer)) {
      send_status("431 Request Header Fields Too Large");
      break;
    }

    if (request_len >= sizeof(current_request_buffer)) {
      send_status("431 Request Header Fields Too Large");
      break;
    }
    memcpy(current_request_buffer, buffer, request_len);
    current_request_buffer[request_len] = '\0';

    /* Preserve any pipelined bytes before NUL-terminating buffer. */
    if (carry) {
      memmove(buffer, header_end + 4, carry);
    }
    pending = carry;

    char request_line[MAX_PATH_LEN + 64];
    char *first_line_end = strstr(current_request_buffer, "\r\n");

    if (first_line_end == NULL ||
        !validate_request_headers(current_request_buffer)) {
      beaker_log(
          "ERROR",
          "handle_client_connection: Invalid HTTP request headers.\n");
      send_status("400 Bad Request");
      break;
    }
    size_t request_line_len = first_line_end - current_request_buffer;
    if (request_line_len >= sizeof(request_line)) {
      beaker_log("ERROR",
                 "handle_client_connection: Request line too long.\n");
      send_status("400 Bad Request");
      break;
    }
    strncpy(request_line, current_request_buffer, request_line_len);
    request_line[request_line_len] = '\0';
    sscanf(request_line, "%15s %255s", method, log_path);

    UrlParams request_params;
    char *requested_path = parse_request_url(request_line, &request_params);

    if (requested_path == NULL) {
      beaker_log("ERROR", "handle_client_connection: Could not parse request "
                          "path. Sending 400 Bad Request.\n");
      send_status("400 Bad Request");
      break;
    }
    strncpy(log_path, requested_path, sizeof(log_path) - 1);
    log_path[sizeof(log_path) - 1] = '\0';

    current_keep_alive = request_allows_keep_alive(request_line);

    bool handled = false;

    if (strncmp(requested_path, "/static/", strlen("/static/")) == 0) {
      if (serve_static_file(requested_path + strlen("/static/"))) {
        handled = true;
      }
    }

    if (!handled) {
      int best_match_handler_index = -1;
      size_t best_match_len = 0;

      for (int i = 0; i < handler_count; i++) {
        size_t handler_path_len = strlen(handlers[i].path);

        if (strncmp(requested_path, handlers[i].path, handler_path_len) == 0) {

          if (handler_path_len == strlen(requested_path) ||
              requested_path[handler_path_len] == '/') {

            if (handler_path_len > best_match_len) {
              best_match_len = handler_path_len;
              best_match_handler_index = i;
            }
          }
        }
      }

      if (best_match_handler_index != -1) {
        handlers[best_match_handler_index].handler(&request_params);
        handled = true;
      }
    }

    if (!handled) {
      const char *not_found_html =
          "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
          "<title>Seek</title>"
          "<link rel=\"stylesheet\" href=\"/static/main.css?v=8\">"
          "<link rel=\"icon\" href=\"/static/favicon.ico\">"
          "<script src=\"/static/theme.js?v=2\"></script></head><body>"
          "<main class=\"error-page\"><div class=\"error-card\">"
          "<a class=\"error-logo\" href=\"/\">Seek</a>"
          "<h1 class=\"error-heading\">Page not found</h1>"
          "<p class=\"error-detail\">That address is not a Seek page.</p>"
          "<a class=\"error-back\" href=\"/\">Back to Seek</a>"
          "</div></main></body></html>";
      char not_found_response[8192];
      int nf = beaker_snprint_http_html_response(
          not_found_response, sizeof(not_found_response),
          "HTTP/1.1 404 Not Found", not_found_html);
      if (nf > 0 && nf < (int)sizeof(not_found_response) &&
          beaker_send_all(new_socket, not_found_response, (size_t)nf) < 0) {
        beaker_log_errno("Failed to send 404 response");
      }
      current_response_status = 404;
      current_response_size = strlen(not_found_html);
      current_keep_alive = 0;
    }

    log_request_end(method, log_path, &started_at);
    free(requested_path);
    requested_path = NULL;

    first_request = 0;

    if (!current_keep_alive)
      break;
  }

  beaker_clear_response_cookies();
  close(new_socket);
  current_client_socket = -1;
#ifdef __GLIBC__
  malloc_trim(0);
#endif
}

int beaker_run(const char *ip, int *port) {
  return beaker_run_with_threads(ip, port, 0);
}

int beaker_run_with_threads(const char *ip, int *port, int num_workers) {
  int server_fd;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  if (port == NULL) {
    beaker_log("ERROR", "beaker_run_with_threads: port argument is NULL.");
    return -1;
  }
  if (*port < 1 || *port > 65535) {
    beaker_log("ERROR", "beaker_run_with_threads: invalid port %d.", *port);
    return -1;
  }

  g_shutdown_requested = 0;

  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  if (num_workers <= 0) {
    num_workers = get_optimal_thread_count();
  }

  int preferred = *port;
  int bound_port = preferred;
  int bound = 0;
  for (int attempt = 0; attempt < BEAKER_PORT_FALLBACK_MAX; attempt++) {
    int r = initialize_server_socket(ip, bound_port, &server_fd, &address);
    if (r == 0) {
      bound = 1;
      struct sockaddr_in name;
      socklen_t namelen = sizeof(name);
      if (getsockname(server_fd, (struct sockaddr *)&name, &namelen) == 0)
        *port = (int)ntohs(name.sin_port);
      else
        *port = bound_port;
      if (*port != preferred) {
        beaker_log("INFO", "port %d in use; listening on %d instead", preferred,
                   *port);
      }
      break;
    }
    if (r == -2) {
      bound_port++;
      continue;
    }
    return -1;
  }
  if (!bound) {
    beaker_log("ERROR",
               "could not bind: no free port in range %d-%d (each address in "
               "use)",
               preferred, preferred + BEAKER_PORT_FALLBACK_MAX - 1);
    return -1;
  }

  work_queue_init(&g_work_queue);

  pthread_t threads[num_workers];
  pthread_attr_t worker_attr;
  int worker_attr_ready = 0;
  if (pthread_attr_init(&worker_attr) == 0) {
    if (pthread_attr_setstacksize(&worker_attr, BEAKER_WORKER_STACK_BYTES) == 0)
      worker_attr_ready = 1;
    else
      pthread_attr_destroy(&worker_attr);
  }

  for (int i = 0; i < num_workers; i++) {
    pthread_create(&threads[i], worker_attr_ready ? &worker_attr : NULL,
                   worker_thread, NULL);
  }

  if (worker_attr_ready)
    pthread_attr_destroy(&worker_attr);

  beaker_log("DEBUG", "started %d worker threads", num_workers);

  struct pollfd pfd = {.fd = server_fd, .events = POLLIN};

  while (!g_shutdown_requested) {
    int ret = poll(&pfd, 1, 1000);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      beaker_log_errno("poll failed");
      break;
    }
    if (ret == 0)
      continue;

    int new_socket;
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                (socklen_t *)&addrlen)) >= 0) {
      if (beaker_configure_client_socket(new_socket) < 0) {
        beaker_log_errno("failed to configure client socket");
        close(new_socket);
        continue;
      }
      if (work_queue_push(&g_work_queue, new_socket) < 0) {
        beaker_log("WARN", "work queue full; rejecting connection");
        char busy_buf[8192];
        int nb = beaker_snprint_http_empty_response(
            busy_buf, sizeof(busy_buf), "HTTP/1.1 503 Service Unavailable");
        beaker_reset_write_deadline();
        if (nb > 0 && nb < (int)sizeof(busy_buf) &&
            beaker_send_all(new_socket, busy_buf, (size_t)nb) < 0) {
          beaker_log_errno("failed to send busy response");
        }
        close(new_socket);
      }
    }
  }

  beaker_log("INFO", "shutting down");

  atomic_store(&g_work_queue.shutdown, 1);
  pthread_cond_broadcast(&g_work_queue.cond);

  for (int i = 0; i < num_workers; i++) {
    pthread_join(threads[i], NULL);
  }

  work_queue_destroy(&g_work_queue);
  close(server_fd);
  return 0;
}

const char *beaker_get_remote_addr(void) {
  return current_request_info.remote_addr;
}

static __thread char g_header_value[MAX_VALUE_LEN];

static bool is_valid_request_header_value(const char *start, const char *end) {
  for (const char *cursor = start; cursor < end; cursor++) {
    unsigned char c = (unsigned char)*cursor;
    if ((c < 0x20 && c != '\t') || c == 0x7f)
      return false;
  }
  return true;
}

static bool validate_request_headers(const char *request_buffer) {
  if (request_buffer == NULL)
    return false;

  const char *request_line_end = strstr(request_buffer, "\r\n");
  if (request_line_end == NULL)
    return false;

  const char *cursor = request_line_end + 2;
  int host_count = 0;
  while (true) {
    const char *line_end = strstr(cursor, "\r\n");
    if (line_end == NULL)
      return false;
    if (line_end == cursor)
      return host_count <= 1;
    if (*cursor == ' ' || *cursor == '\t')
      return false;

    const char *colon = memchr(cursor, ':', (size_t)(line_end - cursor));
    if (colon == NULL || colon == cursor)
      return false;

    size_t name_len = (size_t)(colon - cursor);
    if (!beaker_is_valid_http_token_span(cursor, name_len) ||
        !is_valid_request_header_value(colon + 1, line_end)) {
      return false;
    }

    if (name_len == strlen("Host") &&
        strncasecmp(cursor, "Host", name_len) == 0 && ++host_count > 1) {
      return false;
    }
    cursor = line_end + 2;
  }
}

const char *beaker_get_header(const char *name) {
  g_header_value[0] = '\0';
  if (!beaker_is_valid_http_token(name))
    return g_header_value;

  size_t requested_name_len = strlen(name);
  const char *request_line_end = strstr(current_request_buffer, "\r\n");
  if (request_line_end == NULL)
    return g_header_value;

  const char *cursor = request_line_end + 2;
  bool found = false;
  while (true) {
    const char *line_end = strstr(cursor, "\r\n");
    if (line_end == NULL || line_end == cursor)
      break;
    if (*cursor == ' ' || *cursor == '\t')
      return g_header_value;

    const char *colon = memchr(cursor, ':', (size_t)(line_end - cursor));
    if (colon == NULL || colon == cursor)
      return g_header_value;

    size_t header_name_len = (size_t)(colon - cursor);
    if (header_name_len == requested_name_len &&
        strncasecmp(cursor, name, requested_name_len) == 0) {
      if (found) {
        beaker_log("SECURITY",
                   "beaker_get_header: Rejected duplicate header value.");
        g_header_value[0] = '\0';
        return g_header_value;
      }

      const char *value_start = colon + 1;
      while (value_start < line_end &&
             (*value_start == ' ' || *value_start == '\t')) {
        value_start++;
      }
      const char *value_end = line_end;
      while (value_end > value_start &&
             (value_end[-1] == ' ' || value_end[-1] == '\t')) {
        value_end--;
      }

      size_t value_len = (size_t)(value_end - value_start);
      if (value_len >= sizeof(g_header_value) ||
          !is_valid_request_header_value(value_start, value_end)) {
        beaker_log("SECURITY",
                   "beaker_get_header: Rejected invalid header value.");
        g_header_value[0] = '\0';
        return g_header_value;
      }
      memcpy(g_header_value, value_start, value_len);
      g_header_value[value_len] = '\0';
      found = true;
    }
    cursor = line_end + 2;
  }

  return g_header_value;
}

void beaker_set_request_buffer(const char *buffer) {
  if (buffer == NULL)
    return;
  strncpy(current_request_buffer, buffer, BUFFER_SIZE - 1);
  current_request_buffer[BUFFER_SIZE - 1] = '\0';
}
