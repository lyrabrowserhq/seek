#include "UnitConversion.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define UNIT_NORM_MAX 512

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

static void trim_inplace_mut(char *s) {
  char *start = s;
  while (*start && isspace((unsigned char)*start))
    start++;
  if (start != s)
    memmove(s, start, strlen(start) + 1);
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1]))
    len--;
  s[len] = '\0';
}

static void strip_unit_prefixes(char *s) {
  int again;
  do {
    again = 0;
    if (strncasecmp(s, "unit conversion ", 15) == 0) {
      memmove(s, s + 15, strlen(s + 15) + 1);
      again = 1;
    } else if (strncasecmp(s, "measurement conversion ", 23) == 0) {
      memmove(s, s + 23, strlen(s + 23) + 1);
      again = 1;
    } else if (strncasecmp(s, "measurement ", 12) == 0) {
      memmove(s, s + 12, strlen(s + 12) + 1);
      again = 1;
    } else if (strncasecmp(s, "convert ", 8) == 0) {
      memmove(s, s + 8, strlen(s + 8) + 1);
      again = 1;
    }
  } while (again);
}

static void normalize_unit_query(const char *query, char *out, size_t cap) {
  trim_in(query, out, cap);
  strip_unit_prefixes(out);
  trim_inplace_mut(out);
}

typedef enum {
  UNIT_LENGTH,
  UNIT_WEIGHT,
  UNIT_TEMP,
  UNIT_VOLUME,
  UNIT_AREA,
  UNIT_TIME,
  UNIT_UNKNOWN
} UnitType;

typedef struct {
  const char *name;
  const char *alias[4];
  UnitType type;
  double to_base;
} UnitDef;

static const UnitDef UNITS[] = {
    {"metre", {"m", "metres", "meter", "meters"}, UNIT_LENGTH, 1.0},
    {"kilometre",
     {"km", "kilometres", "kilometer", "kilometers"},
     UNIT_LENGTH,
     1000.0},
    {"centimetre",
     {"cm", "centimetres", "centimeter", "centimeters"},
     UNIT_LENGTH,
     0.01},
    {"millimetre",
     {"mm", "millimetres", "millimeter", "millimeters"},
     UNIT_LENGTH,
     0.001},
    {"mile", {"mi", "miles"}, UNIT_LENGTH, 1609.344},
    {"yard", {"yd", "yards"}, UNIT_LENGTH, 0.9144},
    {"foot", {"ft", "feet", "'"}, UNIT_LENGTH, 0.3048},
    {"inch", {"in", "inches", "\""}, UNIT_LENGTH, 0.0254},

    {"kilogram", {"kg", "kilograms", "kilo", "kilos"}, UNIT_WEIGHT, 1.0},
    {"gram", {"g", "grams"}, UNIT_WEIGHT, 0.001},
    {"milligram", {"mg", "milligrams"}, UNIT_WEIGHT, 0.000001},
    {"pound", {"lb", "lbs", "pounds"}, UNIT_WEIGHT, 0.453592},
    {"ounce", {"oz", "ounces"}, UNIT_WEIGHT, 0.0283495},
    {"tonne", {"tonnes", "tons", "ton"}, UNIT_WEIGHT, 1000.0},
    {"stone", {"st", "stones"}, UNIT_WEIGHT, 6.35029},

    {"celsius", {"c", "°c", "degrees celsius", "degrees c"}, UNIT_TEMP, 1.0},
    {"fahrenheit",
     {"f", "°f", "degrees fahrenheit", "degrees f"},
     UNIT_TEMP,
     1.0},
    {"kelvin", {"k", "degrees kelvin", "degrees k"}, UNIT_TEMP, 1.0},

    {"litre", {"l", "litres", "liter", "liters"}, UNIT_VOLUME, 1.0},
    {"millilitre",
     {"ml", "millilitres", "milliliter", "milliliters"},
     UNIT_VOLUME,
     0.001},
    {"gallon", {"gal", "gallons"}, UNIT_VOLUME, 3.78541},
    {"quart", {"qt", "quarts"}, UNIT_VOLUME, 0.946353},
    {"pint", {"pt", "pints"}, UNIT_VOLUME, 0.473176},
    {"cup", {"cups"}, UNIT_VOLUME, 0.236588},
    {"fluid ounce", {"fl oz", "fluid ounces"}, UNIT_VOLUME, 0.0295735},

    {"square metre", {"sqm", "sq m", "m2", "square metres"}, UNIT_AREA, 1.0},
    {"square foot",
     {"sqft", "sq ft", "ft2", "square feet"},
     UNIT_AREA,
     0.092903},
    {"square kilometre",
     {"sqkm", "sq km", "km2", "square kilometres"},
     UNIT_AREA,
     1000000.0},
    {"square mile",
     {"sqmi", "sq mi", "mi2", "square miles"},
     UNIT_AREA,
     2589988.0},
    {"acre", {"acres"}, UNIT_AREA, 4046.86},
    {"hectare", {"ha", "hectares"}, UNIT_AREA, 10000.0},

    {"second", {"sec", "seconds", "s"}, UNIT_TIME, 1.0},
    {"minute", {"min", "minutes"}, UNIT_TIME, 60.0},
    {"hour", {"hr", "hours", "h"}, UNIT_TIME, 3600.0},
    {"day", {"days", "d"}, UNIT_TIME, 86400.0},
    {"week", {"weeks", "wk"}, UNIT_TIME, 604800.0},
    {"month", {"months", "mo"}, UNIT_TIME, 2629746.0},
    {"year", {"years", "yr"}, UNIT_TIME, 31556952.0},
};

