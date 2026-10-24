#include "ImageScraping.h"
#include "../Cache/Cache.h"
#include "../Routes/ImageProxy.h"
#include "../Utility/HttpClient.h"
#include "../Utility/Rank.h"
#include "Config.h"
#include "Hdoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *encode_image_query(CURL *tmp, const char *query) {
  if (!tmp || !query)
    return NULL;
  if (image_query_has_distinctive_token(query) && strchr(query, ' ') == NULL &&
      strchr(query, '\t') == NULL) {
    char quoted[280];
    snprintf(quoted, sizeof(quoted), "\"%s\"", query);
    return curl_easy_escape(tmp, quoted, 0);
  }
  return curl_easy_escape(tmp, query, 0);
}

static int parse_image_node(Hdoc *doc, Hnode *node, ImageResult *result) {
  /* gigablast item markup: a.thumb > div.cico > img for the thumbnail,
     a.tit for the title/page link, div.meta > div.des for the blurb */
  Hnode *thumb_link = hdoc_find1(doc, node, "a[class*='thumb']");
  Hnode *img = hdoc_find1(doc, thumb_link, "div.cico img");
  if (!img)
    img = hdoc_find1(doc, node, "a[class*='thumb'] img");

  Hnode *tit = hdoc_find1(doc, node, "a[class*='tit']");
  if (!tit)
    tit = hdoc_find1(doc, node, "div.meta a[class*='tit']");
  Hnode *des = hdoc_find1(doc, node, "div.meta div.des");
  if (!des)
    des = hdoc_find1(doc, node, "div.des");

  char *iurl = img ? hnode_attr(img, "src") : NULL;
  char *full_url = thumb_link ? hnode_attr(thumb_link, "href") : NULL;
  char *title = des ? hnode_text(des) : (tit ? hnode_text(tit) : NULL);
  char *rurl = tit ? hnode_attr(tit, "href") : NULL;

  if (!iurl || !*iurl) {
    free(iurl);
    free(title);
    free(rurl);
    free(full_url);
    return 0;
  }

  char *proxy_url = proxy_wrap_image_url(iurl);
  result->thumbnail_url = proxy_url ? proxy_url : strdup(iurl);
  result->title = strdup(title ? title : "Image");
  result->page_url = strdup(rurl ? rurl : "#");
  result->full_url = strdup(full_url ? full_url : "#");

  free(iurl);
  free(title);
  free(rurl);
  free(full_url);
  return 1;
}

static int scrape_images_bing(const char *query, int page,
                              const ImageFilters *filters,
                              ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;

  if (!query || strlen(query) == 0)
    return -1;

  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;

  char *encoded_query = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);

  if (!encoded_query)
    return -1;

  char url[BUFFER_SIZE_XLARGE];
  int first = (page - 1) * IMAGE_RESULTS_PER_PAGE + 1;
  snprintf(url, sizeof(url), "%s?q=%s&first=%d", BING_IMAGE_URL, encoded_query,
           first);
  if (filters && image_filters_append_to_url(filters, url, sizeof(url)) != 0) {
    free(encoded_query);
    return -1;
  }
  free(encoded_query);

  HttpResponse resp = {0};
  char *cache_key = cache_compute_key(query, page, url);
  int loaded_from_cache = 0;

  if (cache_key && get_cache_ttl_image() > 0) {
    if (cache_get(cache_key, (time_t)get_cache_ttl_image(), &resp.memory,
                  &resp.size) == 0 &&
        resp.memory) {
      loaded_from_cache = 1;
    }
  }

  if (!loaded_from_cache) {
    resp = http_get(
        url,
        "Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; rv:11.0) like Gecko");
    if (resp.memory && cache_key && get_cache_ttl_image() > 0) {
      cache_set(cache_key, resp.memory, resp.size);
    }
  }
  free(cache_key);

  if (!resp.memory) {
    return -1;
  }

  Hdoc *doc = hdoc_parse(resp.memory, resp.size);
  if (!doc) {
    http_response_free(&resp);
    return -1;
  }

  HnodeList items = hdoc_select(doc, NULL, "div.item");

  int max_images =
      (items.count < IMAGE_RESULTS_PER_PAGE) ? items.count : IMAGE_RESULTS_PER_PAGE;
  if (max_images <= 0) {
    hlist_free(&items);
    hdoc_free(doc);
    http_response_free(&resp);
    return 0;
  }

  ImageResult *results = malloc(sizeof(ImageResult) * max_images);
  if (!results) {
    hlist_free(&items);
    hdoc_free(doc);
    http_response_free(&resp);
    return -1;
  }

  int count = 0;
  for (int i = 0; i < items.count && count < IMAGE_RESULTS_PER_PAGE; i++) {
    if (parse_image_node(doc, items.items[i], &results[count])) {
      count++;
    }
  }

  hlist_free(&items);
  hdoc_free(doc);
  http_response_free(&resp);

  *out_results = results;
  *out_count = count;
  return 0;
}

