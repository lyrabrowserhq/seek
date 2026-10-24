#include "Rank.h"
#include "../../beaker/beaker.h"
#include <ctype.h>

/* weak stub so unit tests can link Rank.o without beaker; the real
   implementation in libbeaker wins when both are present */
__attribute__((weak)) char *get_cookie(const char *name) {
  (void)name;
  return NULL;
}
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

static int url_is_wiki(const char *url) {
  return host_ends(url, "wikipedia.org") || host_ends(url, "wikimedia.org") ||
         host_ends(url, "wiki.archlinux.org") ||
         host_ends(url, "wiki.debian.org") ||
         host_ends(url, "wiki.gentoo.org") || host_ends(url, "mediawiki.org") ||
         host_ends(url, "wiki.nixos.org") || host_ends(url, "wiktionary.org") ||
         host_ends(url, "wikibooks.org") ||
         host_ends(url, "wikiquote.org") || host_ends(url, "wikisource.org") ||
         host_ends(url, "wikivoyage.org") || host_ends(url, "wikiversity.org");
}

static int url_is_gamewiki(const char *url) {
  return host_ends(url, "wiki.gg") || host_ends(url, "pcgamingwiki.com") ||
         host_ends(url, "minecraft.wiki") || host_ends(url, "strategywiki.org") ||
         host_ends(url, "tcrf.net");
}

static int url_is_docs(const char *url) {
  return host_ends(url, "docs.rs") || host_ends(url, "pkg.go.dev") ||
         host_ends(url, "man7.org") || host_ends(url, "kernel.org") ||
         host_ends(url, "rfc-editor.org") || host_ends(url, "cppreference.com") ||
         host_ends(url, "learn.microsoft.com") ||
         host_ends(url, "stackoverflow.com") ||
         host_ends(url, "stackexchange.com") || host_ends(url, "crates.io") ||
         host_ends(url, "pypi.org") || host_ends(url, "npmjs.com") ||
         host_ends(url, "go.dev") || host_ends(url, "doc.rust-lang.org") ||
         host_ends(url, "docs.python.org") || host_ends(url, "readthedocs.io") ||
         host_ends(url, "devdocs.io") || host_ends(url, "ziglang.org") ||
         host_ends(url, "llvm.org") || host_ends(url, "manpages.debian.org") ||
         host_ends(url, "developer.mozilla.org") || host_ends(url, "mdn.dev") ||
         host_ends(url, "ietf.org") || host_ends(url, "w3.org") ||
         host_ends(url, "gnu.org") || host_ends(url, "postgresql.org") ||
         host_ends(url, "sqlite.org") || host_ends(url, "apache.org");
}

static int url_is_forge(const char *url) {
  return host_ends(url, "github.com") || host_ends(url, "gitlab.com") ||
         host_ends(url, "codeberg.org") || host_ends(url, "sr.ht") ||
         host_ends(url, "git.kernel.org") || host_ends(url, "sourcehut.org") ||
         host_ends(url, "bitbucket.org");
}

static int host_starts(const char *url, const char *prefix) {
  if (!url || !prefix)
    return 0;
  const char *h = strstr(url, "://");
  h = h ? h + 3 : url;
  return strncasecmp(h, prefix, strlen(prefix)) == 0;
}

