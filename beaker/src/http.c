/*
 * Modified by the Lyra project on 2026-10-03: redirect responses now send Content-Length: 0 and Cache-Control: no-store.
 * Upstream: https://git.bwaaa.monster/beaker (commit acdd9e7).
 * This file remains under the LGPL-2.1; see ../LICENSE.
 */
#include "../beaker.h"
#include "beaker_globals.h"
#include "security_headers.h"
#include "gzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool is_http_token_char(unsigned char c) {
  if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z')) {
    return true;
  }

  return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
         c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
         c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool beaker_is_valid_http_token(const char *value) {
  if (value == NULL) {
    return false;
  }

  return beaker_is_valid_http_token_span(value, strlen(value));
}

bool beaker_is_valid_http_token_span(const char *value, size_t length) {
  if (value == NULL || length == 0)
    return false;

  for (size_t i = 0; i < length; i++) {
    if (!is_http_token_char((unsigned char)value[i])) {
      return false;
    }
  }
  return true;
}

bool beaker_is_valid_header_value(const char *value) {
  if (value == NULL) {
    return false;
  }

  for (size_t i = 0; value[i] != '\0'; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c < 0x20 || c == 0x7f) {
      return false;
    }
  }
  return true;
}

static bool is_valid_cookie_value(const char *value) {
  if (value == NULL) {
    return false;
  }

  for (size_t i = 0; value[i] != '\0'; i++) {
    unsigned char c = (unsigned char)value[i];
    if (!(c == 0x21 || (c >= 0x23 && c <= 0x2b) || (c >= 0x2d && c <= 0x3a) ||
          (c >= 0x3c && c <= 0x5b) || (c >= 0x5d && c <= 0x7e))) {
      return false;
    }
  }
  return true;
}

static bool is_valid_cookie_attribute(const char *value) {
  return value == NULL ||
         (beaker_is_valid_header_value(value) && strchr(value, ';') == NULL);
}

static bool parse_status_line(const char *status_line, int *status_code) {
  if (status_line == NULL || status_code == NULL)
    return false;

  size_t status_line_len = strlen(status_line);
  if (status_line_len < 5 || status_line_len >= 128 || status_line[0] < '0' ||
      status_line[0] > '9' || status_line[1] < '0' || status_line[1] > '9' ||
      status_line[2] < '0' || status_line[2] > '9' || status_line[3] != ' ' ||
      status_line[4] == '\0' || !beaker_is_valid_header_value(status_line)) {
    return false;
  }

  int code = (status_line[0] - '0') * 100 + (status_line[1] - '0') * 10 +
             (status_line[2] - '0');
  if (code < 100 || code > 599) {
    return false;
  }

  *status_code = code;
  return true;
}

