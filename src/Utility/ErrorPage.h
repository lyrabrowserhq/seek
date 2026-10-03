#ifndef ERRORPAGE_H
#define ERRORPAGE_H

char *render_error_page(const char *heading, const char *detail,
                        const char *back_href);
char *render_suggestion_error_page(const char *heading,
                                   const char *did_you_mean_label,
                                   const char *suggestion,
                                   const char *suggest_href,
                                   const char *back_href);

#endif