static int url_is_forum(const char *url) {
  /* common subdomain prefixes for forum software installs */
  static const char *prefixes[] = {"forums.", "forum.",    "discuss.",
                                   "discourse.", "community.", "bbs.",
                                   "lemmy.", "boards.",   "talk.",
                                   "fora.",   "meta.",    "lists."};
  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    if (host_starts(url, prefixes[i]))
      return 1;
  /* path-shaped forums - many installs live at /forum /topic /thread on an
     otherwise ordinary host, so check the path too. includes the classic
     software routes: phpbb viewtopic, vbulletin showthread, xenforo
     /threads, discourse /t and /c, smf index.php?topic, mybb /thread-,
     vanilla /discussion, pipermail archives */
  const char *path = strchr(strstr(url, "://") ? strstr(url, "://") + 3 : url,
                            '/');
  if (path) {
    /* top-level routes - cheap prefix check */
    if (strncasecmp(path, "/forum", 6) == 0 ||
        strncasecmp(path, "/post/", 6) == 0 ||
        strncasecmp(path, "/c/", 3) == 0 ||
        strncasecmp(path, "/t/", 3) == 0)
      return 1;
    /* nested routes - software may live under /community, /boards etc so
       substring-check the distinctive script and path names instead */
    if (strstr(path, "viewtopic.php") || strstr(path, "viewforum.php") ||
        strstr(path, "showthread.php") || strstr(path, "forumdisplay.php") ||
        strstr(path, "index.php?topic=") || strstr(path, "index.php/topic/") ||
        strstr(path, "/threads/") || strstr(path, "/topic/") ||
        strstr(path, "/topics/") || strstr(path, "/discussion/") ||
        strstr(path, "/pipermail/") || strstr(path, "/thread-"))
      return 1;
  }
  return host_ends(url, "news.ycombinator.com") || host_ends(url, "lobste.rs") ||
         host_ends(url, "lemmy.world") || host_ends(url, "lemmy.ml") ||
         host_ends(url, "lemmings.world") || host_ends(url, "sh.itjust.works") ||
         host_ends(url, "beehaw.org") || host_ends(url, "kbin.social") ||
         host_ends(url, "facepunch.com") || host_ends(url, "resetera.com") ||
         host_ends(url, "old.reddit.com") || host_ends(url, "reddit.com") ||
         host_ends(url, "redd.it") || host_ends(url, "discourse.org") ||
         host_ends(url, "stackexchange.com") || host_ends(url, "steamcommunity.com") ||
         /* hosted forum platforms and other big board communities */
         host_ends(url, "proboards.com") || host_ends(url, "forumotion.com") ||
         host_ends(url, "tapatalk.com") || host_ends(url, "boards.net") ||
         host_ends(url, "freeforums.net") || host_ends(url, "createaforum.com") ||
         host_ends(url, "jeuxvideo.com") || host_ends(url, "gamefaqs.gamespot.com") ||
         host_ends(url, "f95zone.to") || host_ends(url, "vogons.org") ||
         host_ends(url, "xda-developers.com") || host_ends(url, "quora.com") ||
         host_ends(url, "answers.com") || host_ends(url, "nsaneforums.com") ||
         host_ends(url, "wilderssecurity.com") || host_ends(url, "forums.mozillazine.org") ||
         /* mailing list archives */
         host_ends(url, "marc.info") || host_ends(url, "lore.kernel.org") ||
         host_ends(url, "mail-archive.com") || host_ends(url, "nabble.com") ||
         host_ends(url, "groups.google.com") || host_ends(url, "gmane.io");
}

static int url_is_social(const char *url) {
  return host_ends(url, "twitter.com") || host_ends(url, "x.com") ||
         host_ends(url, "facebook.com") || host_ends(url, "tiktok.com") ||
         host_ends(url, "instagram.com") || host_ends(url, "threads.net") ||
         host_ends(url, "bsky.app");
}

double rank_host_weight(const char *url, int allow_forums) {
  if (url_is_wiki(url) || host_ends(url, "mdn.dev") ||
      host_ends(url, "developer.mozilla.org"))
    return 3.0;
  if (url_is_docs(url))
    return 2.4;
  if (url_is_forge(url))
    return 1.35;
  if (url_is_gamewiki(url))
    return 2.2;
  if (url_is_forum(url)) {
    return allow_forums ? 1.2 : 0.15;
  }
  if (url_is_social(url))
    return allow_forums ? 0.4 : 0.02;
  return 1.0;
}

