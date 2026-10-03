#ifndef WEATHER_H
#define WEATHER_H

#include "Infobox.h"

int is_weather_query(const char *query);
InfoBox fetch_weather_data(char *query);

#endif
