#include "beaker_globals.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static __thread int64_t write_deadline_ms = 0;

int beaker_configure_client_socket(int socket) {
  int flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }

  if ((flags & O_NONBLOCK) == 0 &&
      fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
    return -1;
  }

#ifdef SO_NOSIGPIPE
  int enabled = 1;
  if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) <
      0) {
    return -1;
  }
#endif

  return 0;
}

static int64_t monotonic_time_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return -1;
  }
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_for_socket(int socket, short events, int64_t deadline_ms) {
  for (;;) {
    int64_t now_ms = monotonic_time_ms();
    if (now_ms < 0) {
      return -1;
    }

    int64_t remaining_ms = deadline_ms - now_ms;
    if (remaining_ms <= 0) {
      errno = ETIMEDOUT;
      return -1;
    }

    int timeout_ms = remaining_ms > INT_MAX ? INT_MAX : (int)remaining_ms;
    struct pollfd descriptor = {.fd = socket, .events = events};
    int result = poll(&descriptor, 1, timeout_ms);

    if (result > 0) {
      if (descriptor.revents & POLLNVAL) {
        errno = EBADF;
        return -1;
      }
      if (descriptor.revents & (events | POLLERR | POLLHUP)) {
        return 0;
      }
      continue;
    }
    if (result == 0) {
      errno = ETIMEDOUT;
      return -1;
    }
    if (errno != EINTR) {
      return -1;
    }
  }
}

static bool contains_complete_headers(const char *buffer, size_t length) {
  if (length < 4) {
    return false;
  }

  for (size_t i = 0; i <= length - 4; i++) {
    if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' &&
        buffer[i + 3] == '\n') {
      return true;
    }
  }
  return false;
}

BeakerRequestReadResult
beaker_read_request_headers_ex(int socket, char *buffer, size_t buffer_size,
                               size_t *bytes_read, int64_t timeout_ms) {
  if (buffer == NULL || bytes_read == NULL || buffer_size < 2) {
    errno = EINVAL;
    return BEAKER_REQUEST_READ_ERROR;
  }

  *bytes_read = 0;
  buffer[0] = '\0';

  int64_t now_ms = monotonic_time_ms();
  if (now_ms < 0) {
    return BEAKER_REQUEST_READ_ERROR;
  }
  int64_t deadline_ms = now_ms + timeout_ms;

  while (*bytes_read < buffer_size - 1) {
    ssize_t result =
        recv(socket, buffer + *bytes_read, buffer_size - 1 - *bytes_read, 0);
    if (result > 0) {
      *bytes_read += (size_t)result;
      buffer[*bytes_read] = '\0';
      if (contains_complete_headers(buffer, *bytes_read)) {
        return BEAKER_REQUEST_READ_OK;
      }
      continue;
    }
    if (result == 0) {
      return BEAKER_REQUEST_READ_CLOSED;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      return BEAKER_REQUEST_READ_ERROR;
    }
    if (wait_for_socket(socket, POLLIN, deadline_ms) != 0) {
      return errno == ETIMEDOUT ? BEAKER_REQUEST_READ_TIMEOUT
                                : BEAKER_REQUEST_READ_ERROR;
    }
  }

  return BEAKER_REQUEST_READ_TOO_LARGE;
}

BeakerRequestReadResult beaker_read_request_headers(int socket, char *buffer,
                                                    size_t buffer_size,
                                                    size_t *bytes_read) {
  return beaker_read_request_headers_ex(
      socket, buffer, buffer_size, bytes_read, BEAKER_REQUEST_HEADER_TIMEOUT_MS);
}

void beaker_reset_write_deadline(void) { write_deadline_ms = 0; }

int beaker_send_all(int socket, const void *buffer, size_t length) {
  if (buffer == NULL && length != 0) {
    errno = EINVAL;
    return -1;
  }

  if (write_deadline_ms == 0) {
    int64_t now_ms = monotonic_time_ms();
    if (now_ms < 0) {
      return -1;
    }
    write_deadline_ms = now_ms + BEAKER_RESPONSE_WRITE_TIMEOUT_MS;
  }

  const char *bytes = buffer;
  size_t bytes_sent = 0;
  while (bytes_sent < length) {
#ifdef MSG_NOSIGNAL
    ssize_t result =
        send(socket, bytes + bytes_sent, length - bytes_sent, MSG_NOSIGNAL);
#else
    ssize_t result = send(socket, bytes + bytes_sent, length - bytes_sent, 0);
#endif
    if (result > 0) {
      bytes_sent += (size_t)result;
      continue;
    }
    if (result == 0) {
      errno = EPIPE;
      return -1;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      return -1;
    }
    if (wait_for_socket(socket, POLLOUT, write_deadline_ms) != 0) {
      return -1;
    }
  }

  return 0;
}
