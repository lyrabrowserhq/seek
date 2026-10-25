#ifndef SEEKER_PREFS_H
#define SEEKER_PREFS_H

#include <beaker.h>

#define PREF_COOKIE_EXPIRES "Fri, 31 Dec 2038 23:59:59 GMT"

int prefs_domain_handler(UrlParams *params);
int prefs_lens_handler(UrlParams *params);

#endif
