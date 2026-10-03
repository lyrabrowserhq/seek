#include "MeetingPlanner.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

typedef struct {
  const char *alias;
  const char *iana;
} TzAlias;

static const TzAlias TZ_ALIASES[] = {
    {"utc", "UTC"},           {"gmt", "GMT"},
    {"london", "Europe/London"},
    {"uk", "Europe/London"},
    {"paris", "Europe/Paris"},
    {"berlin", "Europe/Berlin"},
    {"tokyo", "Asia/Tokyo"},
    {"japan", "Asia/Tokyo"},
    {"sydney", "Australia/Sydney"},
    {"nyc", "America/New_York"},
    {"new york", "America/New_York"},
    {"los angeles", "America/Los_Angeles"},
    {"la", "America/Los_Angeles"},
    {"chicago", "America/Chicago"},
    {"denver", "America/Denver"},
    {"phoenix", "America/Phoenix"},
    {"honolulu", "Pacific/Honolulu"},
    {"dubai", "Asia/Dubai"},
    {"mumbai", "Asia/Kolkata"},
    {"delhi", "Asia/Kolkata"},
    {"singapore", "Asia/Singapore"},
    {"hong kong", "Asia/Hong_Kong"},
    {"shanghai", "Asia/Shanghai"},
    {"moscow", "Europe/Moscow"},
    {"cairo", "Africa/Cairo"},
    {"johannesburg", "Africa/Johannesburg"},
    {"sao paulo", "America/Sao_Paulo"},
    {"mexico city", "America/Mexico_City"},
    {"toronto", "America/Toronto"},
    {"vancouver", "America/Vancouver"},
    {"auckland", "Pacific/Auckland"},
};

