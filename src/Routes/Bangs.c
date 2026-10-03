#include "Bangs.h"
#include "../Bangs.h"
#include "../Utility/Utility.h"
#include <beaker.h>
#include <stdlib.h>

int bangs_handler(UrlParams *params) {
  (void)params;
  TemplateContext ctx = new_context();
  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  free(locale);

  int count = bangs_count();
  char ***rows = malloc(sizeof(char **) * count);
  int *inner = malloc(sizeof(int) * count);
  int n = 0;

  if (rows && inner) {
    for (int i = 0; i < count; i++) {
      const BangEntry *b = bangs_at(i);
      if (!b)
        continue;
      rows[n] = malloc(sizeof(char *) * 2);
      if (rows[n]) {
        rows[n][0] = (char *)b->name;
        rows[n][1] = (char *)b->tmpl;
        inner[n] = 2;
        n++;
      }
    }
  }

  if (n > 0)
    context_set_array_of_arrays(&ctx, "bangs", rows, n, inner);

  char *rendered = render_template("bangs.html", &ctx);
  if (rendered) {
    send_response(rendered);
    free(rendered);
  } else {
    send_status("500 Internal Server Error");
  }

  if (rows) {
    for (int i = 0; i < n; i++)
      free(rows[i]);
    free(rows);
  }
  free(inner);
  free_context(&ctx);
  return 0;
}
