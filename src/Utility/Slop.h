#ifndef SLOP_H
#define SLOP_H

#define SLOP_OFF 0
#define SLOP_HIDE 1
#define SLOP_DERANK 2
#define SLOP_SCORE 3

int slop_cookie_mode(void);
int slop_score_pages(const char **titles, const char **snips, int n,
                     double *out);

#endif
