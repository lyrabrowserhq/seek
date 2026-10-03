#include "Settings.h"
#include "../Config.h"
#include "../Scraping/Scraping.h"
#include "../Utility/Utility.h"
#include <beaker.h>
#include <stdlib.h>
#include <string.h>

extern Config global_config;

int settings_handler(UrlParams *params) {
  (void)params;
  TemplateContext ctx = new_context();

  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);

  LocaleInfo locales[32];
  int locale_count = beaker_get_all_locales(locales, 32);
  if (locale_count < 0)
    locale_count = 0;

  char **locale_rows[32];
  int locale_inner[32];
  for (int i = 0; i < locale_count; i++) {
    locale_rows[i] = malloc(sizeof(char *) * 2);
    if (locale_rows[i]) {
      locale_rows[i][0] = locales[i].meta.id;
      locale_rows[i][1] = locales[i].meta.name;
      locale_inner[i] = 2;
    } else {
      locale_inner[i] = 0;
    }
  }

  char *default_engine = get_default_search_engine(global_config.default_engine);

  char **user_engines = NULL;
  int user_engine_count = 0;
  int has_user_pref =
      (get_user_engines(&user_engines, &user_engine_count) == 0);

  if (strcmp(default_engine ? default_engine : "all", "all") != 0 &&
      has_user_pref &&
      !user_engines_contains(default_engine, user_engines, user_engine_count)) {
    free(default_engine);
    default_engine = strdup("all");
  }

  int total = engines_total();
  char ***engine_rows = malloc(sizeof(char **) * total);
  int *engine_inner = malloc(sizeof(int) * total);
  int enabled_count = 0;

  if (engine_rows && engine_inner) {
    for (int i = 0; i < total; i++) {
      const SearchEngine *eng = engine_at(i);
      if (!eng || !eng->enabled)
        continue;

      int is_selected = 1;
      if (has_user_pref) {
        is_selected = user_engines_contains(eng->id, user_engines,
                                            user_engine_count);
      }

      engine_rows[enabled_count] = malloc(sizeof(char *) * 4);
      if (engine_rows[enabled_count]) {
        engine_rows[enabled_count][0] = (char *)eng->id;
        engine_rows[enabled_count][1] = (char *)eng->name;
        engine_rows[enabled_count][2] = is_selected ? "checked" : "";
        engine_rows[enabled_count][3] =
            default_engine && strcmp(default_engine, eng->id) == 0
                ? "selected"
                : "";
        engine_inner[enabled_count] = 4;
        enabled_count++;
      }
    }
  }

  context_set(&ctx, "default_engine", default_engine ? default_engine : "all");
  if (locale_count > 0) {
    context_set_array_of_arrays(&ctx, "locales", locale_rows, locale_count,
                                locale_inner);
  }
  if (enabled_count > 0) {
    context_set_array_of_arrays(&ctx, "enabled_engines", engine_rows,
                                enabled_count, engine_inner);
    context_set(&ctx, "has_enabled_engines", "1");
  }

  char *forums = get_cookie("forums");
  if (!forums || forums[0] != '0')
    context_set(&ctx, "forums_checked", "checked");
  free(forums);

  char *slop = get_cookie("slop");
  if (slop && (strcmp(slop, "hide") == 0 || strcmp(slop, "derank") == 0 ||
               strcmp(slop, "score") == 0))
    context_set(&ctx, "slop_mode", slop);
  else
    context_set(&ctx, "slop_mode", "off");
  free(slop);

  char *rendered = render_template("settings.html", &ctx);
  if (rendered) {
    send_response(rendered);
    free(rendered);
  } else {
    send_status("500 Internal Server Error");
  }

  for (int i = 0; i < locale_count; i++)
    free(locale_rows[i]);
  if (engine_rows) {
    for (int i = 0; i < enabled_count; i++)
      free(engine_rows[i]);
    free(engine_rows);
  }
  free(engine_inner);
  free(default_engine);
  if (user_engines) {
    for (int i = 0; i < user_engine_count; i++)
      free(user_engines[i]);
    free(user_engines);
  }
  free(locale);
  free_context(&ctx);
  return 0;
}
