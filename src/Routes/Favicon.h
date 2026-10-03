#ifndef FAVICON_H
#define FAVICON_H

#include <beaker.h>
#include <stddef.h>

int favicon_handler(UrlParams *params);
char *favicon_proxy_url_for_page(const char *page_url);
char *favicon_proxy_url_for_host(const char *host);
char *favicon_url_for_engine_id(const char *engine_id);
void favicon_prime_pages(const char **page_urls, int n);
int favicon_host_hash(const char *host, char *out, size_t out_sz);
int favicon_parent_host(const char *host, char *out, size_t out_sz);
int favicon_href_from_html(const char *html, const char *host, char *out,
                           size_t out_sz);

#endif
