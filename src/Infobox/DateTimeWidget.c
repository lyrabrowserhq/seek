#define _GNU_SOURCE 1

#include "DateTimeWidget.h"
#include "MeetingPlanner.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static pthread_mutex_t g_tz_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static int eq_ci(const char *a, const char *b) {
  return strcasecmp(a, b) == 0;
}

static int starts_ci(const char *s, const char *p) {
  size_t n = strlen(p);
  return strncasecmp(s, p, n) == 0;
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

static char *wrap_datetime(const char *preset, const char *iana,
                            const char *date_a, const char *date_b,
                            const char *fallback_plain) {
  char ep[64], ei[180], ea[96], eb[96], ef[512];
  html_escape_attr(preset, ep, sizeof(ep));
  html_escape_attr(iana ? iana : "", ei, sizeof(ei));
  html_escape_attr(date_a ? date_a : "", ea, sizeof(ea));
  html_escape_attr(date_b ? date_b : "", eb, sizeof(eb));
  html_escape_text(fallback_plain, ef, sizeof(ef));
  char *html = malloc(2048);
  if (!html)
    return NULL;
  snprintf(html, 2048,
           "<div class=\"widget widget-datetime\" data-widget=\"datetime\" "
           "data-preset=\"%s\" data-iana=\"%s\" data-date-a=\"%s\" "
           "data-date-b=\"%s\">"
           "<div class=\"widget-datetime-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           ep, ei, ea, eb, ef);
  return html;
}

static void format_in_tz(const char *iana, char *buf, size_t bufsize) {
  if (!iana || !buf || bufsize == 0)
    return;
  pthread_mutex_lock(&g_tz_mutex);
  char prev[256];
  prev[0] = '\0';
  const char *was = getenv("TZ");
  if (was)
    snprintf(prev, sizeof(prev), "%s", was);
  setenv("TZ", iana, 1);
  tzset();
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(buf, bufsize, "%A, %Y-%m-%d %H:%M:%S %Z", &tm);
  if (prev[0])
    setenv("TZ", prev, 1);
  else
    unsetenv("TZ");
  tzset();
  pthread_mutex_unlock(&g_tz_mutex);
}

static int parse_iso(const char *s, struct tm *out) {
  memset(out, 0, sizeof(*out));
  char *r = strptime(s, "%Y-%m-%d", out);
  if (r && *r == '\0')
    return 1;
  memset(out, 0, sizeof(*out));
  r = strptime(s, "%d/%m/%Y", out);
  if (r && *r == '\0')
    return 1;
  memset(out, 0, sizeof(*out));
  r = strptime(s, "%m/%d/%Y", out);
  if (r && *r == '\0')
    return 1;
  return 0;
}

static int parse_iso_loose(const char *s, struct tm *out) {
  char buf[64];
  trim_cpy(s, buf, sizeof(buf));
  return parse_iso(buf, out);
}

static time_t tm_to_time(struct tm *tm) {
  tm->tm_isdst = -1;
  return mktime(tm);
}

static int extract_between_dates(const char *q, char *a, char *b, size_t alen,
                                 size_t blen) {
  const char *p = strcasestr(q, "between");
  if (!p)
    return 0;
  p += 7;
  while (*p && isspace((unsigned char)*p))
    p++;
  const char *andw = strcasestr(p, " and ");
  if (!andw)
    return 0;
  size_t la = (size_t)(andw - p);
  while (la > 0 && isspace((unsigned char)p[la - 1]))
    la--;
  if (la >= alen)
    la = alen - 1;
  memcpy(a, p, la);
  a[la] = '\0';
  const char *r = andw + 5;
  while (*r && isspace((unsigned char)*r))
    r++;
  size_t lb = strlen(r);
  while (lb > 0 && isspace((unsigned char)r[lb - 1]))
    lb--;
  if (lb >= blen)
    lb = blen - 1;
  memcpy(b, r, lb);
  b[lb] = '\0';
  return a[0] && b[0];
}

static int extract_two_dates_to(const char *q, char *a, char *b, size_t alen,
                                size_t blen) {
  const char *p = strcasestr(q, " to ");
  if (!p)
    return 0;
  char left[64], right[64];
  size_t ll = (size_t)(p - q);
  if (ll >= sizeof(left))
    return 0;
  memcpy(left, q, ll);
  left[ll] = '\0';
  trim_cpy(left, left, sizeof(left));
  trim_cpy(p + 4, right, sizeof(right));
  struct tm tma, tmb;
  if (!parse_iso_loose(left, &tma) || !parse_iso_loose(right, &tmb))
    return 0;
  snprintf(a, alen, "%s", left);
  snprintf(b, blen, "%s", right);
  return 1;
}

static int extract_days_until(const char *q, char *out, size_t outsz) {
  const char *keys[] = {"days until", "day until", "how many days until",
                        "days till",  NULL};
  for (int i = 0; keys[i]; i++) {
    const char *p = strcasestr(q, keys[i]);
    if (!p)
      continue;
    p += strlen(keys[i]);
    while (*p && (isspace((unsigned char)*p) || *p == ':'))
      p++;
    trim_cpy(p, out, outsz);
    if (out[0])
      return 1;
  }
  return 0;
}

int is_datetime_widget_query(const char *query) {
  if (!query)
    return 0;
  if (is_meeting_planner_query(query))
    return 0;
  char q[512];
  trim_cpy(query, q, sizeof(q));
  if (q[0] == '\0')
    return 0;

  if (eq_ci(q, "time") || eq_ci(q, "what time") || eq_ci(q, "what time?") ||
      eq_ci(q, "what time is it") || eq_ci(q, "what time is it?") ||
      eq_ci(q, "current time") || eq_ci(q, "local time") ||
      eq_ci(q, "time now"))
    return 1;
  if (eq_ci(q, "utc") || eq_ci(q, "utc time") || eq_ci(q, "time utc") ||
      eq_ci(q, "gmt") || eq_ci(q, "gmt time") || eq_ci(q, "gmt now"))
    return 1;
  if (eq_ci(q, "unix time") || eq_ci(q, "unix timestamp") ||
      eq_ci(q, "epoch") || eq_ci(q, "epoch time") || eq_ci(q, "unix epoch"))
    return 1;
  if (eq_ci(q, "today") || eq_ci(q, "today's date") || eq_ci(q, "date today") ||
      eq_ci(q, "what is today's date") || eq_ci(q, "what date is it"))
    return 1;
  if (eq_ci(q, "date") || eq_ci(q, "date calc") || eq_ci(q, "date calculator"))
    return 1;

  if (starts_ci(q, "time in ")) {
    char iana[128];
    if (resolve_tz_name(q + 8, iana, sizeof(iana)))
      return 1;
  }

  if (strcasestr(q, "days between") && strcasestr(q, " and ")) {
    char a[64], b[64];
    if (extract_between_dates(q, a, b, sizeof(a), sizeof(b))) {
      struct tm ta, tb;
      if (parse_iso_loose(a, &ta) && parse_iso_loose(b, &tb))
        return 1;
    }
  }

  char du[64];
  if (extract_days_until(q, du, sizeof(du))) {
    struct tm t;
    if (parse_iso_loose(du, &t))
      return 1;
  }

  char da[64], db[64];
  if (extract_two_dates_to(q, da, db, sizeof(da), sizeof(db)))
    return 1;

  return 0;
}

static char *build_html_date_tools(void) {
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  char line[160];
  strftime(line, sizeof(line), "%A, %Y-%m-%d %H:%M:%S %Z", &tm);
  char fb[384];
  snprintf(fb, sizeof(fb), "Date and time: %s", line);
  return wrap_datetime("datecalc", "", "", "", fb);
}

static char *build_html_local(void) {
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  char line[128];
  strftime(line, sizeof(line), "%A, %Y-%m-%d %H:%M:%S %Z", &tm);
  char fb[256];
  snprintf(fb, sizeof(fb), "Local time: %s", line);
  return wrap_datetime("local", "", "", "", fb);
}

static char *build_html_utc(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  char line[128];
  strftime(line, sizeof(line), "%A, %Y-%m-%d %H:%M:%S UTC", &tm);
  char fb[256];
  snprintf(fb, sizeof(fb), "UTC: %s", line);
  return wrap_datetime("utc", "", "", "", fb);
}

static char *build_html_unix(void) {
  time_t t = time(NULL);
  char fb[128];
  snprintf(fb, sizeof(fb), "Unix time: %lld", (long long)t);
  return wrap_datetime("unix", "", "", "", fb);
}

static char *build_html_today(void) {
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  char line[128];
  strftime(line, sizeof(line), "%A, %B %d, %Y", &tm);
  char fb[256];
  snprintf(fb, sizeof(fb), "Today: %s", line);
  return wrap_datetime("today", "", "", "", fb);
}

static char *build_html_tz_place(const char *place) {
  char rest[128];
  trim_cpy(place, rest, sizeof(rest));
  char iana[128];
  if (!resolve_tz_name(rest, iana, sizeof(iana)))
    return NULL;
  char buf[256];
  format_in_tz(iana, buf, sizeof(buf));
  char fb[512];
  snprintf(fb, sizeof(fb), "Time in %s: %s", rest, buf);
  return wrap_datetime("tz", iana, "", "", fb);
}

static char *build_html_days_between(const char *q) {
  char a[64], b[64];
  if (!extract_between_dates(q, a, b, sizeof(a), sizeof(b)))
    return NULL;
  struct tm ta, tb;
  if (!parse_iso_loose(a, &ta) || !parse_iso_loose(b, &tb))
    return NULL;
  time_t t1 = tm_to_time(&ta);
  time_t t2 = tm_to_time(&tb);
  double days = difftime(t2, t1) / 86400.0;
  char fb[384];
  snprintf(fb, sizeof(fb), "Days between %s and %s: %.0f days", a, b,
           days >= 0 ? days : -days);
  return wrap_datetime("between", "", a, b, fb);
}

static char *build_html_two_dates_to(const char *q) {
  char da[64], db[64];
  if (!extract_two_dates_to(q, da, db, sizeof(da), sizeof(db)))
    return NULL;
  struct tm ta, tb;
  if (!parse_iso_loose(da, &ta) || !parse_iso_loose(db, &tb))
    return NULL;
  time_t t1 = tm_to_time(&ta);
  time_t t2 = tm_to_time(&tb);
  double days = difftime(t2, t1) / 86400.0;
  char fb[384];
  snprintf(fb, sizeof(fb), "From %s to %s: %.0f days", da, db,
           days >= 0 ? days : -days);
  return wrap_datetime("span", "", da, db, fb);
}

static char *build_html_days_until(const char *q) {
  char ds[64];
  if (!extract_days_until(q, ds, sizeof(ds)))
    return NULL;
  struct tm target;
  if (!parse_iso_loose(ds, &target))
    return NULL;
  time_t tt = tm_to_time(&target);
  time_t now = time(NULL);
  struct tm today;
  localtime_r(&now, &today);
  today.tm_hour = 0;
  today.tm_min = 0;
  today.tm_sec = 0;
  time_t t0 = mktime(&today);
  double days = difftime(tt, t0) / 86400.0;
  char fb[256];
  snprintf(fb, sizeof(fb), "Days until %s: %.0f days", ds,
           days >= 0 ? days : -days);
  return wrap_datetime("until", "", ds, "", fb);
}

InfoBox fetch_datetime_widget_data(char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  char q[512];
  trim_cpy(query, q, sizeof(q));

  char *html = NULL;

  if (eq_ci(q, "date") || eq_ci(q, "date calc") || eq_ci(q, "date calculator"))
    html = build_html_date_tools();
  if (!html && starts_ci(q, "time in "))
    html = build_html_tz_place(q + 8);
  if (!html && strcasestr(q, "days between") && strcasestr(q, " and "))
    html = build_html_days_between(q);
  if (!html) {
    char tda[64], tdb[64];
    if (extract_two_dates_to(q, tda, tdb, sizeof(tda), sizeof(tdb)))
      html = build_html_two_dates_to(q);
  }
  if (!html) {
    char du[64];
    if (extract_days_until(q, du, sizeof(du)))
      html = build_html_days_until(q);
  }
  if (!html && (eq_ci(q, "utc") || eq_ci(q, "utc time") || eq_ci(q, "time utc") ||
                eq_ci(q, "gmt") || eq_ci(q, "gmt time") || eq_ci(q, "gmt now")))
    html = build_html_utc();
  if (!html && (eq_ci(q, "unix time") || eq_ci(q, "unix timestamp") ||
                eq_ci(q, "epoch") || eq_ci(q, "epoch time") ||
                eq_ci(q, "unix epoch")))
    html = build_html_unix();
  if (!html && (eq_ci(q, "today") || eq_ci(q, "today's date") ||
                eq_ci(q, "date today") || eq_ci(q, "what is today's date") ||
                eq_ci(q, "what date is it")))
    html = build_html_today();
  if (!html && (eq_ci(q, "time") || eq_ci(q, "what time") || eq_ci(q, "what time?") ||
                eq_ci(q, "what time is it") || eq_ci(q, "what time is it?") ||
                eq_ci(q, "current time") || eq_ci(q, "local time") ||
                eq_ci(q, "time now")))
    html = build_html_local();

  if (!html)
    return info;

  int date_kw = eq_ci(q, "date") || eq_ci(q, "date calc") || eq_ci(q, "date calculator");
  info.title = strdup(date_kw ? "Date and time" : "Time and date");
  info.extract = html;
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");
  return info;
}
