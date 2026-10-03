#ifndef DICTIONARY_H
#define DICTIONARY_H

#include "Infobox.h"

InfoBox fetch_dictionary_data(const char *word);
char *construct_dictionary_url(const char *word);
int is_dictionary_query(const char *query);

#endif
