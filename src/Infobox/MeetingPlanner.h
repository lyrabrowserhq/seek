#ifndef MEETING_PLANNER_H
#define MEETING_PLANNER_H

#include "Infobox.h"

int is_meeting_planner_query(const char *query);
InfoBox fetch_meeting_planner_data(char *query);

#endif
