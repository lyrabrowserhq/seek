#include "SettingsSave.h"
#include "../Scraping/Scraping.h"
#include "../Utility/Utility.h"
#include <stdlib.h>
#include <string.h>

#define PREF_COOKIE_EXPIRES "Fri, 31 Dec 2038 23:59:59 GMT"

int settings_save_handler(UrlParams *params) {
  const char *locale = "";
  const char *default_engine = "all";
  int default_engine_present = 0;
  int engines_present = 0;
  int forums_present = 0;
  int forums = 0;
  int slop_present = 0;
  const char *slop = "off";
  int want_json = 0;
  int total = engines_total();
  char **selected_ids = calloc((size_t)total, sizeof(char *));
  int selected_count = 0;

  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "locale") == 0) {
        locale = params->params[i].value;
      } else if (strcmp(params->params[i].key, "default_engine") == 0) {
        default_engine = params->params[i].value;
        default_engine_present = 1;
      } else if (strcmp(params->params[i].key, "engines_present") == 0) {
        engines_present = 1;
      } else if (strcmp(params->params[i].key, "forums_present") == 0) {
        forums_present = 1;
      } else if (strcmp(params->params[i].key, "forums") == 0) {
        forums = strcmp(params->params[i].value, "1") == 0;
      } else if (strcmp(params->params[i].key, "slop_present") == 0) {
        slop_present = 1;
      } else if (strcmp(params->params[i].key, "slop") == 0) {
        slop = params->params[i].value;
      } else if (strcmp(params->params[i].key, "format") == 0 &&
                 strcmp(params->params[i].value, "json") == 0) {
        want_json = 1;
      } else if (strncmp(params->params[i].key, "engine_", 7) == 0 &&
                 strcmp(params->params[i].value, "1") == 0) {
        const char *engine_id = params->params[i].key + 7;
        if (selected_ids && engine_id[0] != '\0' &&
            is_engine_id_enabled(engine_id) && selected_count < total) {
          selected_ids[selected_count] = (char *)engine_id;
          selected_count++;
        }
      }
    }
  }

  if (locale[0] != '\0' && beaker_get_locale_meta(locale) != NULL) {
    set_cookie("locale", locale, PREF_COOKIE_EXPIRES, "/", false, false);
  }

  if (engines_present) {
    char cookie_value[512];
    cookie_value[0] = '\0';
    for (int i = 0; i < selected_count; i++) {
      if (i > 0)
        strncat(cookie_value, "+",
                sizeof(cookie_value) - strlen(cookie_value) - 1);
      strncat(cookie_value, selected_ids[i],
              sizeof(cookie_value) - strlen(cookie_value) - 1);
    }
    set_cookie("engines", cookie_value, PREF_COOKIE_EXPIRES, "/", false, false);
  }

  if (forums_present) {
    set_cookie("forums", forums ? "1" : "0", PREF_COOKIE_EXPIRES, "/", false,
               false);
  }

  if (slop_present) {
    const char *validated = "off";
    if (strcmp(slop, "hide") == 0 || strcmp(slop, "derank") == 0 ||
        strcmp(slop, "score") == 0)
      validated = slop;
    set_cookie("slop", validated, PREF_COOKIE_EXPIRES, "/", false, false);
  }

  if (default_engine_present) {
    const char *validated = "all";
    if (strcmp(default_engine, "all") == 0) {
      validated = "all";
    } else {
      for (int i = 0; i < total; i++) {
        const SearchEngine *eng = engine_at(i);
        if (!eng || !eng->enabled || strcmp(eng->id, default_engine) != 0)
          continue;
        int selected = !engines_present;
        for (int j = 0; j < selected_count; j++) {
          if (strcmp(selected_ids[j], eng->id) == 0) {
            selected = 1;
            break;
          }
        }
        if (selected)
          validated = eng->id;
        break;
      }
    }
    set_cookie("default_engine", validated, PREF_COOKIE_EXPIRES, "/", false,
               false);
  }

  free(selected_ids);
  if (want_json) {
    serve_data("{\"ok\":true}", 11, "application/json; charset=UTF-8");
    return 0;
  }
  send_redirect("/settings");
  return 0;
}
