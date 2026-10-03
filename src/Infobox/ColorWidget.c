#include "ColorWidget.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

static int hex_val(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  if (c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  return -1;
}

static int parse_hex_color(const char *s, int *r, int *g, int *b) {
  if (!s || *s != '#')
    return 0;
  s++;
  size_t n = 0;
  while (isxdigit((unsigned char)s[n]))
    n++;
  if (n != 3 && n != 6 && n != 8)
    return 0;
  if (n == 3) {
    int a = hex_val(s[0]), b2 = hex_val(s[1]), c = hex_val(s[2]);
    if (a < 0 || b2 < 0 || c < 0)
      return 0;
    *r = a * 17;
    *g = b2 * 17;
    *b = c * 17;
    return 1;
  }
  if (n == 6 || n == 8) {
    int v[6];
    for (int i = 0; i < 6; i++) {
      v[i] = hex_val(s[i]);
      if (v[i] < 0)
        return 0;
    }
    *r = (v[0] << 4) | v[1];
    *g = (v[2] << 4) | v[3];
    *b = (v[4] << 4) | v[5];
    return 1;
  }
  return 0;
}

static int scan_rgb(const char *q, int *r, int *g, int *b) {
  const char *p = strcasestr(q, "rgb");
  if (!p)
    return 0;
  p += 3;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != '(')
    return 0;
  p++;
  int rv, gv, bv;
  if (sscanf(p, "%d , %d , %d", &rv, &gv, &bv) == 3 ||
      sscanf(p, "%d,%d,%d", &rv, &gv, &bv) == 3) {
    if (rv >= 0 && rv <= 255 && gv >= 0 && gv <= 255 && bv >= 0 && bv <= 255) {
      *r = rv;
      *g = gv;
      *b = bv;
      return 1;
    }
  }
  return 0;
}

static double hue2rgb(double p, double q, double t) {
  if (t < 0)
    t += 1;
  if (t > 1)
    t -= 1;
  if (t < 1.0 / 6)
    return p + (q - p) * 6 * t;
  if (t < 0.5)
    return q;
  if (t < 2.0 / 3)
    return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

static void hsl_to_rgb(double h, double s, double l, int *r, int *g, int *b) {
  if (s <= 0) {
    int v = (int)round(l * 255);
    if (v < 0)
      v = 0;
    if (v > 255)
      v = 255;
    *r = *g = *b = v;
    return;
  }
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  double p = 2 * l - q;
  double hk = h / 360.0;
  double R = hue2rgb(p, q, hk + 1.0 / 3);
  double G = hue2rgb(p, q, hk);
  double B = hue2rgb(p, q, hk - 1.0 / 3);
  *r = (int)round(R * 255);
  *g = (int)round(G * 255);
  *b = (int)round(B * 255);
  if (*r < 0)
    *r = 0;
  if (*r > 255)
    *r = 255;
  if (*g < 0)
    *g = 0;
  if (*g > 255)
    *g = 255;
  if (*b < 0)
    *b = 0;
  if (*b > 255)
    *b = 255;
}

static int scan_hsl(const char *q, int *r, int *g, int *b) {
  const char *p = strcasestr(q, "hsl");
  if (!p)
    return 0;
  p += 3;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != '(')
    return 0;
  p++;
  double h, s, l;
  if (sscanf(p, "%lf , %lf %% , %lf %%", &h, &s, &l) == 3 ||
      sscanf(p, "%lf,%lf%%,%lf%%", &h, &s, &l) == 3 ||
      sscanf(p, "%lf , %lf%% , %lf%%", &h, &s, &l) == 3) {
    s /= 100.0;
    l /= 100.0;
    hsl_to_rgb(h, s, l, r, g, b);
    return 1;
  }
  return 0;
}

static int extract_hex_from_query(const char *q, char *hexout, size_t hexcap) {
  const char *p = strchr(q, '#');
  if (!p)
    return 0;
  size_t k = 0;
  const char *s = p + 1;
  while (isxdigit((unsigned char)s[k]) && k < 16)
    k++;
  if (k != 3 && k != 6 && k != 8)
    return 0;
  if (hexcap < k + 2)
    return 0;
  hexout[0] = '#';
  memcpy(hexout + 1, s, k);
  hexout[k + 1] = '\0';
  return 1;
}