/* Minimal JSON string-field extractor: scans forward for "key":"value" and
   returns the unescaped value, advancing cursor past it. */
static char *ov_find_string(const char **cursor, const char *key) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(*cursor, pat);
  if (!p)
    return NULL;
  p += strlen(pat);
  while (*p == ' ' || *p == ':')
    p++;
  if (*p != '"') {
    *cursor = p;
    return NULL;
  }
  p++;
  const char *v = p;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1])
      p++;
    p++;
  }
  size_t len = p - v;
  char *out = malloc(len + 1);
  if (!out) {
    *cursor = p;
    return NULL;
  }
  char *w = out;
  for (size_t i = 0; i < len; i++) {
    if (v[i] == '\\' && i + 1 < len) {
      i++;
      switch (v[i]) {
      case 'n':
        *w++ = '\n';
        break;
      case 'r':
        *w++ = '\r';
        break;
      case 't':
        *w++ = '\t';
        break;
      default:
        *w++ = v[i];
        break;
      }
    } else {
      *w++ = v[i];
    }
  }
  *w = '\0';
  *cursor = p + 1;
  return out;
}

static int scrape_images_openverse(const char *query, int page,
                                   ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;

  if (!query || strlen(query) == 0)
    return -1;

  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;
  char *encoded_query = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);
  if (!encoded_query)
    return -1;

  char url[BUFFER_SIZE_XLARGE];
  snprintf(url, sizeof(url),
           "https://api.openverse.org/v1/images/?q=%s&page=%d&page_size=%d",
           encoded_query, page, IMAGE_RESULTS_PER_PAGE);
  free(encoded_query);

  HttpResponse resp = http_get(url,
      "Seek/2.0 (+https://seek.lyrabrowser.com)");
  if (!resp.memory)
    return -1;

  int cap = IMAGE_RESULTS_PER_PAGE;
  ImageResult *results = malloc(sizeof(ImageResult) * cap);
  if (!results) {
    http_response_free(&resp);
    return -1;
  }

  int count = 0;
  const char *cursor = strstr(resp.memory, "\"results\"");
  while (cursor && count < cap) {
    const char *item = strstr(cursor, "{\"id\"");
    if (!item)
      break;
    const char *next = strstr(item + 1, "{\"id\"");
    if (!next)
      next = item + strlen(item);

    char *title = NULL;
    char *full = NULL;
    char *thumb = NULL;
    char *landing = NULL;
    const char *c = item;
    while (c < next && (!title || !full || !landing)) {
      const char *k = memchr(c, '"', (size_t)(next - c));
      if (!k)
        break;
      /* peek at key name */
      if (strncmp(k, "\"title\"", 7) == 0) {
        c = k;
        title = ov_find_string(&c, "title");
      } else if (strncmp(k, "\"url\"", 5) == 0) {
        c = k;
        char *v = ov_find_string(&c, "url");
        if (v && !full)
          full = v;
        else
          free(v);
      } else if (strncmp(k, "\"thumbnail\"", 11) == 0) {
        c = k;
        thumb = ov_find_string(&c, "thumbnail");
      } else if (strncmp(k, "\"foreign_landing_url\"", 22) == 0) {
        c = k;
        landing = ov_find_string(&c, "foreign_landing_url");
      } else {
        c = k + 1;
      }
    }

    if (full && landing) {
      char *proxy_thumb = proxy_wrap_image_url(thumb ? thumb : full);
      results[count].thumbnail_url =
          proxy_thumb ? proxy_thumb : strdup(thumb ? thumb : full);
      results[count].title = title ? title : strdup("Image");
      results[count].page_url = landing;
      results[count].full_url = full;
      free(thumb);
      count++;
    } else {
      free(title);
      free(full);
      free(thumb);
      free(landing);
    }
    cursor = next;
  }

  http_response_free(&resp);

  if (count == 0) {
    free(results);
    return -1;
  }

  *out_results = results;
  *out_count = count;
  return 0;
}

