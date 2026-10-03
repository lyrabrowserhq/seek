#ifndef IMAGE_PROXY_HANDLER_H
#define IMAGE_PROXY_HANDLER_H

#include <beaker.h>

int image_proxy_handler(UrlParams *params);

char *proxy_wrap_image_url(const char *url);

#endif