int is_color_widget_query(const char *query) {
  if (!query)
    return 0;
  char q[512];
  trim_cpy(query, q, sizeof(q));
  if (strcasestr(q, "rgb("))
    return 1;
  if (strcasestr(q, "hsl("))
    return 1;
  if (q[0] == '#') {
    char hex[16];
    if (extract_hex_from_query(q, hex, sizeof(hex))) {
      int r, g, b;
      return parse_hex_color(hex, &r, &g, &b);
    }
  }
  {
    const char *cw = strcasestr(q, "color ");
    const char *hash = strchr(q, '#');
    if (cw && hash && hash > cw) {
      char hex[16];
      if (extract_hex_from_query(hash, hex, sizeof(hex))) {
        int r, g, b;
        return parse_hex_color(hex, &r, &g, &b);
      }
    }
  }
  return 0;
}

static void rgb_to_hsl(int r, int g, int b, double *h, double *s, double *l) {
  double R = r / 255.0, G = g / 255.0, B = b / 255.0;
  double mx = fmax(R, fmax(G, B));
  double mn = fmin(R, fmin(G, B));
  double d = mx - mn;
  *l = (mx + mn) / 2;
  if (d < 1e-9) {
    *h = 0;
    *s = 0;
    return;
  }
  *s = *l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
  if (mx == R)
    *h = 60 * fmod((G - B) / d + 6.0, 6.0);
  else if (mx == G)
    *h = 60 * (((B - R) / d) + 2);
  else
    *h = 60 * (((R - G) / d) + 4);
}

static char *wrap_color(int r, int g, int b) {
  double h, s, l;
  rgb_to_hsl(r, g, b, &h, &s, &l);
  char hex[16];
  snprintf(hex, sizeof(hex), "#%02x%02x%02x", r, g, b);
  char eh[32], er[16], eg[16], eb[16], ef[512];
  html_escape_attr(hex, eh, sizeof(eh));
  snprintf(er, sizeof(er), "%d", r);
  snprintf(eg, sizeof(eg), "%d", g);
  snprintf(eb, sizeof(eb), "%d", b);
  char fb[512];
  snprintf(fb, sizeof(fb), "%s  RGB %d, %d, %d  HSL %.0f, %.1f%%, %.1f%%", hex, r, g, b,
           h, s * 100, l * 100);
  html_escape_text(fb, ef, sizeof(ef));

  char *html = malloc(2048);
  if (!html)
    return NULL;
  snprintf(html, 2048,
           "<div class=\"widget widget-color\" data-widget=\"color\" "
           "data-hex=\"%s\" data-r=\"%s\" data-g=\"%s\" data-b=\"%s\" "
           "data-h=\"%.6f\" data-s=\"%.6f\" data-l=\"%.6f\">"
           "<div class=\"widget-color-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           eh, er, eg, eb, h, s, l, ef);
  return html;
}

InfoBox fetch_color_widget_data(char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  char q[512];
  trim_cpy(query, q, sizeof(q));
  int r = 0, g = 0, b = 0;
  char hexbuf[16];

  if (scan_rgb(q, &r, &g, &b)) {
    /* ok */
  } else if (scan_hsl(q, &r, &g, &b)) {
    /* ok */
  } else if (q[0] == '#' && extract_hex_from_query(q, hexbuf, sizeof(hexbuf)) &&
             parse_hex_color(hexbuf, &r, &g, &b)) {
    /* ok */
  } else {
    const char *hash = strchr(q, '#');
    const char *cw = strcasestr(q, "color ");
    if (cw && hash && hash > cw && extract_hex_from_query(hash, hexbuf, sizeof(hexbuf)) &&
        parse_hex_color(hexbuf, &r, &g, &b)) {
      /* ok */
    } else {
      return info;
    }
  }

  char *html = wrap_color(r, g, b);
  if (!html)
    return info;
  info.title = strdup("Color");
  info.extract = html;
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");
  return info;
}