static int image_url_equals(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  return strcmp(a, b) == 0;
}

static int scrape_images_commons(const char *query, int page,
                                 ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;
  if (!query || !query[0])
    return -1;
  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;
  char *encoded = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);
  if (!encoded)
    return -1;
  char url[BUFFER_SIZE_XLARGE];
  int offset = (page - 1) * IMAGE_RESULTS_PER_PAGE;
  if (offset < 0)
    offset = 0;
  snprintf(url, sizeof(url),
           "https://commons.wikimedia.org/w/api.php?action=query&format=json"
           "&generator=search&gsrsearch=%s&gsrnamespace=6&gsrlimit=%d"
           "&gsroffset=%d&prop=imageinfo&iiprop=url|size&iiurlwidth=320",
           encoded, IMAGE_RESULTS_PER_PAGE, offset);
  free(encoded);

  HttpResponse resp = http_get(url, "Seek/1.0 (https://seek.lyrabrowser.com)");
  if (!resp.memory)
    return -1;

  int cap = IMAGE_RESULTS_PER_PAGE;
  ImageResult *results = malloc(sizeof(ImageResult) * cap);
  if (!results) {
    http_response_free(&resp);
    return -1;
  }
  int count = 0;
  const char *p = resp.memory;
  while (p && count < cap) {
    const char *titlek = strstr(p, "\"title\"");
    if (!titlek)
      break;
    const char *c = titlek;
    char *title = ov_find_string(&c, "title");
    const char *thumbk = strstr(c, "\"thumburl\"");
    const char *urlk = strstr(c, "\"url\"");
    char *thumb = NULL;
    char *full = NULL;
    if (thumbk && (!urlk || thumbk < urlk + 2000)) {
      const char *t = thumbk;
      thumb = ov_find_string(&t, "thumburl");
    }
    if (urlk) {
      const char *u = urlk;
      full = ov_find_string(&u, "url");
    }
    const char *desc = strstr(c, "\"descriptionurl\"");
    char *pageu = NULL;
    if (desc) {
      const char *d = desc;
      pageu = ov_find_string(&d, "descriptionurl");
    }
    if (full && full[0]) {
      char *proxy_thumb = proxy_wrap_image_url(thumb ? thumb : full);
      results[count].thumbnail_url =
          proxy_thumb ? proxy_thumb : strdup(thumb ? thumb : full);
      results[count].title = title ? title : strdup("Image");
      title = NULL;
      results[count].page_url = pageu ? pageu : strdup(full);
      pageu = NULL;
      results[count].full_url = full;
      full = NULL;
      count++;
    }
    free(title);
    free(thumb);
    free(full);
    free(pageu);
    p = c + 1;
  }
  http_response_free(&resp);
  if (count == 0) {
    free(results);
    return -1;
  }
  *out_results = results;
  *out_count = count;
  return 0;
}

