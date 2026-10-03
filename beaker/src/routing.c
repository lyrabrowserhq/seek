/*
 * Modified by the Lyra project on 2026-10-03: image responses send Cache-Control: public, max-age=86400.
 * Upstream: https://git.bwaaa.monster/beaker (commit acdd9e7).
 * This file remains under the LGPL-2.1; see ../LICENSE.
 */
#include "../beaker.h"
#include "beaker_globals.h"
#include "gzip.h"
#include "security_headers.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void set_handler(const char *path, RequestHandler handler) {

  if (handler_count < MAX_HANDLERS) {

    strncpy(handlers[handler_count].path, path, MAX_PATH_LEN - 1);
    handlers[handler_count].path[MAX_PATH_LEN - 1] = '\0';

    handlers[handler_count].handler = handler;

    handler_count++;
  } else {

    beaker_log("WARN",
               "set_handler: Maximum number of handlers reached. Cannot "
               "register handler for '%s'.\n",
               path);
  }
}

const char *get_mime_type(const char *file_path) {

  const char *ext = strrchr(file_path, '.');
  if (!ext) {
    return "application/octet-stream";
  }
  ext++;

  if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
    return "text/html";
  if (strcmp(ext, "css") == 0)
    return "text/css";
  if (strcmp(ext, "js") == 0)
    return "application/javascript";
  if (strcmp(ext, "json") == 0)
    return "application/json";
  if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0)
    return "image/jpeg";
  if (strcmp(ext, "png") == 0)
    return "image/png";
  if (strcmp(ext, "gif") == 0)
    return "image/gif";
  if (strcmp(ext, "ico") == 0)
    return "image/x-icon";
  if (strcmp(ext, "svg") == 0)
    return "image/svg+xml";
  if (strcmp(ext, "pdf") == 0)
    return "application/pdf";
  if (strcmp(ext, "txt") == 0)
    return "text/plain";

  return "application/octet-stream";
}

