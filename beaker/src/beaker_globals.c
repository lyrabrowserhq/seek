#include "beaker_globals.h"

RouteHandler handlers[MAX_HANDLERS];

int handler_count = 0;

__thread int current_client_socket = -1;

__thread int current_keep_alive = 0;

__thread Cookie cookies_to_set[MAX_COOKIES];

__thread int cookies_to_set_count = 0;

__thread char current_request_buffer[BUFFER_SIZE];

__thread RequestInfo current_request_info = {0};

__thread int current_response_status = 0;

__thread size_t current_response_size = 0;

Locale *locales = NULL;

int locale_count = 0;

int locale_capacity = 0;

const char *beaker_connection_label(void) {
  return current_keep_alive ? "keep-alive" : "close";
}