static const int UNIT_COUNT = sizeof(UNITS) / sizeof(UNITS[0]);

static int is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const UnitDef *find_unit(const char *str) {
  if (!str || !*str)
    return NULL;

  size_t len = strlen(str);
  char normalized[64] = {0};
  size_t j = 0;

  for (size_t i = 0; i < len && j < 63; i++) {
    if ((unsigned char)str[i] == 0xC2 && (unsigned char)str[i + 1] == 0xB0) {
      i++;
      continue;
    }
    if (str[i] == '^' && i + 1 < len && str[i + 1] == '2') {
      normalized[j++] = '2';
      i++;
      continue;
    }
    normalized[j++] = tolower((unsigned char)str[i]);
  }
  normalized[j] = '\0';

  for (int i = 0; i < UNIT_COUNT; i++) {
    if (strcmp(normalized, UNITS[i].name) == 0)
      return &UNITS[i];
    for (int k = 0; k < 4 && UNITS[i].alias[k]; k++) {
      if (strcmp(normalized, UNITS[i].alias[k]) == 0)
        return &UNITS[i];
    }
  }
  return NULL;
}

int is_unit_conv_query(const char *query) {
  if (!query)
    return 0;

  char buf[UNIT_NORM_MAX];
  normalize_unit_query(query, buf, sizeof buf);

  const char *patterns[] = {" to ",     " in ",    " into ",    " = ",
                            " equals ", " equal ", " convert ", " conversion ",
                            " -> ",     " → ",     NULL};

  int has_pattern = 0;
  for (int i = 0; patterns[i]; i++) {
    if (strstr(buf, patterns[i])) {
      has_pattern = 1;
      break;
    }
  }

  if (!has_pattern) {
    const char *last_space = strrchr(buf, ' ');
    if (last_space) {
      const UnitDef *u = find_unit(last_space + 1);
      if (u) {
        const char *before = buf;
        while (*before && is_whitespace(*before))
          before++;
        const char *num_end = before;
        while (*num_end &&
               (isdigit(*num_end) || *num_end == '.' || *num_end == '-' ||
                *num_end == '+' || *num_end == '/' || *num_end == '\'' ||
                *num_end == '"')) {
          num_end++;
        }
        if (num_end > before)
          has_pattern = 1;
      }
    }
  }

  return has_pattern;
}

