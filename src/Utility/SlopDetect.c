#include "SlopDetect.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TELL_WORDS[] = {
    "delve",       "delves",      "delved",       "delving",
    "tapestry",    "tapestries",  "testament",    "pivotal",
    "intricate",   "intricacies", "meticulous",   "meticulously",
    "multifaceted","myriad",      "plethora",     "bolster",
    "bolstered",   "bolstering",  "seamless",     "seamlessly",
    "leverage",    "leveraging",  "interplay",    NULL};

static const char *P_UNDUE[] = {
    "stands as a testament",
    "serves as a testament",
    "a testament to",
    "plays a pivotal role",
    "plays a crucial role",
    "plays a vital role",
    "plays a key role",
    "plays a significant role",
    "plays a central role",
    "plays a critical role",
    "cannot be overstated",
    "underscore the importance",
    "underscores the importance",
    "highlight the importance",
    "highlights the importance",
    NULL};

static const char *P_VAGUE[] = {
    "experts say",
    "expert says",
    "experts believe",
    "experts agree",
    "experts suggest",
    "studies show",
    "studies suggest",
    "studies indicate",
    "studies have shown",
    "research shows",
    "research suggests",
    "research indicates",
    "research has shown",
    "it is widely believed",
    "it is commonly known",
    "it is generally accepted",
    "many argue",
    "many believe",
    "many claim",
    "many suggest",
    NULL};

static const char *P_CLOSE[] = {
    "in conclusion", "in summary", "to summarize", "to sum up",
    "at the end of the day", "the bottom line is", NULL};

static const char *P_CHAT[] = {
    "great question",
    "that's a great question",
    "i hope this helps",
    "as an ai",
    "as a large language model",
    "feel free to reach out",
    "feel free to ask",
    "feel free to let me know",
    "here is a breakdown",
    "here's a breakdown",
    "here is a summary",
    "here is a list",
    NULL};

static const char *P_HEDGE[] = {
    "it is important to note",
    "it's worth noting",
    "it's worth mentioning",
    "it should be noted",
    "it's important to note",
    "it's important to remember",
    "it's important to understand",
    "needless to say",
    "generally speaking",
    NULL};

static const char *P_THROAT[] = {
    "in the modern world",
    "in the digital age",
    "in the contemporary era",
    "in the modern age",
    "in the digital world",
    "as we navigate",
    "as we move forward",
    "as we look to the future",
    "the realm of",
    NULL};

static const char *P_INFLATE[] = {
    "delve into",
    "delves into",
    "delved into",
    "delving into",
    "delve deep into",
    "a myriad of",
    "a plethora of",
    "a rich tapestry of",
    "paradigm shift",
    "game-changer",
    "game-changing",
    "ever-evolving",
    "shape the future",
    "shaping the future",
    "shapes the future",
    NULL};

static const char *P_BALANCE[] = {
    "on one hand",
    "on the one hand",
    "a balanced approach",
    "a balanced perspective",
    "a balanced view",
    "it's a nuanced issue",
    "it's a complex topic",
    "it's a multifaceted issue",
    "depends on various factors",
    "depends on several factors",
    "depends on many factors",
    "striking a balance",
    NULL};

static const char *P_TAIL[] = {
    ", highlighting its",
    ", highlighting the",
    ", highlighting their",
    ", underscoring its",
    ", underscoring the",
    ", underscoring their",
    ", showcasing its",
    ", showcasing the",
    ", showcasing their",
    ", paving the way",
    ", setting the stage",
    NULL};

static const char *P_LIST[] = {
    "here are the key points",
    "here are some key points",
    "here are a few points",
    "here are the steps",
    "here are some ways",
    "here are a few reasons",
    "key features include",
    "key factors include",
    "important aspects are",
    "the following are",
    "the following is",
    "the following include",
    NULL};

