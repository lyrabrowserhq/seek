#include "Hdoc.h"

#include <lexbor/css/css.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>
#include <stdlib.h>
#include <string.h>

typedef struct Hdoc Hdoc;

struct Hnode {
  lxb_dom_node_t *n;
  Hdoc *doc;
};

struct Hdoc {
  lxb_html_document_t *doc;
  lxb_selectors_t *sel;
  /* pooled node wrappers - all Hnodes for a doc die with it */
  Hnode *pool;
  size_t pool_used;
  size_t pool_cap;
};

static Hnode *pool_node(Hdoc *d, lxb_dom_node_t *n) {
  if (!n)
    return NULL;
  if (d->pool_used == d->pool_cap) {
    size_t nc = d->pool_cap == 0 ? 128 : d->pool_cap * 2;
    Hnode *nb = realloc(d->pool, nc * sizeof(Hnode));
    if (!nb)
      return NULL;
    d->pool = nb;
    d->pool_cap = nc;
  }
  Hnode *w = &d->pool[d->pool_used++];
  w->n = n;
  w->doc = d;
  return w;
}

static lxb_status_t match_flag_cb(lxb_dom_node_t *node,
                                  lxb_css_selector_specificity_t spec,
                                  void *ctx) {
  (void)node;
  (void)spec;
  *(int *)ctx = 1;
  return LXB_STATUS_OK;
}

typedef struct {
  Hdoc *doc;
  HnodeList list;
  int cap;
} FindCtx;

static lxb_status_t find_cb(lxb_dom_node_t *node,
                            lxb_css_selector_specificity_t spec, void *ctx) {
  (void)spec;
  FindCtx *fc = ctx;
  if (fc->list.count == fc->cap) {
    int nc = fc->cap == 0 ? 32 : fc->cap * 2;
    Hnode **nb = realloc(fc->list.items, (size_t)nc * sizeof(Hnode *));
    if (!nb)
      return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    fc->list.items = nb;
    fc->cap = nc;
  }
  Hnode *w = pool_node(fc->doc, node);
  if (!w)
    return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
  fc->list.items[fc->list.count++] = w;
  return LXB_STATUS_OK;
}

Hdoc *hdoc_parse(const char *html, size_t len) {
  if (!html || len == 0)
    return NULL;

  Hdoc *d = calloc(1, sizeof(Hdoc));
  if (!d)
    return NULL;

  d->doc = lxb_html_document_create();
  if (!d->doc) {
    free(d);
    return NULL;
  }
  if (lxb_html_document_parse(d->doc, (const lxb_char_t *)html, len) !=
      LXB_STATUS_OK) {
    lxb_html_document_destroy(d->doc);
    free(d);
    return NULL;
  }

  d->sel = lxb_selectors_create();
  if (!d->sel || lxb_selectors_init(d->sel) != LXB_STATUS_OK) {
    lxb_html_document_destroy(d->doc);
    free(d);
    return NULL;
  }
  return d;
}

void hdoc_free(Hdoc *d) {
  if (!d)
    return;
  free(d->pool);
  if (d->sel)
    lxb_selectors_destroy(d->sel, true);
  if (d->doc)
    lxb_html_document_destroy(d->doc);
  free(d);
}

/* fresh css parser per query: selector lists are allocated from the
   parser's memory pool, so a shared parser would leak or corrupt across
   calls. the parser is tiny - a couple of small arrays */
HnodeList hdoc_select(Hdoc *d, Hnode *ctx, const char *css) {
  HnodeList out = {0};
  if (!d || !css || !*css)
    return out;

  lxb_css_parser_t *parser = lxb_css_parser_create();
  if (!parser || lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK) {
    if (parser)
      lxb_css_parser_destroy(parser, true);
    return out;
  }
  lxb_css_selector_list_t *list = lxb_css_selectors_parse(
      parser, (const lxb_char_t *)css, strlen(css));
  if (!list || parser->status != LXB_STATUS_OK) {
    lxb_css_parser_destroy(parser, true);
    return out;
  }

  lxb_dom_node_t *root =
      ctx ? ctx->n
          : lxb_dom_interface_node(lxb_html_document_body_element(d->doc));
  if (!root)
    root = lxb_dom_interface_node(d->doc);

  FindCtx fc = {.doc = d, .list = {0}, .cap = 0};
  lxb_status_t st = lxb_selectors_find(d->sel, root, list, find_cb, &fc);
  lxb_css_parser_destroy(parser, true);
  if (st != LXB_STATUS_OK && fc.list.count == 0) {
    free(fc.list.items);
    return out;
  }
  return fc.list;
}