static double parse_value(const char **ptr) {
  const char *p = *ptr;
  while (*p && is_whitespace(*p))
    p++;

  double value = 0.0;
  int has_num = 0;

  if (*p == '-' || *p == '+')
    p++;
  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (*p - '0');
    has_num = 1;
    p++;
  }
  if (*p == '.') {
    p++;
    double frac = 0.1;
    while (*p >= '0' && *p <= '9') {
      value += (*p - '0') * frac;
      frac *= 0.1;
      has_num = 1;
      p++;
    }
  }

  if (*p == '/' && has_num) {
    p++;
    double denom = 0.0;
    int has_denom = 0;
    while (*p >= '0' && *p <= '9') {
      denom = denom * 10 + (*p - '0');
      has_denom = 1;
      p++;
    }
    if (has_denom && denom > 0) {
      value = value / denom;
    }
  }

  while (*p == '\'' || *p == '"') {
    double extra = 0.0;
    p++;
    while (*p >= '0' && *p <= '9') {
      extra = extra * 10 + (*p - '0');
      p++;
    }
    if (*p == '.') {
      p++;
      double frac = 0.1;
      while (*p >= '0' && *p <= '9') {
        extra += (*p - '0') * frac;
        frac *= 0.1;
        p++;
      }
    }
    if (*p == '\'' || *p == '"')
      p++;
    value += extra * (p[-1] == '\'' ? 0.3048 : 0.0254);
  }

  if (!has_num) {
    *ptr = p;
    return 0.0;
  }

  *ptr = p;
  return value;
}

static int is_separator(char c) {
  return is_whitespace(c) || c == ',' || c == '.' || c == '(' || c == ')' ||
         c == '\0';
}

static int parse_conversion_query(const char *query, double *value,
                                  const UnitDef **from_unit,
                                  const UnitDef **to_unit) {
  char buf[UNIT_NORM_MAX];
  normalize_unit_query(query, buf, sizeof buf);

  *value = 0;
  *from_unit = NULL;
  *to_unit = NULL;

  const char *value_end = buf;
  *value = parse_value(&value_end);

  if (value_end == buf)
    return 0;

  const char *p = value_end;
  while (*p && is_whitespace(*p))
    p++;

  size_t remaining = strlen(p);
  if (remaining < 2)
    return 0;

  const char *to_keywords[] = {" to ", " in ", " into ", " -> ",
                               " → ",  " = ",  NULL};
  const char *to_pos = NULL;
  size_t keyword_len = 0;
  for (int i = 0; to_keywords[i]; i++) {
    const char *found = strstr(p, to_keywords[i]);
    if (found) {
      to_pos = found + strlen(to_keywords[i]);
      keyword_len = strlen(to_keywords[i]);
      break;
    }
  }

  if (!to_pos) {
    const char *last_space = strrchr(p, ' ');
    if (last_space && last_space > p) {
      char from_part[64] = {0};
      size_t len = last_space - p;
      if (len < 63) {
        strncpy(from_part, p, len);
        *from_unit = find_unit(from_part);
        if (*from_unit) {
          *to_unit = find_unit(last_space + 1);
          return *to_unit ? 1 : 0;
        }
      }
    }
    return 0;
  }

  char from_part[64] = {0};
  size_t from_len = to_pos - p - keyword_len;
  if (from_len > 63)
    from_len = 63;
  strncpy(from_part, p, from_len);

  char *end_from = from_part + from_len;
  while (end_from > from_part && is_whitespace(end_from[-1]))
    end_from--;
  *end_from = '\0';

  *from_unit = find_unit(from_part);
  if (!*from_unit) {
    char *end = from_part + strlen(from_part);
    while (end > from_part) {
      while (end > from_part && is_whitespace(end[-1]))
        end--;
      if (end <= from_part)
        break;
      char *start = end;
      while (start > from_part && !is_whitespace(start[-1]))
        start--;
      size_t word_len = end - start;
      memmove(from_part + word_len + 1, from_part, start - from_part);
      from_part[word_len] = ' ';
      from_part[word_len + 1] = '\0';
      *from_unit = find_unit(from_part);
      if (*from_unit)
        break;
      end = start;
    }
  }

  if (!*from_unit)
    return 0;

  while (*to_pos && is_whitespace(*to_pos))
    to_pos++;

  if (!*to_pos)
    return 0;

  char to_part[64] = {0};
  size_t to_len = 0;
  const char *tp = to_pos;
  while (*tp && !is_separator(*tp) && to_len < 63) {
    to_part[to_len++] = *tp++;
  }
  to_part[to_len] = '\0';

  *to_unit = find_unit(to_part);
  if (!*to_unit) {
    const char *try_ptr = to_pos;
    while (*try_ptr && is_whitespace(*try_ptr))
      try_ptr++;
    char try_buf[64] = {0};
    size_t try_len = 0;
    while (*try_ptr && try_len < 63) {
      try_buf[try_len++] = *try_ptr++;
    }
    while (try_len > 0) {
      *to_unit = find_unit(try_buf);
      if (*to_unit) {
        strcpy(to_part, try_buf);
        break;
      }
      char *last_space = strrchr(try_buf, ' ');
      if (!last_space)
        break;
      *last_space = '\0';
      try_len = strlen(try_buf);
    }
  }

  return *to_unit ? 1 : 0;
}