static void trim_cpy(const char *in, char *out, size_t cap) {
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

static int resolve_tz_name(const char *place, char *buf, size_t bufsz) {
  char key[96];
  trim_cpy(place, key, sizeof(key));
  if (key[0] == '\0')
    return 0;
  if (strchr(key, '/')) {
    if (strlen(key) >= bufsz)
      return 0;
    snprintf(buf, bufsz, "%s", key);
    return 1;
  }
  for (size_t i = 0; i < sizeof(TZ_ALIASES) / sizeof(TZ_ALIASES[0]); i++) {
    if (strcasecmp(key, TZ_ALIASES[i].alias) == 0) {
      snprintf(buf, bufsz, "%s", TZ_ALIASES[i].iana);
      return 1;
    }
  }
  return 0;
}

static int parse_clock(const char *s, int *hh, int *mm) {
  char buf[128];
  trim_cpy(s, buf, sizeof(buf));
  int h = 0, m = 0;
  char ap[8] = "";
  if (sscanf(buf, "%d:%d%7s", &h, &m, ap) >= 3) {
  } else if (sscanf(buf, "%d:%d", &h, &m) == 2) {
    ap[0] = '\0';
  } else if (sscanf(buf, "%d%7s", &h, ap) == 2) {
    m = 0;
  } else {
    return 0;
  }
  if (strcasecmp(ap, "pm") == 0) {
    if (h < 12)
      h += 12;
  } else if (strcasecmp(ap, "am") == 0) {
    if (h == 12)
      h = 0;
  } else if (ap[0] != '\0') {
    return 0;
  }
  if (h < 0 || h > 23 || m < 0 || m > 59)
    return 0;
  *hh = h;
  *mm = m;
  return 1;
}

static int parse_tail_place(const char *left_in, char *place, size_t placecap,
                            char *timebuf, size_t timecap) {
  char left[256];
  trim_cpy(left_in, left, sizeof(left));
  const char *p = left + strlen(left);
  while (p > left && isspace((unsigned char)p[-1]))
    p--;
  const char *end = p;
  while (p > left && !isspace((unsigned char)p[-1]))
    p--;
  char one[96];
  size_t wl = (size_t)(end - p);
  if (wl >= sizeof(one))
    return 0;
  memcpy(one, p, wl);
  one[wl] = '\0';
  trim_cpy(one, one, sizeof(one));
  char iana[128];
  if (resolve_tz_name(one, iana, sizeof(iana))) {
    size_t tp = (size_t)(p - left);
    while (tp > 0 && isspace((unsigned char)left[tp - 1]))
      tp--;
    if (tp >= timecap)
      return 0;
    memcpy(timebuf, left, tp);
    timebuf[tp] = '\0';
    trim_cpy(timebuf, timebuf, timecap);
    snprintf(place, placecap, "%s", one);
    return 1;
  }
  if (p <= left)
    return 0;
  p--;
  while (p > left && isspace((unsigned char)p[-1]))
    p--;
  end = p + 1;
  while (p > left && !isspace((unsigned char)p[-1]))
    p--;
  wl = (size_t)(end - p);
  if (wl >= sizeof(one))
    return 0;
  memcpy(one, p, wl);
  one[wl] = '\0';
  trim_cpy(one, one, sizeof(one));
  if (!resolve_tz_name(one, iana, sizeof(iana)))
    return 0;
  size_t tp = (size_t)(p - left);
  while (tp > 0 && isspace((unsigned char)left[tp - 1]))
    tp--;
  if (tp >= timecap)
    return 0;
  memcpy(timebuf, left, tp);
  timebuf[tp] = '\0';
  trim_cpy(timebuf, timebuf, timecap);
  snprintf(place, placecap, "%s", one);
  return 1;
}

static int parse_when_in(const char *s, char *place, char *timebuf, size_t pcap,
                         size_t tcap) {
  const char *in = strcasestr(s, " in ");
  if (!in)
    return 0;
  size_t tl = (size_t)(in - s);
  if (tl >= tcap)
    return 0;
  memcpy(timebuf, s, tl);
  timebuf[tl] = '\0';
  trim_cpy(timebuf, timebuf, tcap);
  trim_cpy(in + 4, place, pcap);
  return place[0] && timebuf[0];
}

static int try_parse_to_form(const char *q, char *from_i, char *to_i,
                             char *date_iso, int *hh, int *mm) {
  const char *sep = strcasestr(q, " to ");
  if (!sep)
    return 0;
  char left[256], right[128];
  size_t ll = (size_t)(sep - q);
  if (ll >= sizeof(left))
    return 0;
  memcpy(left, q, ll);
  left[ll] = '\0';
  trim_cpy(sep + 4, right, sizeof(right));
  char from_place[96], time_part[128];
  if (!parse_tail_place(left, from_place, sizeof(from_place), time_part,
                         sizeof(time_part)))
    return 0;
  if (!resolve_tz_name(right, to_i, 128))
    return 0;
  if (!resolve_tz_name(from_place, from_i, 128))
    return 0;
  if (!parse_clock(time_part, hh, mm))
    return 0;
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(date_iso, 32, "%Y-%m-%d", &tm);
  return 1;
}

static int try_parse_when_form(const char *q, char *from_i, char *to_i,
                               char *date_iso, int *hh, int *mm) {
  const char *ti = strcasestr(q, "time in ");
  const char *wh = strcasestr(q, " when ");
  if (!ti || !wh || wh <= ti)
    return 0;
  char to_place[96];
  size_t len = (size_t)(wh - (ti + 8));
  if (len >= sizeof(to_place))
    return 0;
  memcpy(to_place, ti + 8, len);
  to_place[len] = '\0';
  trim_cpy(to_place, to_place, sizeof(to_place));
  char time_part[128], from_place[96];
  if (!parse_when_in(wh + 6, from_place, time_part, sizeof(from_place),
                     sizeof(time_part)))
    return 0;
  if (!resolve_tz_name(to_place, to_i, 128))
    return 0;
  if (!resolve_tz_name(from_place, from_i, 128))
    return 0;
  if (!parse_clock(time_part, hh, mm))
    return 0;
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(date_iso, 32, "%Y-%m-%d", &tm);
  return 1;
}

int is_meeting_planner_query(const char *query) {
  if (!query)
    return 0;
  char q[512];
  trim_cpy(query, q, sizeof(q));
  char fi[128], ti[128], ds[32];
  int h, m;
  if (try_parse_to_form(q, fi, ti, ds, &h, &m))
    return 1;
  if (try_parse_when_form(q, fi, ti, ds, &h, &m))
    return 1;
  return 0;
}

static char *wrap_meeting(const char *from_i, const char *to_i,
                          const char *date_iso, int hh, int mm,
                          const char *fallback) {
  char ef[128], et[128], ed[32], eh[8], em[8], en[512];
  html_escape_attr(from_i, ef, sizeof(ef));
  html_escape_attr(to_i, et, sizeof(et));
  html_escape_attr(date_iso, ed, sizeof(ed));
  snprintf(eh, sizeof(eh), "%d", hh);
  snprintf(em, sizeof(em), "%d", mm);
  html_escape_text(fallback, en, sizeof(en));
  char *html = malloc(2048);
  if (!html)
    return NULL;
  snprintf(html, 2048,
           "<div class=\"widget widget-meeting\" data-widget=\"meeting\" "
           "data-from-iana=\"%s\" data-to-iana=\"%s\" data-date=\"%s\" "
           "data-hour=\"%s\" data-minute=\"%s\">"
           "<div class=\"widget-meeting-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           ef, et, ed, eh, em, en);
  return html;
}

InfoBox fetch_meeting_planner_data(char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  char q[512];
  trim_cpy(query, q, sizeof(q));
  char fi[128], ti[128], ds[32];
  int h = 0, m = 0;
  if (!try_parse_to_form(q, fi, ti, ds, &h, &m) &&
      !try_parse_when_form(q, fi, ti, ds, &h, &m)) {
    return info;
  }

  char fb[256];
  snprintf(fb, sizeof(fb), "When it is %02d:%02d on %s in the first zone, "
                           "see converted time in the second (enable JavaScript).",
           h, m, ds);
  char *html = wrap_meeting(fi, ti, ds, h, m, fb);
  if (!html)
    return info;
  info.title = strdup("Time zones");
  info.extract = html;
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");
  return info;
}
