#ifndef WIDGET_H
#define WIDGET_H

/*
 * Instant answers (widgets) use the same InfoBox pipeline as Search.c:
 *
 *   1. Add int  is_<name>_query(const char *query);
 *   2. Add InfoBox fetch_<name>_data(char *query);
 *   3. Register { is_fn, fetch_fn, NULL } in Search.c handlers[] (order matters
 *      for sidebar stacking; Wikipedia stays last with always_true).
 *   4. Map the handler index in infobox_modifier_class() to classes:
 *        " infobox--widget infobox--<id>"
 *      Wikipedia uses " infobox--wikipedia" only.
 *   5. Render HTML with outer class "widget" plus "widget-<id>" (see main.css).
 * Example: weather uses "weather <city>" and data-widget="weather" in Weather.c.
 */

#endif
