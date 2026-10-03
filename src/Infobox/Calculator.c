#include "Calculator.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char logic_log[4096];

typedef struct {
  const char *buffer;
  int pos;
} Parser;

static double parse_expression(Parser *p);

static void skip_ws(Parser *p) {
  while (p->buffer[p->pos] == ' ')
    p->pos++;
}

static double parse_factor(Parser *p) {
  skip_ws(p);
  if (p->buffer[p->pos] == '-') {
    p->pos++;
    return -parse_factor(p);
  }
  if (p->buffer[p->pos] == '(') {
    p->pos++;
    double res = parse_expression(p);
    if (p->buffer[p->pos] == ')')
      p->pos++;
    return res;
  }
  char *endptr;
  double val = strtod(&p->buffer[p->pos], &endptr);
  p->pos = (int)(endptr - p->buffer);
  return val;
}

static double parse_term(Parser *p) {
  double left = parse_factor(p);
  while (1) {
    skip_ws(p);
    char op = p->buffer[p->pos];
    if (op == '*' || op == '/') {
      p->pos++;
      double right = parse_factor(p);
      double old = left;
      left = (op == '*') ? left * right : left / right;

      char step[256];

      snprintf(step, sizeof(step),
               "<div class=\"widget-calc-step\">%g %c %g = <b>%g</b></div>", old,
               op, right, left);
      strncat(logic_log, step, sizeof(logic_log) - strlen(logic_log) - 1);
    } else
      break;
  }
  return left;
}

static double parse_expression(Parser *p) {
  double left = parse_term(p);
  while (1) {
    skip_ws(p);
    char op = p->buffer[p->pos];
    if (op == '+' || op == '-') {
      p->pos++;
      double right = parse_term(p);
      double old = left;
      left = (op == '+') ? left + right : left - right;

      char step[256];

      snprintf(step, sizeof(step),
               "<div class=\"widget-calc-step\">%g %c %g = <b>%g</b></div>", old,
               op, right, left);
      strncat(logic_log, step, sizeof(logic_log) - strlen(logic_log) - 1);
    } else
      break;
  }
  return left;
}

double evaluate(const char *expr) {
  logic_log[0] = '\0';
  if (!expr || strlen(expr) == 0)
    return 0.0;
  Parser p = {expr, 0};
  return parse_expression(&p);
}

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

static int strip_calc_prefix(const char *query, char *out, size_t cap) {
  char buf[512];
  trim_in(query, buf, sizeof(buf));
  if (buf[0] == '\0')
    return 0;

  const char *prefixes[] = {"calculator ", "calculate ", "calc ", NULL};
  for (int i = 0; prefixes[i]; i++) {
    size_t L = strlen(prefixes[i]);
    if (strncasecmp(buf, prefixes[i], L) == 0) {
      trim_in(buf + L, out, cap);
      return 1;
    }
  }
  if (strcasecmp(buf, "calc") == 0 || strcasecmp(buf, "calculator") == 0 ||
      strcasecmp(buf, "calculate") == 0) {
    out[0] = '\0';
    return 1;
  }
  return 0;
}

static int is_math_expression(const char *query) {
  if (!query)
    return 0;

  int has_digit = 0;
  int has_math_operator = 0;

  for (const char *p = query; *p; p++) {
    if (isdigit((unsigned char)*p) || *p == '.') {
      has_digit = 1;
    }
    if (*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == '^') {
      has_math_operator = 1;
    }
  }

  if (!has_digit || !has_math_operator)
    return 0;

  int len = (int)strlen(query);
  for (int i = 0; i < len; i++) {
    char c = query[i];
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
      int has_num_before = 0;
      int has_num_after = 0;

      for (int j = i - 1; j >= 0; j--) {
        if (isdigit((unsigned char)query[j]) || query[j] == '.') {
          has_num_before = 1;
          break;
        }
        if (query[j] != ' ')
          break;
      }

      for (int j = i + 1; j < len; j++) {
        if (isdigit((unsigned char)query[j]) || query[j] == '.') {
          has_num_after = 1;
          break;
        }
        if (query[j] != ' ')
          break;
      }

      if (has_num_before || has_num_after) {
        return 1;
      }
    }
  }

  return 0;
}

int calc_query_matches(const char *query) {
  char work[512];
  trim_in(query, work, sizeof(work));
  char expr[512];
  if (strip_calc_prefix(work, expr, sizeof(expr))) {
    if (expr[0] == '\0')
      return 1;
    return is_math_expression(expr);
  }
  return is_math_expression(work);
}

InfoBox fetch_calc_data(char *math_input) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!math_input)
    return info;

  char work[512];
  trim_in(math_input, work, sizeof(work));
  char expr[512];
  int had_prefix = strip_calc_prefix(work, expr, sizeof(expr));
  const char *eval_src = had_prefix ? expr : work;

  char html_output[6144];
  if (eval_src[0] == '\0') {
    snprintf(html_output, sizeof(html_output),
             "<div class=\"widget widget-calc\" data-widget=\"calc\" "
             "data-initial-expr=\"\" data-initial-result=\"\">"
             "<div class=\"widget-calc-root\"></div>"
             "<noscript><p class=\"widget-fallback\">Turn on JavaScript for the "
             "full calculator with buttons.</p></noscript></div>");
    info.title = strdup("Calculator");
    info.extract = strdup(html_output);
    info.thumbnail_url = strdup("/static/calculation.svg");
    info.url = strdup("#");
    return info;
  }

  double result = evaluate(eval_src);

  char expr_esc[1200];
  char res_str[64];
  char fallback_raw[1024];
  char fb_esc[1200];
  html_escape_attr(eval_src, expr_esc, sizeof(expr_esc));
  snprintf(res_str, sizeof(res_str), "%g", result);
  snprintf(fallback_raw, sizeof(fallback_raw), "%s = %g", eval_src, result);
  html_escape_text(fallback_raw, fb_esc, sizeof(fb_esc));

  snprintf(html_output, sizeof(html_output),
           "<div class=\"widget widget-calc\" data-widget=\"calc\" "
           "data-initial-expr=\"%s\" data-initial-result=\"%s\">"
           "<div class=\"widget-calc-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           expr_esc, res_str, fb_esc);

  info.title = strdup("Calculator");
  info.extract = strdup(html_output);
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");

  return info;
}
