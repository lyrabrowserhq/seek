#include "Rank.h"
#include <ctype.h>
#include <stddef.h>
#include <strings.h>
#include <string.h>

static int host_ends(const char *url, const char *suffix) {
  if (!url || !suffix)
    return 0;
  const char *h = strstr(url, "://");
  h = h ? h + 3 : url;
  char buf[256];
  size_t n = 0;
  while (h[n] && h[n] != '/' && h[n] != ':' && n + 1 < sizeof(buf)) {
    buf[n] = h[n];
    n++;
  }
  buf[n] = '\0';
  size_t sl = strlen(suffix);
  if (n < sl)
    return 0;
  if (strcasecmp(buf + n - sl, suffix) != 0)
    return 0;
  return n == sl || buf[n - sl - 1] == '.';
}

int rank_query_allows_forums(const char *q) {
  if (!q || !q[0])
    return 0;
  return strcasestr(q, "site:reddit") != NULL ||
         strcasestr(q, "reddit.com") != NULL ||
         strcasestr(q, "lobste.rs") != NULL ||
         strcasestr(q, "lemmy") != NULL ||
         strcasestr(q, "news.ycombinator") != NULL ||
         strcasestr(q, "site:hn") != NULL;
}

double rank_host_weight(const char *url, int allow_forums) {
  if (host_ends(url, "wikipedia.org") || host_ends(url, "wikimedia.org") ||
      host_ends(url, "wiki.archlinux.org") || host_ends(url, "wiki.debian.org") ||
      host_ends(url, "wiki.gentoo.org") || host_ends(url, "mediawiki.org") ||
      host_ends(url, "wiki.nixos.org") || host_ends(url, "wiktionary.org") ||
      host_ends(url, "mdn.dev") || host_ends(url, "developer.mozilla.org"))
    return 3.0;
  if (host_ends(url, "docs.rs") || host_ends(url, "pkg.go.dev") ||
      host_ends(url, "man7.org") || host_ends(url, "kernel.org") ||
      host_ends(url, "rfc-editor.org") || host_ends(url, "cppreference.com") ||
      host_ends(url, "learn.microsoft.com") ||
      host_ends(url, "stackoverflow.com") || host_ends(url, "stackexchange.com") ||
      host_ends(url, "crates.io") || host_ends(url, "pypi.org") ||
      host_ends(url, "npmjs.com") || host_ends(url, "go.dev") ||
      host_ends(url, "doc.rust-lang.org") || host_ends(url, "docs.python.org") ||
      host_ends(url, "readthedocs.io") || host_ends(url, "devdocs.io") ||
      host_ends(url, "ziglang.org") || host_ends(url, "llvm.org") ||
      host_ends(url, "manpages.debian.org"))
    return 2.4;
  if (host_ends(url, "github.com") || host_ends(url, "gitlab.com") ||
      host_ends(url, "codeberg.org") || host_ends(url, "sr.ht") ||
      host_ends(url, "git.kernel.org") || host_ends(url, "sourcehut.org"))
    return 1.35;
  if (host_ends(url, "wiki.gg") || host_ends(url, "pcgamingwiki.com") ||
      host_ends(url, "minecraft.wiki") || host_ends(url, "strategywiki.org") ||
      host_ends(url, "tcrf.net"))
    return 2.2;
  if (host_ends(url, "news.ycombinator.com") || host_ends(url, "lobste.rs") ||
      host_ends(url, "lemmy.world") || host_ends(url, "reddit.com") ||
      host_ends(url, "redd.it")) {
    return allow_forums ? 1.2 : 0.15;
  }
  if (host_ends(url, "twitter.com") || host_ends(url, "x.com") ||
      host_ends(url, "facebook.com") || host_ends(url, "tiktok.com") ||
      host_ends(url, "instagram.com"))
    return allow_forums ? 0.4 : 0.02;
  return 1.0;
}

static void copy_lower(char *dst, size_t dst_sz, const char *src) {
  size_t i = 0;
  if (!dst || dst_sz == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  for (; src[i] && i + 1 < dst_sz; i++)
    dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = '\0';
}

static int hay_has(const char *hay, const char *tok) {
  return hay && tok && tok[0] && strstr(hay, tok) != NULL;
}

static int is_image_stop(const char *t) {
  static const char *s[] = {
      "a",       "an",     "the",     "of",      "and",     "or",
      "to",      "in",     "on",      "for",     "with",    "from",
      "image",   "images", "img",     "photo",   "photos",  "picture",
      "pictures", NULL};
  for (int i = 0; s[i]; i++) {
    if (strcmp(t, s[i]) == 0)
      return 1;
  }
  return 0;
}

int image_query_score(const char *query, const char *title, const char *page_url,
                      const char *image_url) {
  char q[256];
  char title_l[512];
  char page_l[768];
  char img_l[768];
  copy_lower(q, sizeof(q), query);
  copy_lower(title_l, sizeof(title_l), title);
  copy_lower(page_l, sizeof(page_l), page_url);
  copy_lower(img_l, sizeof(img_l), image_url);

  int score = 0;
  int tokens = 0;
  int hits = 0;
  int distinctive_miss = 0;
  char tok[64];
  size_t ti = 0;
  for (size_t i = 0;; i++) {
    unsigned char c = (unsigned char)q[i];
    int alnum = c && isalnum(c);
    if (alnum && ti + 1 < sizeof(tok)) {
      tok[ti++] = (char)c;
      continue;
    }
    if (ti > 0) {
      tok[ti] = '\0';
      if (ti > 1 && !is_image_stop(tok)) {
        tokens++;
        if (hay_has(title_l, tok)) {
          score += 14;
          hits++;
        } else if (hay_has(page_l, tok)) {
          score += 9;
          hits++;
        } else if (hay_has(img_l, tok)) {
          score += 7;
          hits++;
        } else if (ti >= 6) {
          score -= 18;
          distinctive_miss = 1;
        } else if (ti >= 3) {
          score -= 6;
        }
      }
      ti = 0;
    }
    if (!c)
      break;
  }
  if (tokens > 0 && hits == tokens)
    score += 16;
  if (distinctive_miss)
    score -= 10;

  static const char *memes[] = {
      "imgflip.com",
      "9gag.com",
      "knowyourmeme.com",
      "kym-cdn.com",
      "memegenerator.net",
      "makeameme.org",
      "memedroid.com",
      "icanhascheezburger.com",
      "cheezburger.com",
      "memebase.com",
      "livememe.com",
      "memecenter.com",
      "funnyjunk.com",
      "meme-arsenal.com",
      NULL};
  for (int i = 0; memes[i]; i++) {
    if (hay_has(page_l, memes[i]) || hay_has(img_l, memes[i])) {
      score -= 45;
      break;
    }
  }
  if (hay_has(page_l, "github.com") || hay_has(page_l, "gitlab.com") ||
      hay_has(page_l, "codeberg.org") || hay_has(page_l, "sr.ht"))
    score += 12;
  return score;
}

int image_query_has_distinctive_token(const char *query) {
  char q[256];
  copy_lower(q, sizeof(q), query);
  char tok[64];
  size_t ti = 0;
  for (size_t i = 0;; i++) {
    unsigned char c = (unsigned char)q[i];
    int alnum = c && isalnum(c);
    if (alnum && ti + 1 < sizeof(tok)) {
      tok[ti++] = (char)c;
      continue;
    }
    if (ti > 0) {
      tok[ti] = '\0';
      if (ti >= 5 && !is_image_stop(tok))
        return 1;
      ti = 0;
    }
    if (!c)
      break;
  }
  return 0;
}
