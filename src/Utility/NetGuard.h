#ifndef NETGUARD_H
#define NETGUARD_H

#include <curl/curl.h>
#include <sys/socket.h>

int netguard_addr_blocked(const struct sockaddr *sa);
int netguard_host_is_public_literal_or_name(const char *host);
int netguard_url_host(const char *url, char *host, size_t host_sz);
CURLcode netguard_perform(CURL *curl, const char *url, int max_redirs,
                          void (*reset)(void *), void *reset_arg);

#endif
