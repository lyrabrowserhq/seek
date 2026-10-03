#ifndef RATE_LIMIT_H
#define RATE_LIMIT_H

#include <stddef.h>

typedef struct {
  int max_requests;
  int interval_seconds;
} RateLimitConfig;

typedef struct {
  int limited;
  int retry_after_seconds;
} RateLimitResult;

void rate_limit_get_client_key(char *client_key, size_t client_key_size);
int rate_limit_ip_is_local(const char *ip);
RateLimitResult rate_limit_check(const char *scope,
                                 const RateLimitConfig *config);
RateLimitResult rate_limit_check_ip(const char *scope,
                                    const RateLimitConfig *config,
                                    const char *raw_ip);

#endif