void hlist_free(HnodeList *list) {
  if (!list)
    return;
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

Hnode *hdoc_find1(Hdoc *d, Hnode *ctx, const char *css) {
  HnodeList l = hdoc_select(d, ctx, css);
  Hnode *r = l.count > 0 ? l.items[0] : NULL;
  hlist_free(&l);
  return r;
}

static void collect_text(lxb_dom_node_t *n, char **buf, size_t *len,
                         size_t *cap) {
  if (!n)
    return;
  if (n->type == LXB_DOM_NODE_TYPE_TEXT ||
      n->type == LXB_DOM_NODE_TYPE_CDATA_SECTION) {
    lxb_dom_text_t *t = lxb_dom_interface_text(n);
    if (t && t->char_data.data.length > 0) {
      size_t add = t->char_data.data.length;
      if (*len + add + 1 > *cap) {
        size_t nc = *cap == 0 ? 256 : *cap;
        while (*len + add + 1 > nc)
          nc *= 2;
        char *nb = realloc(*buf, nc);
        if (!nb)
          return;
        *buf = nb;
        *cap = nc;
      }
      memcpy(*buf + *len, t->char_data.data.data, add);
      *len += add;
      (*buf)[*len] = ' ';
      (*len)++;
    }
    return;
  }
  for (lxb_dom_node_t *c = n->first_child; c; c = c->next)
    collect_text(c, buf, len, cap);
}

char *hnode_text(const Hnode *node) {
  if (!node || !node->n)
    return NULL;
  char *buf = NULL;
  size_t len = 0, cap = 0;
  collect_text(node->n, &buf, &len, &cap);
  if (!buf)
    return calloc(1, 1);
  if (len > 0)
    len--; /* drop trailing space */
  buf[len] = '\0';
  return buf;
}

char *hnode_attr(const Hnode *node, const char *name) {
  if (!node || !node->n || !name ||
      node->n->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return NULL;
  lxb_dom_element_t *el = lxb_dom_interface_element(node->n);
  if (!el)
    return NULL;
  size_t len = 0;
  const lxb_char_t *v = lxb_dom_element_get_attribute(
      el, (const lxb_char_t *)name, strlen(name), &len);
  if (!v)
    return NULL;
  char *out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, v, len);
  out[len] = '\0';
  return out;
}

static lxb_dom_node_t *walk_elem(lxb_dom_node_t *n, int dir) {
  while (n) {
    n = dir > 0 ? n->next : n->prev;
    if (n && n->type == LXB_DOM_NODE_TYPE_ELEMENT)
      return n;
  }
  return NULL;
}

Hnode *hnode_parent(const Hnode *node) {
  if (!node || !node->n || !node->doc)
    return NULL;
  lxb_dom_node_t *p = node->n->parent;
  while (p && p->type != LXB_DOM_NODE_TYPE_ELEMENT)
    p = p->parent;
  return pool_node(node->doc, p);
}

Hnode *hnode_next_sibling(const Hnode *node) {
  if (!node || !node->n || !node->doc)
    return NULL;
  return pool_node(node->doc, walk_elem(node->n, 1));
}

Hnode *hnode_prev_sibling(const Hnode *node) {
  if (!node || !node->n || !node->doc)
    return NULL;
  return pool_node(node->doc, walk_elem(node->n, -1));
}

Hnode *hnode_first_child(const Hnode *node) {
  if (!node || !node->n || !node->doc)
    return NULL;
  lxb_dom_node_t *c = node->n->first_child;
  while (c && c->type != LXB_DOM_NODE_TYPE_ELEMENT)
    c = c->next;
  return pool_node(node->doc, c);
}

Hnode *hnode_closest(Hdoc *d, const Hnode *node, const char *css) {
  if (!d || !node || !node->n || !css)
    return NULL;
  lxb_css_parser_t *parser = lxb_css_parser_create();
  if (!parser || lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK) {
    if (parser)
      lxb_css_parser_destroy(parser, true);
    return NULL;
  }
  lxb_css_selector_list_t *list = lxb_css_selectors_parse(
      parser, (const lxb_char_t *)css, strlen(css));
  if (!list || parser->status != LXB_STATUS_OK) {
    lxb_css_parser_destroy(parser, true);
    return NULL;
  }
  lxb_dom_node_t *cur = node->n;
  Hnode *found = NULL;
  while (cur) {
    if (cur->type == LXB_DOM_NODE_TYPE_ELEMENT) {
      int matched = 0;
      lxb_selectors_match_node(d->sel, cur, list, match_flag_cb, &matched);
      if (matched) {
        found = pool_node(d, cur);
        break;
      }
    }
    cur = cur->parent;
  }
  lxb_css_parser_destroy(parser, true);
  return found;
}

const char *hnode_tag(const Hnode *node) {
  if (!node || !node->n || node->n->type != LXB_DOM_NODE_TYPE_ELEMENT)
    return "";
  size_t len = 0;
  const lxb_char_t *name = lxb_dom_node_name(node->n, &len);
  return (const char *)name;
}
