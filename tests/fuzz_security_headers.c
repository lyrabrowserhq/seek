#include "beaker_globals.h"
#include "security_headers.h"
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  beaker_set_csp_policy(NULL);
  beaker_set_permissions_policy(NULL);

  memset(current_request_buffer, 0, sizeof(current_request_buffer));
  size_t n = size < sizeof(current_request_buffer) - 1 ? size
                                                        : sizeof(current_request_buffer) - 1;
  memcpy(current_request_buffer, data, n);
  current_request_buffer[n] = '\0';

  uint8_t opt = size > 0 ? data[0] : 0;
  if (opt & 1)
    beaker_set_hsts_max_age_sec(3600);
  else if (opt & 2)
    beaker_set_hsts_max_age_sec(-1);
  else
    beaker_set_hsts_max_age_sec(0);
  beaker_set_hsts_include_subdomains((opt & 4) ? 1 : 0);

  char buf[16384];
  (void)beaker_format_security_headers(buf, sizeof(buf));
  (void)beaker_snprint_http_empty_response(buf, sizeof(buf), "HTTP/1.1 204 No Content");
  (void)beaker_snprint_http_html_response(buf, sizeof(buf), "HTTP/1.1 200 OK", "<p>x</p>");
  return 0;
}