static void build_cookie_headers(char *cookie_headers_buffer,
                                 size_t buffer_size) {

  cookie_headers_buffer[0] = '\0';

  for (int i = 0; i < cookies_to_set_count; i++) {
    char single_cookie_header[MAX_KEY_LEN * 2 + MAX_VALUE_LEN * 2 + 128];

    int header_length = snprintf(
        single_cookie_header, sizeof(single_cookie_header), "Set-Cookie: %s=%s",
        cookies_to_set[i].name, cookies_to_set[i].value);
    if (header_length < 0 ||
        (size_t)header_length >= sizeof(single_cookie_header)) {
      beaker_log("ERROR", "build_cookie_headers: Cookie is too large.");
      continue;
    }

    if (strlen(cookies_to_set[i].expires) > 0) {
      strncat(single_cookie_header, "; Expires=",
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
      strncat(single_cookie_header, cookies_to_set[i].expires,
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
    }

    if (strlen(cookies_to_set[i].path) > 0) {
      strncat(single_cookie_header, "; Path=",
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
      strncat(single_cookie_header, cookies_to_set[i].path,
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
    }

    if (cookies_to_set[i].http_only) {
      strncat(single_cookie_header, "; HttpOnly",
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
    }

    if (cookies_to_set[i].secure) {
      strncat(single_cookie_header, "; Secure",
              sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);
    }

    strncat(single_cookie_header, "\r\n",
            sizeof(single_cookie_header) - strlen(single_cookie_header) - 1);

    if (strlen(cookie_headers_buffer) + strlen(single_cookie_header) <
        buffer_size) {
      strncat(cookie_headers_buffer, single_cookie_header,
              buffer_size - strlen(cookie_headers_buffer) - 1);
    } else {
      beaker_log(
          "WARN",
          "build_cookie_headers: Cookie headers buffer full, truncating\n");
      break;
    }
  }
}

void beaker_clear_response_cookies(void) {
  memset(cookies_to_set, 0, sizeof(cookies_to_set));
  cookies_to_set_count = 0;
}

void send_status(const char *status_line) {
  if (current_client_socket == -1) {
    beaker_log("ERROR",
               "send_status: No client socket set. Cannot send response.\n");
    return;
  }

  int status_code;
  if (!parse_status_line(status_line, &status_code)) {
    beaker_log("SECURITY", "send_status: Rejected invalid HTTP status line.");
    status_line = "500 Internal Server Error";
    status_code = 500;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  char http_response[BUFFER_SIZE + 6144];
  current_response_status = status_code;
  current_response_size = 0;
  int response_length = snprintf(http_response, sizeof(http_response),
                                 "HTTP/1.1 %s\r\n"
                                 "%.*s"
                                 "Content-Length: 0\r\n"
                                 "Connection: close\r\n"
                                 "\r\n",
                                 status_line, seclen, sec);
  if (response_length < 0 || (size_t)response_length >= sizeof(http_response)) {
    beaker_log("ERROR", "send_status: Failed to construct HTTP status.");
    return;
  }

  if (beaker_send_all(current_client_socket, http_response,
                      strlen(http_response)) < 0) {
    beaker_log_errno_format("ERROR",
                            "send_status: Failed to send HTTP status.\n");
  }
}

void send_response(const char *html) {

  if (current_client_socket == -1) {
    beaker_log("ERROR",
               "send_response: No client socket set. Cannot send response.\n");
    return;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  char http_response_header[BUFFER_SIZE * 2 + 6144];
  size_t content_length = strlen(html);

  unsigned char *gz = NULL;
  const unsigned char *body = (const unsigned char *)html;
  size_t body_size = content_length;
  if (content_length > GZIP_MIN_SIZE && beaker_client_accepts_gzip()) {
    gz = beaker_gzip_compress((const unsigned char *)html, content_length, &body_size);
    if (gz)
      body = gz;
    else
      body_size = content_length;
  }

  current_response_status = 200;
  current_response_size = body_size;
  char cookie_headers[BUFFER_SIZE];

  build_cookie_headers(cookie_headers, sizeof(cookie_headers));
  beaker_clear_response_cookies();

  snprintf(http_response_header, sizeof(http_response_header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html; charset=UTF-8\r\n"
           "%.*s"
           "%s"
           "Content-Length: %zu\r\n"
           "%s"
           "Connection: %s\r\n"
           "\r\n",
           seclen, sec,
           gz ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n"
              : "Vary: Accept-Encoding\r\n",
           body_size, cookie_headers, beaker_connection_label());

  if (beaker_send_all(current_client_socket, http_response_header,
                      strlen(http_response_header)) < 0) {
    beaker_log_errno_format("ERROR",
                            "send_response: Failed to send HTTP header.\n");
    free(gz);
    return;
  }

  if (beaker_send_all(current_client_socket, (const char *)body, body_size) <
      0) {
    beaker_log_errno_format("ERROR",
                            "send_response: Failed to send HTML body.\n");
    free(gz);
    return;
  }
  free(gz);
}

void send_response_with_status(const char *status_line, const char *html,
                               const char *extra_headers) {
  if (current_client_socket == -1) {
    beaker_log(
        "ERROR",
        "send_response_with_status: No client socket set. Cannot send "
        "response.\n");
    return;
  }

  int status_code;
  if (!parse_status_line(status_line, &status_code)) {
    beaker_log(
        "SECURITY",
        "send_response_with_status: Rejected invalid HTTP status line.");
    status_line = "500 Internal Server Error";
    status_code = 500;
  }

  if (extra_headers &&
      (extra_headers[0] == '\0' ||
       !beaker_is_valid_header_value(extra_headers))) {
    beaker_log(
        "SECURITY",
        "send_response_with_status: Rejected invalid extra headers.");
    extra_headers = NULL;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  char http_response_header[BUFFER_SIZE * 2 + 6144];
  size_t content_length = strlen(html);

  unsigned char *gz = NULL;
  const unsigned char *body = (const unsigned char *)html;
  size_t body_size = content_length;
  if (content_length > GZIP_MIN_SIZE && beaker_client_accepts_gzip()) {
    gz = beaker_gzip_compress((const unsigned char *)html, content_length, &body_size);
    if (gz)
      body = gz;
    else
      body_size = content_length;
  }

  current_response_status = status_code;
  current_response_size = body_size;
  char cookie_headers[BUFFER_SIZE];

  build_cookie_headers(cookie_headers, sizeof(cookie_headers));
  beaker_clear_response_cookies();

  int header_length = snprintf(http_response_header,
                               sizeof(http_response_header),
                               "HTTP/1.1 %s\r\n"
                               "Content-Type: text/html; charset=UTF-8\r\n"
                               "%.*s"
                               "%s"
                               "Content-Length: %zu\r\n"
                               "%s%s%s"
                               "Connection: %s\r\n"
                               "\r\n",
                               status_line, seclen, sec,
                               gz ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n"
                                  : "Vary: Accept-Encoding\r\n",
                               body_size, cookie_headers,
                               extra_headers ? extra_headers : "",
                               extra_headers ? "\r\n" : "",
                               beaker_connection_label());
  if (header_length < 0 ||
      (size_t)header_length >= sizeof(http_response_header)) {
    beaker_log("ERROR",
               "send_response_with_status: Failed to construct response.\n");
    free(gz);
    return;
  }

  if (beaker_send_all(current_client_socket, http_response_header,
                      strlen(http_response_header)) < 0) {
    beaker_log_errno_format(
        "ERROR", "send_response_with_status: Failed to send HTTP header.\n");
    free(gz);
    return;
  }

  if (beaker_send_all(current_client_socket, (const char *)body, body_size) <
      0) {
    beaker_log_errno_format(
        "ERROR", "send_response_with_status: Failed to send HTML body.\n");
    free(gz);
    return;
  }
  free(gz);
}

void send_redirect(const char *location) {

  if (current_client_socket == -1) {
    beaker_log("ERROR",
               "send_redirect: No client socket set. Cannot send redirect.\n");
    return;
  }

  if (location == NULL || location[0] == '\0' ||
      !beaker_is_valid_header_value(location)) {
    beaker_log("SECURITY", "send_redirect: Rejected invalid Location value.");
    beaker_clear_response_cookies();
    send_status("500 Internal Server Error");
    return;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  char http_response_header[BUFFER_SIZE * 2 + 6144];
  current_response_status = 302;
  current_response_size = 0;
  char cookie_headers[BUFFER_SIZE];

  build_cookie_headers(cookie_headers, sizeof(cookie_headers));
  beaker_clear_response_cookies();

  int header_length =
      snprintf(http_response_header, sizeof(http_response_header),
               "HTTP/1.1 302 Found\r\n"
               "%.*s"
               "Location: %s\r\n"
               "%s"
               "Content-Length: 0\r\n"
               "Cache-Control: no-store\r\n"
               "Connection: %s\r\n"
               "\r\n",
               seclen, sec, location, cookie_headers,
               beaker_connection_label());
  if (header_length < 0 ||
      (size_t)header_length >= sizeof(http_response_header)) {
    beaker_log("SECURITY", "send_redirect: Location value is too long.");
    send_status("500 Internal Server Error");
    return;
  }

  if (beaker_send_all(current_client_socket, http_response_header,
                      strlen(http_response_header)) < 0) {
    beaker_log_errno_format("ERROR",
                            "send_redirect: Failed to send redirect header.\n");
    return;
  }
}

void set_cookie(const char *name, const char *value, const char *expires,
                const char *path, bool http_only, bool secure) {

  if (!beaker_is_valid_http_token(name) || !is_valid_cookie_value(value) ||
      !is_valid_cookie_attribute(expires) || !is_valid_cookie_attribute(path) ||
      strlen(name) >= MAX_KEY_LEN || strlen(value) >= MAX_VALUE_LEN ||
      (expires != NULL && strlen(expires) >= MAX_VALUE_LEN) ||
      (path != NULL && strlen(path) >= MAX_KEY_LEN)) {
    beaker_log("SECURITY", "set_cookie: Rejected invalid cookie data.");
    return;
  }

  if (cookies_to_set_count >= MAX_COOKIES) {
    beaker_log("WARN",
               "set_cookie: Maximum number of cookies to set reached. Cannot "
               "set cookie '%s'.\n",
               name);
    return;
  }

  Cookie *new_cookie = &cookies_to_set[cookies_to_set_count];

  strncpy(new_cookie->name, name, MAX_KEY_LEN - 1);
  new_cookie->name[MAX_KEY_LEN - 1] = '\0';

  strncpy(new_cookie->value, value, MAX_VALUE_LEN - 1);
  new_cookie->value[MAX_VALUE_LEN - 1] = '\0';

  if (expires && strlen(expires) > 0) {
    strncpy(new_cookie->expires, expires, MAX_VALUE_LEN - 1);
    new_cookie->expires[MAX_VALUE_LEN - 1] = '\0';
  } else {
    new_cookie->expires[0] = '\0';
  }

  if (path && strlen(path) > 0) {
    strncpy(new_cookie->path, path, MAX_KEY_LEN - 1);
    new_cookie->path[MAX_KEY_LEN - 1] = '\0';
  } else {
    new_cookie->path[0] = '\0';
  }

  new_cookie->http_only = http_only;
  new_cookie->secure = secure;

  cookies_to_set_count++;
}

char *get_cookie(const char *cookie_name) {

  char *cookie_header_start = strstr(current_request_buffer, "\r\nCookie: ");
  if (cookie_header_start == NULL) {
    return NULL;
  }

  cookie_header_start += strlen("\r\nCookie: ");

  char *cookie_header_end = strstr(cookie_header_start, "\r\n");
  if (cookie_header_end == NULL) {

    cookie_header_end =
        (char *)(current_request_buffer + strlen(current_request_buffer));
  }

  size_t cookie_str_len = cookie_header_end - cookie_header_start;
  char *cookie_str = (char *)malloc(cookie_str_len + 1);
  if (cookie_str == NULL) {
    beaker_log_errno_format("ERROR",
                            "get_cookie: Allocation failed for cookie_str.\n");
    return NULL;
  }

  strncpy(cookie_str, cookie_header_start, cookie_str_len);
  cookie_str[cookie_str_len] = '\0';

  char *token;
  char *saveptr_cookie;

  token = strtok_r(cookie_str, ";", &saveptr_cookie);
  while (token != NULL) {

    while (*token == ' ') {
      token++;
    }

    char *equals_sign = strchr(token, '=');
    if (equals_sign != NULL) {
      size_t name_len = equals_sign - token;

      if (name_len == strlen(cookie_name) &&
          strncmp(token, cookie_name, name_len) == 0) {

        char *cookie_value = strdup(equals_sign + 1);
        if (cookie_value == NULL) {
          beaker_log_errno_format(
              "ERROR", "get_cookie: Allocation failed for cookie_value.\n");
        }
        free(cookie_str);
        return cookie_value;
      }
    }

    token = strtok_r(NULL, ";", &saveptr_cookie);
  }

  free(cookie_str);
  return NULL;
}