static int url_is_paper(const char *url) {
  return host_ends(url, "arxiv.org") || host_ends(url, "biorxiv.org") ||
         host_ends(url, "medrxiv.org") || host_ends(url, "chemrxiv.org") ||
         host_ends(url, "doi.org") || host_ends(url, "semanticscholar.org") ||
         host_ends(url, "pubmed.ncbi.nlm.nih.gov") ||
         host_ends(url, "ncbi.nlm.nih.gov") || host_ends(url, "europepmc.org") ||
         host_ends(url, "openalex.org") || host_ends(url, "nature.com") ||
         host_ends(url, "science.org") || host_ends(url, "sciencedirect.com") ||
         host_ends(url, "springer.com") || host_ends(url, "ieeexplore.ieee.org") ||
         host_ends(url, "dl.acm.org") || host_ends(url, "plos.org") ||
         host_ends(url, "jstor.org") || host_ends(url, "ssrn.com") ||
         host_ends(url, "frontiersin.org") || host_ends(url, "mdpi.com") ||
         host_ends(url, "cell.com") || host_ends(url, "thelancet.com") ||
         host_ends(url, "nejm.org") || host_ends(url, "bmj.com") ||
         host_ends(url, "usenix.org") || host_ends(url, "aclanthology.org") ||
         host_ends(url, "openreview.net") || host_ends(url, "proceedings.neurips.cc") ||
         host_ends(url, "thecvf.com") || host_ends(url, "annualreviews.org") ||
         host_ends(url, "researchgate.net") || host_ends(url, "scholar.google.com");
}

static int url_is_recipe(const char *url) {
  return host_ends(url, "allrecipes.com") || host_ends(url, "seriouseats.com") ||
         host_ends(url, "foodnetwork.com") || host_ends(url, "bbcgoodfood.com") ||
         host_ends(url, "bonappetit.com") || host_ends(url, "epicurious.com") ||
         host_ends(url, "delish.com") || host_ends(url, "tasty.co") ||
         host_ends(url, "simplyrecipes.com") || host_ends(url, "food52.com") ||
         host_ends(url, "kingarthurbaking.com") || host_ends(url, "themealdb.com") ||
         host_ends(url, "recipetineats.com") || host_ends(url, "budgetbytes.com") ||
         host_ends(url, "minimalistbaker.com") || host_ends(url, "smittenkitchen.com") ||
         host_ends(url, "thepioneerwoman.com") || host_ends(url, "cookpad.com") ||
         host_ends(url, "nytimes.com/cooking") || host_ends(url, "skinnytaste.com") ||
         host_ends(url, "loveandlemons.com") || host_ends(url, "halfbakedharvest.com") ||
         host_ends(url, "damndelicious.net") || host_ends(url, "pinchofyum.com");
}

static int url_is_book(const char *url) {
  return host_ends(url, "openlibrary.org") || host_ends(url, "goodreads.com") ||
         host_ends(url, "books.google.com") || host_ends(url, "gutenberg.org") ||
         host_ends(url, "worldcat.org") || host_ends(url, "librarything.com") ||
         host_ends(url, "bookshop.org") || host_ends(url, "standardebooks.org") ||
         host_ends(url, "barnesandnoble.com") || host_ends(url, "amazon.") ||
         host_ends(url, "archive.org") || host_ends(url, "hathitrust.org") ||
         host_ends(url, "isfdb.org") || host_ends(url, "fantasticfiction.com") ||
         host_ends(url, "librivox.org") || host_ends(url, "audible.com") ||
         host_ends(url, "storygraph.com") || host_ends(url, "books.apple.com");
}

static int url_is_podcast(const char *url) {
  return host_ends(url, "podcasts.apple.com") ||
         host_ends(url, "itunes.apple.com") ||
         host_ends(url, "open.spotify.com") || host_ends(url, "spotify.com") ||
         host_ends(url, "podcastindex.org") || host_ends(url, "listennotes.com") ||
         host_ends(url, "podtail.com") || host_ends(url, "podbay.fm") ||
         host_ends(url, "castbox.fm") || host_ends(url, "overcast.fm") ||
         host_ends(url, "pca.st") || host_ends(url, "pocketcasts.com") ||
         host_ends(url, "player.fm") || host_ends(url, "podchaser.com") ||
         host_ends(url, "audible.com") || host_ends(url, "librivox.org") ||
         host_ends(url, "radiopublic.com") || host_ends(url, "stitcher.com");
}

