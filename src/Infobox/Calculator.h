#ifndef CALCULATOR_H
#define CALCULATOR_H

#include "Infobox.h"

double evaluate(const char *expr);
int calc_query_matches(const char *query);
InfoBox fetch_calc_data(char *math_input);

#endif