static void drop_image(ImageResult *r) {
  free(r->thumbnail_url);
  free(r->title);
  free(r->page_url);
  free(r->full_url);
}

static int scrape_images_qwant(const char *query, int page,
                               ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;
  if (!query || !query[0])
    return -1;
  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;
  char *encoded = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);
  if (!encoded)
    return -1;
  char url[BUFFER_SIZE_XLARGE];
  int offset = (page - 1) * IMAGE_RESULTS_PER_PAGE;
  if (offset < 0)
    offset = 0;
  snprintf(url, sizeof(url),
           "https://api.qwant.com/v3/search/images?locale=en_US&count=%d"
           "&offset=%d&q=%s",
           IMAGE_RESULTS_PER_PAGE, offset, encoded);
  free(encoded);

  HttpResponse resp = http_get(url, "Seek/1.0 (https://seek.lyrabrowser.com)");
  if (!resp.memory)
    return -1;

  int cap = IMAGE_RESULTS_PER_PAGE;
  ImageResult *results = malloc(sizeof(ImageResult) * cap);
  if (!results) {
    http_response_free(&resp);
    return -1;
  }
  int count = 0;
  const char *items = strstr(resp.memory, "\"items\"");
  const char *p = items;
  const char *lim = resp.memory + resp.size;
  while (p && p < lim && count < cap) {
    const char *media = strstr(p, "\"media\"");
    if (!media)
      break;
    const char *c = media;
    char *full = ov_find_string(&c, "media");
    char *thumb = NULL;
    const char *th = strstr(p, "\"thumbnail\"");
    if (th && th < media + 2000) {
      const char *t = th;
      thumb = ov_find_string(&t, "thumbnail");
    }
    char *title = NULL;
    const char *tk = strstr(p, "\"title\"");
    if (tk && tk < media + 2000) {
      const char *t = tk;
      title = ov_find_string(&t, "title");
    }
    char *pageu = NULL;
    const char *uk = strstr(p, "\"url\"");
    if (uk && uk < media + 4000) {
      const char *u = uk;
      pageu = ov_find_string(&u, "url");
    }
    if (full && full[0]) {
      char *proxy_thumb = proxy_wrap_image_url(thumb ? thumb : full);
      results[count].thumbnail_url =
          proxy_thumb ? proxy_thumb : strdup(thumb ? thumb : full);
      results[count].title = title ? title : strdup("Image");
      title = NULL;
      results[count].page_url = pageu ? pageu : strdup(full);
      pageu = NULL;
      results[count].full_url = full;
      full = NULL;
      count++;
    }
    free(title);
    free(thumb);
    free(full);
    free(pageu);
    p = media + 8;
  }
  http_response_free(&resp);
  if (count == 0) {
    free(results);
    return -1;
  }
  *out_results = results;
  *out_count = count;
  return 0;
}

