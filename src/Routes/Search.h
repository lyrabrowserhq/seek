#ifndef SEARCH_HANDLER_H
#define SEARCH_HANDLER_H

#include <beaker.h>

int results_handler(UrlParams *params);
int json_search_handler(UrlParams *params);
int rss_handler(UrlParams *params);

#endif