static int url_is_shopping(const char *url) {
  return host_ends(url, "amazon.") || host_ends(url, "ebay.") ||
         host_ends(url, "etsy.com") || host_ends(url, "walmart.com") ||
         host_ends(url, "target.com") || host_ends(url, "bestbuy.com") ||
         host_ends(url, "aliexpress.com") || host_ends(url, "newegg.com") ||
         host_ends(url, "homedepot.com") || host_ends(url, "lowes.com") ||
         host_ends(url, "ikea.com") || host_ends(url, "wayfair.com") ||
         host_ends(url, "costco.com") || host_ends(url, "bhphotovideo.com") ||
         host_ends(url, "microcenter.com") || host_ends(url, "craigslist.org") ||
         host_ends(url, "shopify.com") || host_ends(url, "slickdeals.net") ||
         host_ends(url, "camelcamelcamel.com") || host_ends(url, "keepa.com");
}

static int url_is_map(const char *url) {
  return host_ends(url, "openstreetmap.org") || host_ends(url, "osm.org") ||
         host_ends(url, "maps.google.com") || host_ends(url, "google.com/maps") ||
         host_ends(url, "bing.com/maps") || host_ends(url, "waze.com") ||
         host_ends(url, "mapbox.com") || host_ends(url, "here.com") ||
         host_ends(url, "mapquest.com") || host_ends(url, "nominatim") ||
         host_ends(url, "yelp.com") || host_ends(url, "tripadvisor.") ||
         host_ends(url, "foursquare.com") || host_ends(url, "openstreetmap.fr") ||
         host_ends(url, "organicmaps.app") || host_ends(url, "magic-earth.com");
}

static int url_is_video(const char *url) {
  return host_ends(url, "youtube.com") || host_ends(url, "youtu.be") ||
         host_ends(url, "vimeo.com") || host_ends(url, "dailymotion.com") ||
         host_ends(url, "peertube") || host_ends(url, "tube.") ||
         host_ends(url, "video.") || host_ends(url, "videos.") ||
         host_ends(url, "nebula.tv") || host_ends(url, "twitch.tv") ||
         host_ends(url, "odysee.com") || host_ends(url, "rumble.com") ||
         host_ends(url, "bilibili.com") || host_ends(url, "veoh.com") ||
         host_ends(url, "archive.org/details") || host_ends(url, "tiktok.com");
}

static int url_is_news(const char *url) {
  return host_ends(url, "bbc.") || host_ends(url, "reuters.com") ||
         host_ends(url, "apnews.com") || host_ends(url, "theguardian.com") ||
         host_ends(url, "nytimes.com") || host_ends(url, "npr.org") ||
         host_ends(url, "aljazeera.com") || host_ends(url, "cnn.com") ||
         host_ends(url, "bloomberg.com") || host_ends(url, "wsj.com") ||
         host_ends(url, "washingtonpost.com") || host_ends(url, "politico.com") ||
         host_ends(url, "axios.com") || host_ends(url, "ft.com") ||
         host_ends(url, "economist.com") || host_ends(url, "abcnews.go.com") ||
         host_ends(url, "cbsnews.com") || host_ends(url, "nbcnews.com") ||
         host_ends(url, "usatoday.com") || host_ends(url, "ap.org");
}

/* engines worth firing when a lens is active - the union makes a lens
   produce vertical results even from engines the query did not name */
static const char *const LENS_ENGINES_FORUMS[] = {"lemmy", "lobsters", "reddit", "hn", "stackoverflow"};
static const char *const LENS_ENGINES_DOCS[] = {"stackoverflow", "mediawiki", "nixos", "gentoo", "archwiki", "debianwiki"};
static const char *const LENS_ENGINES_WIKI[] = {"wikipedia", "wiktionary", "wikibooks", "wikiquote"};
static const char *const LENS_ENGINES_PAPERS[] = {"arxiv", "s2", "openalex", "europepmc"};
static const char *const LENS_ENGINES_RECIPES[] = {"themealdb"};
static const char *const LENS_ENGINES_BOOKS[] = {"openlibrary", "gbooks"};
static const char *const LENS_ENGINES_PODCASTS[] = {"itunes"};
static const char *const LENS_ENGINES_MAPS[] = {"osm"};
static const char *const LENS_ENGINES_VIDEOS[] = {"peertube"};