/* unsplash internal search api - high-quality photos, no key needed */
static int scrape_images_unsplash(const char *query, int page,
                                  ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;
  if (!query || !query[0])
    return -1;
  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;
  char *encoded = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);
  if (!encoded)
    return -1;
  char url[BUFFER_SIZE_XLARGE];
  snprintf(url, sizeof(url),
           "https://unsplash.com/napi/search/photos?query=%s&per_page=%d&page=%d",
           encoded, IMAGE_RESULTS_PER_PAGE, page);
  free(encoded);

  HttpResponse resp = http_get(url, "Seek/1.0 (https://seek.lyrabrowser.com)");
  if (!resp.memory)
    return -1;

  int cap = IMAGE_RESULTS_PER_PAGE;
  ImageResult *results = malloc(sizeof(ImageResult) * cap);
  if (!results) {
    http_response_free(&resp);
    return -1;
  }
  int count = 0;
  const char *lim = resp.memory + resp.size;
  const char *p = strstr(resp.memory, "\"urls\"");
  while (p && p < lim && count < cap) {
    /* each result object has one urls block with thumb/full variants */
    const char *c = p;
    char *small = ov_find_string(&c, "small");
    c = p;
    char *regular = ov_find_string(&c, "regular");
    char *title = NULL;
    const char *tk = strstr(p, "\"alt_description\"");
    if (tk && tk < p + 3000) {
      const char *t = tk;
      title = ov_find_string(&t, "alt_description");
    }
    char *landing = NULL;
    const char *lk = strstr(p, "\"html\"");
    if (lk && lk < p + 4000) {
      const char *u = lk;
      landing = ov_find_string(&u, "html");
    }
    const char *full = regular ? regular : small;
    if (full && full[0]) {
      char *proxy_thumb = proxy_wrap_image_url(small ? small : full);
      results[count].thumbnail_url =
          proxy_thumb ? proxy_thumb : strdup(small ? small : full);
      results[count].title = title ? title : strdup("Photo");
      title = NULL;
      results[count].page_url =
          landing ? landing : strdup("https://unsplash.com/");
      landing = NULL;
      results[count].full_url = strdup(full);
      count++;
    }
    free(small);
    free(regular);
    free(title);
    free(landing);
    p = strstr(p + 6, "\"urls\"");
  }
  http_response_free(&resp);
  if (count == 0) {
    free(results);
    return -1;
  }
  *out_results = results;
  *out_count = count;
  return 0;
}

/* flickr public photos feed - tag search, latest 20 items, page 1 only */
static int scrape_images_flickr(const char *query, int page,
                                ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;
  if (!query || !query[0] || page > 1)
    return -1;
  CURL *tmp = curl_easy_init();
  if (!tmp)
    return -1;
  char *encoded = encode_image_query(tmp, query);
  curl_easy_cleanup(tmp);
  if (!encoded)
    return -1;
  char url[BUFFER_SIZE_XLARGE];
  /* first word as the tag keeps results on-topic for multiword queries */
  char tag[64] = {0};
  snprintf(tag, sizeof(tag), "%s", encoded);
  char *sp = strchr(tag, ' ');
  if (sp)
    *sp = '\0';
  snprintf(url, sizeof(url),
           "https://api.flickr.com/services/feeds/photos_public.gne?tags=%s&format=json&nojsoncallback=1",
           tag);
  free(encoded);

  HttpResponse resp = http_get(url, "Seek/1.0 (https://seek.lyrabrowser.com)");
  if (!resp.memory)
    return -1;

  int cap = IMAGE_RESULTS_PER_PAGE;
  ImageResult *results = malloc(sizeof(ImageResult) * cap);
  if (!results) {
    http_response_free(&resp);
    return -1;
  }
  int count = 0;
  const char *lim = resp.memory + resp.size;
  const char *p = strstr(resp.memory, "\"media\"");
  while (p && p < lim && count < cap) {
    const char *c = p;
    char *m = ov_find_string(&c, "m");
    char *title = NULL;
    const char *tk = strstr(p, "\"title\"");
    if (tk && tk < p + 3000) {
      const char *t = tk;
      title = ov_find_string(&t, "title");
    }
    char *link = NULL;
    const char *lk = strstr(p, "\"link\"");
    if (lk && lk < p + 2000) {
      const char *u = lk;
      link = ov_find_string(&u, "link");
    }
    if (m && m[0]) {
      char *proxy_thumb = proxy_wrap_image_url(m);
      results[count].thumbnail_url = proxy_thumb ? proxy_thumb : strdup(m);
      results[count].title =
          (title && title[0]) ? title : strdup("Flickr photo");
      title = NULL;
      results[count].page_url =
          link ? link : strdup("https://www.flickr.com/");
      link = NULL;
      results[count].full_url = m;
      m = NULL;
      count++;
    }
    free(m);
    free(title);
    free(link);
    p = strstr(p + 7, "\"media\"");
  }
  http_response_free(&resp);
  if (count == 0) {
    free(results);
    return -1;
  }
  *out_results = results;
  *out_count = count;
  return 0;
}

