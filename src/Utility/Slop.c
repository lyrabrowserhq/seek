#include "Slop.h"
#include "SlopDetect.h"
#include <beaker.h>
#include <stdlib.h>
#include <string.h>

int slop_cookie_mode(void) {
  char *c = get_cookie("slop");
  int m = SLOP_OFF;
  if (c) {
    if (strcmp(c, "hide") == 0)
      m = SLOP_HIDE;
    else if (strcmp(c, "derank") == 0)
      m = SLOP_DERANK;
    else if (strcmp(c, "score") == 0)
      m = SLOP_SCORE;
  }
  free(c);
  return m;
}

int slop_score_pages(const char **titles, const char **snips, int n,
                     double *out) {
  if (!out || n <= 0)
    return -1;
  for (int i = 0; i < n; i++) {
    out[i] = slop_detect_page(titles ? titles[i] : NULL,
                              snips ? snips[i] : NULL, NULL);
  }
  return n;
}