static double convert_temp(double value, const UnitDef *from,
                           const UnitDef *to) {
  double celsius = 0;

  if (strcmp(from->name, "celsius") == 0)
    celsius = value;
  else if (strcmp(from->name, "fahrenheit") == 0)
    celsius = (value - 32) * 5.0 / 9.0;
  else if (strcmp(from->name, "kelvin") == 0)
    celsius = value - 273.15;

  if (strcmp(to->name, "celsius") == 0)
    return celsius;
  else if (strcmp(to->name, "fahrenheit") == 0)
    return celsius * 9.0 / 5.0 + 32;
  else if (strcmp(to->name, "kelvin") == 0)
    return celsius + 273.15;
  return 0;
}

static double convert_value(double value, const UnitDef *from,
                            const UnitDef *to) {
  if (from->type != to->type)
    return 0;

  if (from->type == UNIT_TEMP) {
    return convert_temp(value, from, to);
  }

  double base_value = value * from->to_base;
  return base_value / to->to_base;
}

static void format_number(double val, char *buf, size_t bufsize) {
  if (bufsize == 0)
    return;
  if (val == 0) {
    snprintf(buf, bufsize, "0");
    return;
  }
  if (fabs(val) < 0.01 && fabs(val) > 0) {
    snprintf(buf, bufsize, "%.2g", val);
  } else if (fabs(val) < 1) {
    snprintf(buf, bufsize, "%.2f", val);
    char *p = buf + strlen(buf) - 1;
    while (p > buf && *p == '0')
      *p-- = '\0';
    if (*p == '.')
      *p = '\0';
  } else if (fmod(val + 0.0001, 1.0) < 0.0002) {
    snprintf(buf, bufsize, "%.0f", val);
  } else {
    snprintf(buf, bufsize, "%.2f", val);
    char *p = buf + strlen(buf) - 1;
    while (p > buf && *p == '0')
      *p-- = '\0';
    if (*p == '.')
      *p = '\0';
  }
}

