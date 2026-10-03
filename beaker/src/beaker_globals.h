#ifndef BEAKER_GLOBALS_H
#define BEAKER_GLOBALS_H

#include "../beaker.h"
#include <stdatomic.h>
#include <stddef.h>

#ifndef BEAKER_REQUEST_HEADER_TIMEOUT_MS
#define BEAKER_REQUEST_HEADER_TIMEOUT_MS 5000
#endif

#ifndef BEAKER_KEEPALIVE_IDLE_TIMEOUT_MS
#define BEAKER_KEEPALIVE_IDLE_TIMEOUT_MS 3000
#endif

#ifndef BEAKER_RESPONSE_WRITE_TIMEOUT_MS
#define BEAKER_RESPONSE_WRITE_TIMEOUT_MS 30000
#endif

typedef enum {
  BEAKER_REQUEST_READ_OK = 0,
  BEAKER_REQUEST_READ_CLOSED,
  BEAKER_REQUEST_READ_TIMEOUT,
  BEAKER_REQUEST_READ_TOO_LARGE,
  BEAKER_REQUEST_READ_ERROR,
} BeakerRequestReadResult;

extern RouteHandler handlers[MAX_HANDLERS];

extern int handler_count;

extern __thread int current_client_socket;

extern __thread int current_keep_alive;

const char *beaker_connection_label(void);

extern __thread Cookie cookies_to_set[MAX_COOKIES];

extern __thread int cookies_to_set_count;

extern __thread char current_request_buffer[BUFFER_SIZE];

extern __thread RequestInfo current_request_info;

extern __thread int current_response_status;

extern __thread size_t current_response_size;

void beaker_log(const char *level, const char *format, ...);

void beaker_log_errno_format(const char *level, const char *format, ...);

void beaker_log_errno(const char *message);

void beaker_log_request(const char *remote_addr, const char *method,
                        const char *path, int status, size_t response_size,
                        double duration_ms);

void beaker_clear_response_cookies(void);

BeakerRequestReadResult beaker_read_request_headers(int socket, char *buffer,
                                                    size_t buffer_size,
                                                    size_t *bytes_read);

BeakerRequestReadResult
beaker_read_request_headers_ex(int socket, char *buffer, size_t buffer_size,
                               size_t *bytes_read, int64_t timeout_ms);

int beaker_configure_client_socket(int socket);

int beaker_send_all(int socket, const void *buffer, size_t length);

void beaker_reset_write_deadline(void);

bool beaker_is_valid_http_token(const char *value);

bool beaker_is_valid_http_token_span(const char *value, size_t length);

bool beaker_is_valid_header_value(const char *value);

extern Locale *locales;

extern int locale_count;

extern int locale_capacity;

#endif
