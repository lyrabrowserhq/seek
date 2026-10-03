#ifndef DEVTOOLS_H
#define DEVTOOLS_H

#include "Infobox.h"

int is_devtools_query(const char *query);
InfoBox fetch_devtools_data(char *query);

#endif