static int ci_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static const char *find_ci(const char *hay, const char *needle) {
  if (!hay || !needle || !*needle)
    return NULL;
  size_t n = strlen(needle);
  for (const char *p = hay; *p; p++) {
    size_t i = 0;
    while (i < n && p[i] &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
      i++;
    if (i == n)
      return p;
  }
  return NULL;
}

static int count_lit(const char *hay, const char *needle) {
  int n = 0;
  const char *p = hay;
  size_t nl = strlen(needle);
  while ((p = find_ci(p, needle))) {
    n++;
    p += nl;
  }
  return n;
}

static int count_list(const char *hay, const char **list) {
  int n = 0;
  for (int i = 0; list[i]; i++)
    n += count_lit(hay, list[i]);
  return n;
}

static int is_word_char(unsigned char c) {
  return isalnum(c) || c == '\'';
}

static int count_span(const char *hay, const char **left, int nleft,
                      const char *right, int maxgap) {
  int n = 0;
  for (int i = 0; i < nleft; i++) {
    const char *p = hay;
    size_t ll = strlen(left[i]);
    while ((p = find_ci(p, left[i]))) {
      const char *q = p + ll;
      int gap = 0;
      while (*q && gap < maxgap) {
        if (find_ci(q, right) == q) {
          n++;
          break;
        }
        q++;
        gap++;
      }
      p += ll;
    }
  }
  return n;
}

static int count_tell_words(const char *text) {
  int n = 0;
  const char *p = text;
  char tok[64];
  while (*p) {
    while (*p && !is_word_char((unsigned char)*p))
      p++;
    if (!*p)
      break;
    int i = 0;
    while (*p && is_word_char((unsigned char)*p) && i < 63) {
      tok[i++] = (char)tolower((unsigned char)*p);
      p++;
    }
    tok[i] = '\0';
    for (int w = 0; TELL_WORDS[w]; w++) {
      if (ci_eq(tok, TELL_WORDS[w])) {
        n++;
        break;
      }
    }
  }
  return n;
}

static int word_count(const char *text) {
  int n = 0;
  int in = 0;
  for (const char *p = text; *p; p++) {
    if (is_word_char((unsigned char)*p)) {
      if (!in) {
        n++;
        in = 1;
      }
    } else {
      in = 0;
    }
  }
  return n;
}

static int words_in(const char *s, const char *e) {
  int n = 0;
  int in = 0;
  for (const char *p = s; p < e && *p; p++) {
    if (is_word_char((unsigned char)*p)) {
      if (!in) {
        n++;
        in = 1;
      }
    } else {
      in = 0;
    }
  }
  return n ? n : 1;
}

static double pstdev(const int *v, int n) {
  if (n < 2)
    return NAN;
  double mu = 0;
  for (int i = 0; i < n; i++)
    mu += v[i];
  mu /= n;
  double s = 0;
  for (int i = 0; i < n; i++) {
    double d = v[i] - mu;
    s += d * d;
  }
  return sqrt(s / n);
}

static void sentence_cv(const char *text, int *n_out, double *cv_out) {
  int lens[128];
  int n = 0;
  const char *start = text;
  for (const char *p = text; ; p++) {
    if (*p == '.' || *p == '!' || *p == '?' || *p == '\0') {
      if (p > start) {
        while (start < p && (*start == ' ' || *start == '\n'))
          start++;
        if (p > start && n < 128)
          lens[n++] = words_in(start, p);
      }
      if (*p == '\0')
        break;
      start = p + 1;
    }
  }
  *n_out = n;
  if (n < 2) {
    *cv_out = NAN;
    return;
  }
  double mu = 0;
  for (int i = 0; i < n; i++)
    mu += lens[i];
  mu /= n;
  *cv_out = mu > 0 ? pstdev(lens, n) / mu : NAN;
}

static void paragraph_cv(const char *text, int *n_out, double *cv_out) {
  int lens[64];
  int n = 0;
  const char *start = text;
  const char *p = text;
  while (*p) {
    if (p[0] == '\n' && p[1] == '\n') {
      if (p > start && n < 64) {
        while (start < p && (*start == ' ' || *start == '\n'))
          start++;
        if (p > start)
          lens[n++] = words_in(start, p);
      }
      while (*p == '\n')
        p++;
      start = p;
      continue;
    }
    p++;
  }
  if (p > start && n < 64) {
    while (start < p && (*start == ' ' || *start == '\n'))
      start++;
    if (p > start)
      lens[n++] = words_in(start, p);
  }
  *n_out = n;
  if (n < 2) {
    *cv_out = NAN;
    return;
  }
  double mu = 0;
  for (int i = 0; i < n; i++)
    mu += lens[i];
  mu /= n;
  *cv_out = mu > 0 ? pstdev(lens, n) / mu : NAN;
}

static int is_list_line(const char *ln) {
  while (*ln == ' ' || *ln == '\t')
    ln++;
  if (*ln == '-' || *ln == '*' || *ln == '+') {
    ln++;
    return *ln == ' ' && ln[1] && ln[1] != ' ';
  }
  if (isdigit((unsigned char)*ln)) {
    while (isdigit((unsigned char)*ln))
      ln++;
    if (*ln == '.' || *ln == ')') {
      ln++;
      return *ln == ' ' && ln[1];
    }
  }
  return 0;
}

static void list_cv(const char *text, int *n_out, double *cv_out) {
  int lens[64];
  int n = 0;
  const char *p = text;
  while (*p && n < 64) {
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char buf[512];
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    if (is_list_line(buf))
      lens[n++] = (int)len;
    if (!e)
      break;
    p = e + 1;
  }
  *n_out = n;
  if (n < 2) {
    *cv_out = NAN;
    return;
  }
  double mu = 0;
  for (int i = 0; i < n; i++)
    mu += lens[i];
  mu /= n;
  *cv_out = mu > 0 ? pstdev(lens, n) / mu : NAN;
}

static int count_md_headings(const char *t) {
  int n = 0;
  const char *p = t;
  while (*p) {
    const char *s = p;
    while (*s == ' ' || *s == '\t')
      s++;
    if (*s == '#') {
      while (*s == '#')
        s++;
      if (*s == ' ' && s[1] && s[1] != '\n')
        n++;
    }
    p = strchr(p, '\n');
    if (!p)
      break;
    p++;
  }
  return n;
}

static int count_md_bold(const char *t) {
  int n = 0;
  const char *p = t;
  while ((p = strstr(p, "**"))) {
    const char *q = strstr(p + 2, "**");
    if (!q)
      break;
    if (q > p + 2 && (q - p) < 124)
      n++;
    p = q + 2;
  }
  return n;
}

static int count_md_links(const char *t) {
  int n = 0;
  const char *p = t;
  while ((p = strchr(p, '['))) {
    const char *rb = strchr(p, ']');
    if (!rb || rb - p > 120)
      break;
    if (rb[1] == '(') {
      const char *rp = strchr(rb + 2, ')');
      if (rp && rp - (rb + 2) < 200)
        n++;
    }
    p = rb + 1;
  }
  return n;
}

static int count_bold_colon(const char *t) {
  int n = 0;
  const char *p = t;
  while ((p = strstr(p, "**"))) {
    const char *q = strstr(p + 2, "**");
    if (!q)
      break;
    const char *s = q + 2;
    while (*s == ' ' || *s == '\t')
      s++;
    if (*s == ':')
      n++;
    p = q + 2;
  }
  return n;
}

static int today_world(const char *t) {
  int n = 0;
  const char *p = t;
  while ((p = find_ci(p, "in today's "))) {
    const char *q = p + 11;
    int i = 0;
    char buf[48];
    while (*q && i < 40 && *q != '.' && *q != '\n') {
      buf[i++] = (char)tolower((unsigned char)*q);
      q++;
    }
    buf[i] = '\0';
    if (strstr(buf, "world") || strstr(buf, "age") || strstr(buf, "landscape") ||
        strstr(buf, "era"))
      n++;
    p += 11;
  }
  return n;
}

static int em_dash_count(const char *t) {
  int n = 0;
  for (const char *p = t; *p; p++) {
    if ((unsigned char)p[0] == 0xe2 && (unsigned char)p[1] == 0x80 &&
        (unsigned char)p[2] == 0x94) {
      n++;
      p += 2;
    }
  }
  const char *q = t;
  while ((q = strstr(q, " -- "))) {
    n++;
    q += 4;
  }
  return n;
}

static int has_emdash(const char *t) {
  if (!t)
    return 0;
  for (const char *p = t; *p; p++) {
    if ((unsigned char)p[0] == 0xe2 && (unsigned char)p[1] == 0x80 &&
        (unsigned char)p[2] == 0x94)
      return 1;
  }
  return 0;
}

static double clamp(double v, double lo, double hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

double slop_detect_page(const char *title, const char *snippet,
                        const char *text) {
  const char *ti = title ? title : "";
  const char *sn = snippet ? snippet : "";
  const char *tx = text ? text : "";
  char body[8192];
  snprintf(body, sizeof(body), "%s\n%s\n%s", ti, sn, tx);

  int words = word_count(body);
  if (words < 1)
    words = 1;

  int undue = count_list(body, P_UNDUE);
  int vague = count_list(body, P_VAGUE);
  int close = count_list(body, P_CLOSE);
  int chat = count_list(body, P_CHAT);
  int hedge = count_list(body, P_HEDGE);
  int throat = count_list(body, P_THROAT) + today_world(body);
  int inflate = count_list(body, P_INFLATE);
  int balance = count_list(body, P_BALANCE);
  int tails = count_list(body, P_TAIL);
  int lists = count_list(body, P_LIST);
  int tells = count_tell_words(body);

  static const char *neg_left[] = {"not just", "not only", "not merely",
                                   "not simply", "not solely"};
  int neg_par = count_span(body, neg_left, 5, "but", 120);
  static const char *wh_left[] = {"whether it's", "whether it is",
                                  "whether you're", "whether you are"};
  int whether = count_span(body, wh_left, 4, "or", 80);

  int bold_colon = count_bold_colon(body);
  int md_h = count_md_headings(body);
  int md_b = count_md_bold(body);
  int md_l = count_md_links(body);

  int n_sent = 0, n_para = 0, n_li = 0;
  double sent_cv = NAN, para_cv = NAN, li_cv = NAN;
  sentence_cv(body, &n_sent, &sent_cv);
  paragraph_cv(body, &n_para, &para_cv);
  list_cv(body, &n_li, &li_cv);

  int em_n = em_dash_count(body);
  double em_per_k = em_n * 1000.0 / words;
  int title_em = has_emdash(ti);
  int desc_em = has_emdash(sn);

  double seo_t = title_em ? 3.2 : 0;
  double seo_m = desc_em ? 2.0 : 0;
  double uni_p =
      (n_para >= 3 && !isnan(para_cv) && para_cv <= 0.2) ? 2.5 : 0;
  double uni_s =
      (n_sent >= 5 && !isnan(sent_cv) && sent_cv <= 0.3) ? 2.0 : 0;
  double uni_l = (n_li >= 3 && !isnan(li_cv) && li_cv <= 0.2) ? 1.5 : 0;
  double em_d = em_per_k >= 7 ? 1.5 : (em_per_k >= 4 ? 0.75 : 0);

  double raw = seo_t + seo_m + uni_p + uni_s + uni_l + em_d;
  raw += clamp(bold_colon, 0, 4) * 0.8;
  raw += clamp(neg_par, 0, 3) * 0.9;
  raw += clamp(whether, 0, 2) * 0.4;
  raw += clamp(undue, 0, 4) * 0.9;
  raw += clamp(vague, 0, 3) * 0.9;
  raw += clamp(close, 0, 2) * 1.0;
  raw += clamp(chat, 0, 3) * 1.5;
  raw += clamp(hedge, 0, 4) * 0.5;
  raw += clamp(throat, 0, 3) * 0.9;
  raw += clamp(inflate, 0, 5) * 0.6;
  raw += clamp(balance, 0, 2) * 0.7;
  raw += clamp(tails, 0, 3) * 0.9;
  raw += clamp(lists, 0, 2) * 0.9;
  raw += clamp(md_h + md_b + md_l, 0, 4) * 0.4;
  raw += clamp(tells, 0, 5) * 0.3;

  double punct = seo_t + seo_m + em_d;
  double other = raw - punct;
  if (other < 1.5)
    raw *= 0.2;

  double score = raw > 0 ? raw / (raw + 8.0) : 0;
  if (word_count(body) < 12 && !title_em && !desc_em)
    score = 0;
  if (score < 0)
    score = 0;
  if (score > 1)
    score = 1;
  return floor(score * 1000.0 + 0.5) / 1000.0;
}