static int hex_value(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static int url_decode(char *dst, size_t dst_size, const char *src,
                      size_t src_len, bool plus_as_space) {
  size_t src_index = 0;
  size_t dst_index = 0;

  if (dst == NULL || dst_size == 0 || src == NULL)
    return -1;

  while (src_index < src_len) {
    unsigned char decoded;
    if (src[src_index] == '%') {
      if (src_index + 2 >= src_len) {
        beaker_log("SECURITY", "url_decode: Incomplete percent encoding.");
        return -1;
      }
      int high = hex_value((unsigned char)src[src_index + 1]);
      int low = hex_value((unsigned char)src[src_index + 2]);
      if (high < 0 || low < 0) {
        beaker_log("SECURITY", "url_decode: Invalid percent encoding.");
        return -1;
      }
      decoded = (unsigned char)((high << 4) | low);
      src_index += 3;
    } else {
      decoded = (unsigned char)src[src_index++];
      if (plus_as_space && decoded == '+')
        decoded = ' ';
    }

    if (decoded == 0 || decoded < 0x20 || decoded == 0x7f) {
      beaker_log("SECURITY", "url_decode: Rejected control character.");
      return -1;
    }
    if (dst_index + 1 >= dst_size) {
      beaker_log("SECURITY", "url_decode: Decoded value is too long.");
      return -1;
    }
    dst[dst_index++] = (char)decoded;
  }

  dst[dst_index] = '\0';
  return 0;
}

static int canonicalize_path(char *canonical, const char *path,
                             size_t max_len) {
  if (canonical == NULL || path == NULL || max_len < 2 || path[0] != '/') {
    return -1;
  }

  size_t output_len = 1;
  canonical[0] = '/';
  canonical[1] = '\0';

  const char *cursor = path + 1;
  while (*cursor != '\0') {
    while (*cursor == '/')
      cursor++;
    if (*cursor == '\0')
      break;

    const char *component = cursor;
    while (*cursor != '\0' && *cursor != '/')
      cursor++;
    size_t component_len = (size_t)(cursor - component);

    if (component_len == 1 && component[0] == '.') {
      continue;
    }
    if (component_len == 2 && component[0] == '.' && component[1] == '.') {
      if (output_len == 1) {
        beaker_log("SECURITY", "canonicalize_path: Path escapes root.");
        return -1;
      }
      while (output_len > 1 && canonical[output_len - 1] != '/')
        output_len--;
      if (output_len > 1)
        output_len--;
      canonical[output_len] = '\0';
      continue;
    }

    for (size_t i = 0; i < component_len; i++) {
      unsigned char c = (unsigned char)component[i];
      if (c == '\\' || c < 0x20 || c == 0x7f) {
        beaker_log("SECURITY",
                   "canonicalize_path: Rejected unsafe path character.");
        return -1;
      }
    }

    size_t separator_len = output_len > 1 ? 1 : 0;
    if (output_len + separator_len + component_len >= max_len) {
      beaker_log("SECURITY", "canonicalize_path: Path is too long.");
      return -1;
    }
    if (separator_len != 0)
      canonical[output_len++] = '/';
    memcpy(canonical + output_len, component, component_len);
    output_len += component_len;
    canonical[output_len] = '\0';
  }

  return 0;
}

static int extract_request_target(const char *request_line, char *target,
                                  size_t target_size) {
  if (request_line == NULL || target == NULL || target_size == 0)
    return -1;

  const char *first_space = strchr(request_line, ' ');
  if (first_space == NULL || first_space == request_line ||
      first_space[1] == ' ')
    return -1;
  const char *second_space = strchr(first_space + 1, ' ');
  if (second_space == NULL || second_space == first_space + 1 ||
      second_space[1] == '\0' || strchr(second_space + 1, ' ') != NULL ||
      strchr(request_line, '\t') != NULL) {
    return -1;
  }

  size_t method_len = (size_t)(first_space - request_line);
  if (method_len >= 16)
    return -1;
  char method[16];
  memcpy(method, request_line, method_len);
  method[method_len] = '\0';
  if (!beaker_is_valid_http_token(method))
    return -1;

  size_t version_len = strlen(second_space + 1);
  if (version_len == 0 || version_len >= 16 ||
      !beaker_is_valid_header_value(second_space + 1)) {
    return -1;
  }

  size_t target_len = (size_t)(second_space - (first_space + 1));
  if (target_len >= target_size)
    return -1;
  memcpy(target, first_space + 1, target_len);
  target[target_len] = '\0';
  return 0;
}

static int parse_query_params(const char *query, UrlParams *params) {
  const char *cursor = query;
  while (*cursor != '\0') {
    const char *pair_end = strchr(cursor, '&');
    if (pair_end == NULL)
      pair_end = cursor + strlen(cursor);

    const char *equals = memchr(cursor, '=', (size_t)(pair_end - cursor));
    if (equals != NULL) {
      if (params->count >= MAX_URL_PARAMS) {
        beaker_log("SECURITY", "parse_query_params: Too many parameters.");
        return -1;
      }

      UrlParam *param = &params->params[params->count];
      if (url_decode(param->key, sizeof(param->key), cursor,
                     (size_t)(equals - cursor), true) != 0 ||
          url_decode(param->value, sizeof(param->value), equals + 1,
                     (size_t)(pair_end - (equals + 1)), true) != 0) {
        return -1;
      }
      params->count++;
    }

    if (*pair_end == '\0')
      break;
    cursor = pair_end + 1;
  }
  return 0;
}

char *parse_request_url(const char *request_line, UrlParams *params) {
  if (params == NULL)
    return NULL;
  params->count = 0;

  char raw_target[MAX_PATH_LEN];
  if (extract_request_target(request_line, raw_target, sizeof(raw_target)) !=
      0) {
    beaker_log("ERROR", "parse_request_url: Malformed request line.");
    return NULL;
  }

  char *query_start = strchr(raw_target, '?');
  size_t raw_path_len = query_start == NULL
                            ? strlen(raw_target)
                            : (size_t)(query_start - raw_target);
  if (raw_path_len == 0 || raw_target[0] != '/') {
    beaker_log("SECURITY", "parse_request_url: Invalid request target.");
    return NULL;
  }

  char decoded_path[MAX_PATH_LEN];
  if (url_decode(decoded_path, sizeof(decoded_path), raw_target, raw_path_len,
                 false) != 0) {
    return NULL;
  }

  char canonical_path[MAX_PATH_LEN];
  if (canonicalize_path(canonical_path, decoded_path, sizeof(canonical_path)) !=
      0) {
    return NULL;
  }

  if (query_start != NULL && parse_query_params(query_start + 1, params) != 0) {
    params->count = 0;
    return NULL;
  }

  char *final_path = strdup(canonical_path);
  if (final_path == NULL)
    beaker_log_errno("parse_request_url: Failed to allocate result path");
  return final_path;
}

bool serve_static_file_with_mime(const char *request_path_relative_to_static,
                                 const char *mime_type) {
  char full_static_path[MAX_PATH_LEN];

  if (request_path_relative_to_static == NULL ||
      strlen(request_path_relative_to_static) == 0) {
    beaker_log("ERROR", "serve_static_file_with_mime: Empty path provided\n");
    return false;
  }

  if (strstr(request_path_relative_to_static, "..") != NULL ||
      strstr(request_path_relative_to_static, "//") != NULL ||
      request_path_relative_to_static[0] == '/') {
    beaker_log("SECURITY", "Attempted directory traversal: %s\n",
               request_path_relative_to_static);
    send_status("403 Forbidden");
    return true;
  }

  snprintf(full_static_path, sizeof(full_static_path), "%s%s", STATIC_DIR,
           request_path_relative_to_static);

  FILE *fp = fopen(full_static_path, "rb");
  if (fp == NULL) {
    beaker_log("ERROR",
               "serve_static_file_with_mime: File '%s' not found or could not "
               "be opened. %s\n",
               full_static_path, strerror(errno));
    return false;
  }

  struct stat st;

  if (fstat(fileno(fp), &st) < 0) {
    beaker_log_errno_format(
        "ERROR", "serve_static_file_with_mime: fstat failed for '%s'.\n",
        full_static_path);
    fclose(fp);
    send_status("500 Internal Server Error");
    return true;
  }

  long file_size = st.st_size;

  if (!mime_type || mime_type[0] == '\0') {
    mime_type = get_mime_type(full_static_path);
  }

  if (!beaker_is_valid_header_value(mime_type) ||
      strlen(mime_type) >= MAX_VALUE_LEN) {
    beaker_log("SECURITY",
               "serve_static_file_with_mime: Rejected invalid MIME type.");
    fclose(fp);
    send_status("500 Internal Server Error");
    return true;
  }

  char etag[64];
  snprintf(etag, sizeof(etag), "W/\"%lx-%lx\"",
           (unsigned long)st.st_mtime, (unsigned long)file_size);

  char last_modified_header[96];
  struct tm *tm = gmtime(&st.st_mtime);
  if (tm) {
    char lm[64];
    strftime(lm, sizeof(lm), "%a, %d %b %Y %H:%M:%S GMT", tm);
    snprintf(last_modified_header, sizeof(last_modified_header),
             "Last-Modified: %s\r\n", lm);
  } else {
    last_modified_header[0] = '\0';
  }

  const char *if_none_match = beaker_get_header("If-None-Match");
  if (if_none_match &&
      (strstr(if_none_match, etag) != NULL ||
       strstr(if_none_match, "*") != NULL)) {
    char sec304[6144];
    int sec304len = beaker_format_security_headers(sec304, sizeof(sec304));
    if (sec304len < 0)
      sec304len = 0;
    char header304[BUFFER_SIZE + 6144];
    int len304 = snprintf(header304, sizeof(header304),
                          "HTTP/1.1 304 Not Modified\r\n"
                          "%.*s"
                          "ETag: %s\r\n"
                          "Cache-Control: public, max-age=86400\r\n"
                          "Connection: %s\r\n"
                          "\r\n",
                          sec304len, sec304, etag,
                          beaker_connection_label());
    fclose(fp);
    if (len304 > 0 && (size_t)len304 < sizeof(header304))
      beaker_send_all(current_client_socket, header304, strlen(header304));
    current_response_status = 304;
    current_response_size = 0;
    return true;
  }

  int compressible =
      strncmp(mime_type, "text/", 5) == 0 ||
      strstr(mime_type, "javascript") != NULL ||
      strstr(mime_type, "json") != NULL || strstr(mime_type, "xml") != NULL ||
      strstr(mime_type, "svg") != NULL;

  unsigned char *file_data = NULL;
  unsigned char *gz = NULL;
  const unsigned char *body = NULL;
  size_t body_size = (size_t)file_size;

  if (file_size > 0 && file_size <= 16 * 1024 * 1024) {
    file_data = malloc((size_t)file_size);
    if (file_data &&
        fread(file_data, 1, (size_t)file_size, fp) != (size_t)file_size) {
      free(file_data);
      file_data = NULL;
    }
    if (file_data) {
      body = file_data;
      if (compressible && (size_t)file_size > GZIP_MIN_SIZE &&
          beaker_client_accepts_gzip()) {
        gz = beaker_gzip_compress(file_data, (size_t)file_size, &body_size);
        if (gz)
          body = gz;
        else
          body_size = (size_t)file_size;
      }
    }
  }
  fclose(fp);

  if (file_size > 0 && !body) {
    send_status("500 Internal Server Error");
    return true;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  char http_header[BUFFER_SIZE + 6144];
  current_response_status = 200;
  current_response_size = body_size;

  int header_length = snprintf(http_header, sizeof(http_header),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: %s\r\n"
                               "%.*s"
                               "%s"
                               "Content-Length: %zu\r\n"
                               "Cache-Control: public, max-age=86400\r\n"
                               "ETag: %s\r\n"
                               "%s"
                               "Connection: %s\r\n"
                               "\r\n",
                               mime_type, seclen, sec,
                               gz ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n"
                                  : "Vary: Accept-Encoding\r\n",
                               body_size, etag,
                               last_modified_header,
                               beaker_connection_label());
  if (header_length < 0 || (size_t)header_length >= sizeof(http_header)) {
    beaker_log("ERROR",
               "serve_static_file_with_mime: Failed to construct header.");
    free(file_data);
    free(gz);
    send_status("500 Internal Server Error");
    return true;
  }

  if (beaker_send_all(current_client_socket, http_header, strlen(http_header)) <
      0) {
    beaker_log_errno_format(
        "ERROR",
        "serve_static_file_with_mime: Failed to send header for '%s'.\n",
        full_static_path);
    free(file_data);
    free(gz);
    return true;
  }

  if (body_size > 0 &&
      beaker_send_all(current_client_socket, (const char *)body, body_size) <
          0) {
    beaker_log_errno_format(
        "ERROR",
        "serve_static_file_with_mime: Failed to send content for '%s'.\n",
        full_static_path);
  }

  free(file_data);
  free(gz);
  return true;
}

bool serve_static_file(const char *request_path_relative_to_static) {
  return serve_static_file_with_mime(request_path_relative_to_static, NULL);
}

bool serve_data(const char *data, size_t size, const char *mime_type) {

  if (current_client_socket == -1) {
    beaker_log("ERROR",
               "serve_data: No client socket set. Cannot send data.\n");
    return false;
  }

  if (data == NULL || size == 0) {
    beaker_log("ERROR", "serve_data: Invalid data or size.\n");
    return false;
  }

  if (mime_type == NULL || mime_type[0] == '\0' ||
      !beaker_is_valid_header_value(mime_type) ||
      strlen(mime_type) >= MAX_VALUE_LEN) {
    beaker_log("SECURITY", "serve_data: Rejected invalid MIME type.");
    send_status("500 Internal Server Error");
    return false;
  }

  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;

  unsigned char *gz = NULL;
  const unsigned char *body = (const unsigned char *)data;
  size_t body_size = size;
  if (size > GZIP_MIN_SIZE && beaker_client_accepts_gzip()) {
    gz = beaker_gzip_compress((const unsigned char *)data, size, &body_size);
    if (gz)
      body = gz;
    else
      body_size = size;
  }

  char http_header[BUFFER_SIZE + 6144];
  current_response_status = 200;
  current_response_size = body_size;

  int header_length = snprintf(http_header, sizeof(http_header),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: %s\r\n"
                               "%.*s"
                               "%s"
                               "%s"
                               "Content-Length: %zu\r\n"
                               "Connection: %s\r\n"
                               "\r\n",
                               mime_type, seclen, sec,
                               gz ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n"
                                  : "Vary: Accept-Encoding\r\n",
                               (strncmp(mime_type, "image/", 6) == 0)
                                   ? "Cache-Control: public, max-age=86400\r\n"
                                   : "",
                               body_size, beaker_connection_label());
  if (header_length < 0 || (size_t)header_length >= sizeof(http_header)) {
    beaker_log("ERROR", "serve_data: Failed to construct header.");
    send_status("500 Internal Server Error");
    return false;
  }

  if (beaker_send_all(current_client_socket, http_header, strlen(http_header)) <
      0) {
    beaker_log_errno_format("ERROR", "serve_data: Failed to send header.\n");
    free(gz);
    return false;
  }

  if (beaker_send_all(current_client_socket, (const char *)body, body_size) <
      0) {
    beaker_log_errno_format("ERROR", "serve_data: Failed to send content.\n");
    free(gz);
    return false;
  }

  free(gz);
  return true;
}
