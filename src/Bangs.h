#ifndef BANGS_H
#define BANGS_H

#define BANG_NAME_LEN 48
#define BANG_TMPL_LEN 768

typedef struct {
  char name[BANG_NAME_LEN];
  char tmpl[BANG_TMPL_LEN];
} BangEntry;

void bangs_init(const char *optional_config_path);
char *bang_resolve_redirect_url(const char *query);
int bangs_count(void);
const BangEntry *bangs_at(int index);

#endif
