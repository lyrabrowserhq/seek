#include "Weather.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void trim_in(const char *in, char *out, size_t cap) {
  if (!in || cap == 0) {
    if (cap > 0)
      out[0] = '\0';
    return;
  }
  while (*in && isspace((unsigned char)*in))
    in++;
  size_t len = strlen(in);
  while (len > 0 && isspace((unsigned char)in[len - 1]))
    len--;
  if (len >= cap)
    len = cap - 1;
  memcpy(out, in, len);
  out[len] = '\0';
}

int is_weather_query(const char *query) {
  if (!query)
    return 0;
  const char *p = query;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (strncasecmp(p, "weather ", 8) != 0)
    return 0;
  p += 8;
  while (*p && isspace((unsigned char)*p))
    p++;
  return *p != '\0';
}

InfoBox fetch_weather_data(char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  const char *p = query;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (strncasecmp(p, "weather ", 8) != 0)
    return info;
  p += 8;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p == '\0')
    return info;

  char city[256];
  trim_in(p, city, sizeof(city));
  if (city[0] == '\0')
    return info;

  char esc[512];
  html_escape_attr(city, esc, sizeof(esc));

  char fb_raw[384];
  snprintf(fb_raw, sizeof(fb_raw), "Weather for %s requires JavaScript.", city);
  char fb_esc[512];
  html_escape_text(fb_raw, fb_esc, sizeof(fb_esc));

  char html[1536];
  snprintf(html, sizeof(html),
           "<div class=\"widget widget-weather\" data-widget=\"weather\" "
           "data-city=\"%s\">"
           "<div class=\"widget-weather-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           esc, fb_esc);

  info.title = strdup("Weather");
  info.extract = strdup(html);
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");
  return info;
}
