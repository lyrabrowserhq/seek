#include "beaker_globals.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_NONE
};

static int configured_log_level(void) {
  const char *level = getenv("BEAKER_LOG_LEVEL");

  if (level == NULL || strcasecmp(level, "INFO") == 0)
    return LOG_LEVEL_INFO;
  if (strcasecmp(level, "DEBUG") == 0)
    return LOG_LEVEL_DEBUG;
  if (strcasecmp(level, "WARN") == 0 || strcasecmp(level, "WARNING") == 0)
    return LOG_LEVEL_WARN;
  if (strcasecmp(level, "ERROR") == 0)
    return LOG_LEVEL_ERROR;
  if (strcasecmp(level, "NONE") == 0 || strcasecmp(level, "OFF") == 0)
    return LOG_LEVEL_NONE;
  return LOG_LEVEL_INFO;
}

static int message_log_level(const char *level) {
  if (strcasecmp(level, "DEBUG") == 0)
    return LOG_LEVEL_DEBUG;
  if (strcasecmp(level, "INFO") == 0 || strcasecmp(level, "ACCESS") == 0)
    return LOG_LEVEL_INFO;
  if (strcasecmp(level, "WARN") == 0 || strcasecmp(level, "WARNING") == 0 ||
      strcasecmp(level, "SECURITY") == 0)
    return LOG_LEVEL_WARN;
  return LOG_LEVEL_ERROR;
}

static void log_timestamp(char *buffer, size_t size) {
  struct timespec now;
  struct tm local;

  clock_gettime(CLOCK_REALTIME, &now);
  localtime_r(&now.tv_sec, &local);
  strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &local);
}

static void log_safe_value(char *output, const char *input, size_t size) {
  size_t i;

  for (i = 0; i + 1 < size && input[i] != '\0'; i++) {
    unsigned char c = input[i];
    output[i] = c <= ' ' || c == 127 || c == '"' || c == '\\' ? '_' : c;
  }
  output[i] = '\0';
}

void beaker_log(const char *level, const char *format, ...) {
  char message[BUFFER_SIZE];
  char timestamp[32];
  va_list args;

  if (message_log_level(level) < configured_log_level())
    return;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  size_t length = strlen(message);
  while (length > 0 &&
         (message[length - 1] == '\n' || message[length - 1] == '\r'))
    message[--length] = '\0';
  log_timestamp(timestamp, sizeof(timestamp));
  flockfile(stderr);
  fprintf(stderr, "%s %-6s %s\n", timestamp, level, message);
  funlockfile(stderr);
}

void beaker_log_errno_format(const char *level, const char *format, ...) {
  char message[BUFFER_SIZE];
  int error = errno;
  va_list args;

  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  size_t length = strlen(message);
  while (length > 0 &&
         (message[length - 1] == '\n' || message[length - 1] == '\r'))
    message[--length] = '\0';
  if (length > 0 && message[length - 1] == '.')
    message[length - 1] = '\0';
  beaker_log(level, "%s: %s", message, strerror(error));
}

void beaker_log_errno(const char *message) {
  int error = errno;
  beaker_log("ERROR", "%s: %s", message, strerror(error));
}

void beaker_log_request(const char *remote_addr, const char *method,
                        const char *path, int status, size_t response_size,
                        double duration_ms) {
  char safe_method[16];
  char safe_path[MAX_PATH_LEN];

  log_safe_value(safe_method, method, sizeof(safe_method));
  log_safe_value(safe_path, path, sizeof(safe_path));
  beaker_log("ACCESS", "%s \"%s %s\" %03d %zu %.3fms", remote_addr, safe_method,
             safe_path, status, response_size, duration_ms);
}
