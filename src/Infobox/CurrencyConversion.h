#ifndef CURRENCYCONVERSION_H
#define CURRENCYCONVERSION_H

#include "Infobox.h"

int is_currency_query(const char *query);
InfoBox fetch_currency_data(const char *query);

#endif