typedef struct {
  ImageResult img;
  int score;
  int src;
  int ord;
} RankedImage;

static int cmp_ranked_image(const void *a, const void *b) {
  const RankedImage *x = a;
  const RankedImage *y = b;
  if (x->score != y->score)
    return y->score - x->score;
  if (x->src != y->src)
    return x->src - y->src;
  return x->ord - y->ord;
}

int scrape_images(const char *query, int page, const ImageFilters *filters __attribute__((unused)),
                  ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;

  /* bing and qwant are both dead upstream (JS shell / datadome) - keep the
     scrapers but stop spending a fetch on them */
  ImageResult *sets[4] = {NULL, NULL, NULL, NULL};
  int counts[4] = {0, 0, 0, 0};
  scrape_images_openverse(query, page, &sets[0], &counts[0]);
  scrape_images_commons(query, page, &sets[1], &counts[1]);
  scrape_images_unsplash(query, page, &sets[2], &counts[2]);
  scrape_images_flickr(query, page, &sets[3], &counts[3]);
  (void)scrape_images_bing;
  (void)scrape_images_qwant;

  int total = counts[0] + counts[1] + counts[2] + counts[3];
  if (total == 0) {
    for (int i = 0; i < 4; i++)
      free_image_results(sets[i], counts[i]);
    return -1;
  }

  RankedImage *ranked = malloc(sizeof(RankedImage) * (size_t)total);
  if (!ranked) {
    for (int i = 0; i < 4; i++)
      free_image_results(sets[i], counts[i]);
    return -1;
  }

  int n = 0;
  for (int s = 0; s < 4; s++) {
    for (int i = 0; i < counts[s]; i++) {
      ImageResult *take = &sets[s][i];
      int dup = 0;
      for (int j = 0; j < n; j++) {
        if (image_url_equals(ranked[j].img.full_url, take->full_url)) {
          dup = 1;
          break;
        }
      }
      if (dup) {
        drop_image(take);
        continue;
      }
      ranked[n].img = *take;
      ranked[n].score = image_query_score(query, take->title, take->page_url,
                                          take->full_url);
      ranked[n].src = s;
      ranked[n].ord = n;
      n++;
    }
    free(sets[s]);
  }

  if (n == 0) {
    free(ranked);
    return -1;
  }

  qsort(ranked, (size_t)n, sizeof(RankedImage), cmp_ranked_image);

  int keep = n < IMAGE_RESULTS_PER_PAGE ? n : IMAGE_RESULTS_PER_PAGE;
  if (image_query_has_distinctive_token(query)) {
    while (keep > 0 && ranked[keep - 1].score < 8)
      keep--;
  } else {
    int strong = 0;
    for (int i = 0; i < n; i++) {
      if (ranked[i].score >= 8)
        strong++;
    }
    if (strong >= 8) {
      while (keep > 8 && ranked[keep - 1].score < 4)
        keep--;
    }
  }

  if (keep == 0) {
    for (int i = 0; i < n; i++)
      drop_image(&ranked[i].img);
    free(ranked);
    *out_results = NULL;
    *out_count = 0;
    return 0;
  }

  ImageResult *merged = malloc(sizeof(ImageResult) * (size_t)keep);
  if (!merged) {
    for (int i = 0; i < n; i++)
      drop_image(&ranked[i].img);
    free(ranked);
    return -1;
  }
  for (int i = 0; i < keep; i++)
    merged[i] = ranked[i].img;
  for (int i = keep; i < n; i++)
    drop_image(&ranked[i].img);
  free(ranked);

  *out_results = merged;
  *out_count = keep;
  return 0;
}

void free_image_results(ImageResult *results, int count) {
  if (!results)
    return;

  for (int i = 0; i < count; i++) {
    free(results[i].thumbnail_url);
    free(results[i].title);
    free(results[i].page_url);
    free(results[i].full_url);
  }
  free(results);
}