static const char *pluralize(const char *unit, double value, char *buf,
                             size_t bufsize) {
  int is_one = (fabs(value - 1.0) < 0.0001 || fabs(value + 1.0) < 0.0001);

  size_t len = strlen(unit);
  if (len == 0 || bufsize == 0)
    return unit;

  strncpy(buf, unit, bufsize - 1);
  buf[bufsize - 1] = '\0';

  if (strcmp(unit, "foot") == 0 || strcmp(unit, "square foot") == 0) {
    if (is_one)
      strcpy(buf, unit);
    else
      strcpy(buf, strcmp(unit, "square foot") == 0 ? "square feet" : "feet");
    return buf;
  }
  if (strcmp(unit, "inch") == 0 || strcmp(unit, "square inch") == 0) {
    if (is_one)
      strcpy(buf, unit);
    else
      strcpy(buf,
             strcmp(unit, "square inch") == 0 ? "square inches" : "inches");
    return buf;
  }
  if (strcmp(unit, "stone") == 0) {
    if (is_one)
      strcpy(buf, "stone");
    else
      strcpy(buf, "stones");
    return buf;
  }
  if (strcmp(unit, "celsius") == 0 || strcmp(unit, "fahrenheit") == 0 ||
      strcmp(unit, "kelvin") == 0) {
    strcpy(buf, unit);
    return buf;
  }

  if (unit[len - 1] == 's' || unit[len - 1] == 'x' || unit[len - 1] == 'z' ||
      (len >= 2 && unit[len - 2] == 'c' && unit[len - 1] == 'h') ||
      (len >= 2 && unit[len - 2] == 's' && unit[len - 1] == 'h')) {
    if (!is_one) {
      buf[len] = 'e';
      buf[len + 1] = '\0';
    }
  } else if (unit[len - 1] == 'y' && len >= 2 &&
             !(unit[len - 2] == 'a' || unit[len - 2] == 'e' ||
               unit[len - 2] == 'i' || unit[len - 2] == 'o' ||
               unit[len - 2] == 'u')) {
    if (is_one) {
      buf[len - 1] = '\0';
    } else {
      buf[len] = 's';
      buf[len + 1] = '\0';
    }
  } else if (len >= 2 && unit[len - 2] == 'f' && unit[len - 1] == 'e') {
    if (is_one) {
      buf[len - 2] = '\0';
    } else {
      buf[len - 1] = 's';
      buf[len] = '\0';
    }
  } else if (unit[len - 1] == 'f' && len >= 1) {
    if (is_one) {
      buf[len - 1] = '\0';
    } else {
      buf[len - 1] = 'v';
      buf[len] = 'e';
      buf[len + 1] = 's';
      buf[len + 2] = '\0';
    }
  } else if (unit[len - 1] == 'e' && len >= 2 && unit[len - 2] == 'f') {
    if (is_one) {
      buf[len - 2] = '\0';
    } else {
      buf[len - 1] = 's';
      buf[len] = '\0';
    }
  } else {
    if (!is_one) {
      buf[len] = 's';
      buf[len + 1] = '\0';
    }
  }

  return buf;
}

static const char *unit_key(const UnitDef *u) {
  return u->alias[0] ? u->alias[0] : u->name;
}

static char *build_html(double value, const UnitDef *from, double result,
                        const UnitDef *to) {
  char val_buf[64], res_buf[64], from_name_buf[64], to_name_buf[64];
  format_number(value, val_buf, sizeof(val_buf));
  format_number(result, res_buf, sizeof(res_buf));

  pluralize(from->name, value, from_name_buf, sizeof(from_name_buf));
  pluralize(to->name, result, to_name_buf, sizeof(to_name_buf));

  char ev[96], ef[96], et[96];
  html_escape_attr(val_buf, ev, sizeof(ev));
  html_escape_attr(unit_key(from), ef, sizeof(ef));
  html_escape_attr(unit_key(to), et, sizeof(et));

  char fb_raw[512];
  snprintf(fb_raw, sizeof(fb_raw), "%s %s = %s %s", val_buf, from_name_buf,
           res_buf, to_name_buf);
  char fb_esc[600];
  html_escape_text(fb_raw, fb_esc, sizeof(fb_esc));

  char *html = malloc(1536);
  if (!html)
    return NULL;
  snprintf(html, 1536,
           "<div class=\"widget widget-conversion\" data-widget=\"conversion\" "
           "data-value=\"%s\" data-from=\"%s\" data-to=\"%s\">"
           "<div class=\"widget-conversion-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           ev, ef, et, fb_esc);
  return html;
}

InfoBox fetch_unit_conv_data(const char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  double value = 0;
  const UnitDef *from = NULL;
  const UnitDef *to = NULL;

  if (!parse_conversion_query(query, &value, &from, &to))
    return info;
  if (!from || !to)
    return info;
  if (from->type != to->type)
    return info;

  double result = convert_value(value, from, to);
  if (result == 0 && value != 0 && from->type != UNIT_TEMP)
    return info;

  info.title = strdup("Unit Conversion");
  info.extract = build_html(value, from, result, to);
  if (!info.extract) {
    free(info.title);
    info.title = NULL;
    return info;
  }
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");

  return info;
}
