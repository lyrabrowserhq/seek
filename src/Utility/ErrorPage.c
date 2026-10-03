#include "ErrorPage.h"
#include "HtmlEscape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_FIELD_CAP 512

static const char *ERROR_TEMPLATE =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, "
    "initial-scale=1.0, maximum-scale=5.0\">\n"
    "    <title>Seek</title>\n"
    "    <link rel=\"stylesheet\" href=\"/static/main.css?v=8\">\n"
    "    <link rel=\"icon\" type=\"image/x-icon\" href=\"/static/favicon.ico\">\n"
    "    <script src=\"/static/theme.js?v=2\"></script>\n"
    "</head>\n"
    "<body>\n"
    "    <main class=\"error-page\">\n"
    "        <div class=\"error-card\">\n"
    "            <a class=\"error-logo\" href=\"/\">Seek</a>\n"
    "            <h1 class=\"error-heading\">%s</h1>\n"
    "            <p class=\"error-detail\">%s</p>\n"
    "            <a class=\"error-back\" href=\"%s\">"
    "<span class=\"ui-icon ui-icon-arrow-left\" aria-hidden=\"true\"></span>"
    " Back to Seek</a>\n"
    "        </div>\n"
    "    </main>\n"
    "</body>\n"
    "</html>\n";

char *render_error_page(const char *heading, const char *detail,
                        const char *back_href) {
  char h[ERROR_FIELD_CAP];
  char d[ERROR_FIELD_CAP];
  char u[ERROR_FIELD_CAP];

  html_escape_text(heading ? heading : "Something went wrong", h, sizeof(h));
  html_escape_text(detail ? detail : "", d, sizeof(d));
  html_escape_attr(back_href ? back_href : "/", u, sizeof(u));

  size_t need =
      strlen(ERROR_TEMPLATE) + strlen(h) + strlen(d) + strlen(u) + 8;
  char *out = malloc(need);
  if (out)
    snprintf(out, need, ERROR_TEMPLATE, h, d, u);
  return out;
}

static const char *SUGGEST_TEMPLATE =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, "
    "initial-scale=1.0, maximum-scale=5.0\">\n"
    "    <title>Seek</title>\n"
    "    <link rel=\"stylesheet\" href=\"/static/main.css?v=8\">\n"
    "    <link rel=\"icon\" type=\"image/x-icon\" href=\"/static/favicon.ico\">\n"
    "    <script src=\"/static/theme.js?v=2\"></script>\n"
    "</head>\n"
    "<body>\n"
    "    <main class=\"error-page\">\n"
    "        <div class=\"error-card\">\n"
    "            <a class=\"error-logo\" href=\"/\">Seek</a>\n"
    "            <h1 class=\"error-heading\">%s</h1>\n"
    "            <p class=\"error-detail\">%s <a class=\"error-suggest\" "
    "href=\"%s\">%s</a></p>\n"
    "            <a class=\"error-back\" href=\"%s\">"
    "<span class=\"ui-icon ui-icon-arrow-left\" aria-hidden=\"true\"></span>"
    " Back to Seek</a>\n"
    "        </div>\n"
    "    </main>\n"
    "</body>\n"
    "</html>\n";

char *render_suggestion_error_page(const char *heading,
                                   const char *did_you_mean_label,
                                   const char *suggestion,
                                   const char *suggest_href,
                                   const char *back_href) {
  char h[ERROR_FIELD_CAP];
  char lbl[ERROR_FIELD_CAP];
  char sug[ERROR_FIELD_CAP];
  char u[ERROR_FIELD_CAP];
  char b[ERROR_FIELD_CAP];

  html_escape_text(heading ? heading : "Something went wrong", h, sizeof(h));
  html_escape_text(did_you_mean_label ? did_you_mean_label : "Did you mean",
                   lbl, sizeof(lbl));
  html_escape_text(suggestion ? suggestion : "", sug, sizeof(sug));
  html_escape_attr(suggest_href ? suggest_href : "/", u, sizeof(u));
  html_escape_attr(back_href ? back_href : "/", b, sizeof(b));

  size_t need = strlen(SUGGEST_TEMPLATE) + strlen(h) + strlen(lbl) +
                strlen(sug) + strlen(u) + strlen(b) + 8;
  char *out = malloc(need);
  if (out)
    snprintf(out, need, SUGGEST_TEMPLATE, h, lbl, u, sug, b);
  return out;
}