const char *const *lens_engines(const char *lens, int *count) {
  *count = 0;
  if (!lens)
    return NULL;
#define L(l, a) if (!strcmp(lens, l)) { *count = (int)(sizeof(a) / sizeof((a)[0])); return a; }
  L("forums", LENS_ENGINES_FORUMS)
  L("docs", LENS_ENGINES_DOCS)
  L("wiki", LENS_ENGINES_WIKI)
  L("papers", LENS_ENGINES_PAPERS)
  L("recipes", LENS_ENGINES_RECIPES)
  L("books", LENS_ENGINES_BOOKS)
  L("podcasts", LENS_ENGINES_PODCASTS)
  L("maps", LENS_ENGINES_MAPS)
  L("videos", LENS_ENGINES_VIDEOS)
#undef L
  return NULL;
}

int lens_is_valid(const char *lens) {
  return lens && (!strcmp(lens, "forums") || !strcmp(lens, "docs") ||
                  !strcmp(lens, "wiki") || !strcmp(lens, "papers") ||
                  !strcmp(lens, "recipes") || !strcmp(lens, "books") ||
                  !strcmp(lens, "podcasts") || !strcmp(lens, "shopping") ||
                  !strcmp(lens, "news") || !strcmp(lens, "maps") ||
                  !strcmp(lens, "videos") || !strcmp(lens, "custom"));
}

/* user-defined lens: ulens cookie holds comma-separated host suffixes set
   from settings. any suffix match boosts, nothing listed demotes */
static double custom_lens_weight(const char *url) {
  char *cookie = get_cookie("ulens");
  if (!cookie || !cookie[0]) {
    free(cookie);
    return 1.0;
  }
  char buf[2048];
  snprintf(buf, sizeof(buf), "%s", cookie);
  double w = 0.2;
  for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    if (!tok)
      break;
    while (*tok == ' ' || *tok == '.')
      tok++;
    if (!tok[0])
      continue;
    /* split the ','-separated list; dots stay so subdomains match */
    char entry[128];
    snprintf(entry, sizeof(entry), "%s", tok);
    char *dot = strchr(entry, ',');
    if (dot)
      *dot = '\0';
    if (entry[0] && host_ends(url, entry)) {
      w = 4.0;
      break;
    }
  }
  free(cookie);
  return w;
}

int lens_forces_forums(const char *lens) {
  return lens && !strcmp(lens, "forums");
}

/* lens = a curated domain profile applied on top of the base rank.
 * matching hosts are boosted, everything else is pushed down hard but not
 * removed so the lens still returns results when few pages match */
double lens_host_weight(const char *url, const char *lens) {
  if (!lens_is_valid(lens))
    return 1.0;
  if (!strcmp(lens, "forums")) {
    if (url_is_forum(url) || url_is_social(url))
      return 3.5;
    return 0.2;
  }
  if (!strcmp(lens, "docs")) {
    if (url_is_docs(url) || url_is_forge(url))
      return 3.0;
    return 0.35;
  }
  if (!strcmp(lens, "wiki")) {
    if (url_is_wiki(url) || url_is_gamewiki(url))
      return 3.0;
    return 0.35;
  }
  if (!strcmp(lens, "papers")) {
    if (url_is_paper(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "recipes")) {
    if (url_is_recipe(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "books")) {
    if (url_is_book(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "podcasts")) {
    if (url_is_podcast(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "shopping")) {
    if (url_is_shopping(url))
      return 3.5;
    return 0.2;
  }
  if (!strcmp(lens, "news")) {
    if (url_is_news(url))
      return 3.0;
    return 0.3;
  }
  if (!strcmp(lens, "maps")) {
    if (url_is_map(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "videos")) {
    if (url_is_video(url))
      return 3.5;
    return 0.25;
  }
  if (!strcmp(lens, "custom"))
    return custom_lens_weight(url);
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
