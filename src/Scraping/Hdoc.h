#ifndef HDOC_H
#define HDOC_H

#include <stddef.h>

/* thin DOM wrapper over lexbor for engine parsers. xpath-style queries
   become CSS selectors run under an optional context node */

typedef struct Hdoc Hdoc;
typedef struct Hnode Hnode;

typedef struct {
  Hnode **items;
  int count;
} HnodeList;

Hdoc *hdoc_parse(const char *html, size_t len);
void hdoc_free(Hdoc *doc);

/* select nodes matching css under ctx (NULL = document root).
   caller frees the returned list with hlist_free */
HnodeList hdoc_select(Hdoc *doc, Hnode *ctx, const char *css);
void hlist_free(HnodeList *list);

/* first match or NULL */
Hnode *hdoc_find1(Hdoc *doc, Hnode *ctx, const char *css);

/* concatenated text of all descendant text nodes - caller frees */
char *hnode_text(const Hnode *node);

/* attribute value - caller frees, NULL when absent */
char *hnode_attr(const Hnode *node, const char *name);

/* dom navigation - element siblings/ancestors only */
Hnode *hnode_parent(const Hnode *node);
Hnode *hnode_next_sibling(const Hnode *node);
Hnode *hnode_prev_sibling(const Hnode *node);
Hnode *hnode_first_child(const Hnode *node);

/* closest ancestor matching css (walks up); NULL when none */
Hnode *hnode_closest(Hdoc *doc, const Hnode *node, const char *css);

/* lowercase tag name - borrowed pointer, do not free */
const char *hnode_tag(const Hnode *node);

#endif
