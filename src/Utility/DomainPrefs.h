#ifndef DOMAIN_PREFS_H
#define DOMAIN_PREFS_H

/* per-user domain ranking stored in the dprefs cookie.
 * cookie format: host:level+host:level with level in {p,r,l,b}
 * p=pin r=raise l=lower b=block */

double dprefs_url_weight(const char *url); /* 0.0 means blocked */
int dprefs_set(const char *host, const char *level);
char *dprefs_edit_text(void);
int dprefs_save_editor_text(const char *text);

#endif
