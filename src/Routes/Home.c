#include "Home.h"
#include "../Utility/Utility.h"
#include <stdlib.h>

int home_handler(UrlParams *params) {
  (void)params;
  TemplateContext ctx = new_context();
  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  free(locale);
  char *rendered_html = render_template("home.html", &ctx);
  send_response(rendered_html);

  free(rendered_html);
  free_context(&ctx);

  return 0;
}
