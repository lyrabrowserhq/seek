#ifndef JSON_ENGINES_H
#define JSON_ENGINES_H

#include "Scraping.h"

int parse_json_engine(const char *engine_id, const char *json,
                      SearchResult **out_results, int max_results);

#endif
