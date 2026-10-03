#include "ImageScraping.h"
#include "../Cache/Cache.h"
#include "../Routes/ImageProxy.h"
#include "../Utility/HttpClient.h"
#include "../Utility/Rank.h"
#include "Config.h"
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
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

static int parse_image_node(xmlNodePtr node, ImageResult *result) {
  xmlNodePtr img_node = NULL;
  xmlNodePtr tit_node = NULL;
  xmlNodePtr des_node = NULL;
  xmlNodePtr thumb_link = NULL;

  for (xmlNodePtr child = node->children; child; child = child->next) {
    if (child->type != XML_ELEMENT_NODE)
      continue;

    if (xmlStrcmp(child->name, (const xmlChar *)"a") == 0) {
      xmlChar *class = xmlGetProp(child, (const xmlChar *)"class");
      if (class) {
        if (xmlStrstr(class, (const xmlChar *)"thumb") != NULL) {
          thumb_link = child;
          for (xmlNodePtr thumb_child = child->children; thumb_child;
               thumb_child = thumb_child->next) {
            if (xmlStrcmp(thumb_child->name, (const xmlChar *)"div") == 0) {
              xmlChar *div_class =
                  xmlGetProp(thumb_child, (const xmlChar *)"class");
              if (div_class &&
                  xmlStrcmp(div_class, (const xmlChar *)"cico") == 0) {
                for (xmlNodePtr cico_child = thumb_child->children; cico_child;
                     cico_child = cico_child->next) {
                  if (xmlStrcmp(cico_child->name, (const xmlChar *)"img") ==
                      0) {
                    img_node = cico_child;
                    break;
                  }
                }
              }
              if (div_class)
                xmlFree(div_class);
            }
          }
        } else if (xmlStrstr(class, (const xmlChar *)"tit") != NULL) {
          tit_node = child;
        }
        xmlFree(class);
      }
    } else if (xmlStrcmp(child->name, (const xmlChar *)"div") == 0) {
      xmlChar *class = xmlGetProp(child, (const xmlChar *)"class");
      if (class && xmlStrcmp(class, (const xmlChar *)"meta") == 0) {
        for (xmlNodePtr meta_child = child->children; meta_child;
             meta_child = meta_child->next) {
          if (xmlStrcmp(meta_child->name, (const xmlChar *)"div") == 0) {
            xmlChar *div_class =
                xmlGetProp(meta_child, (const xmlChar *)"class");
            if (div_class) {
              if (xmlStrcmp(div_class, (const xmlChar *)"des") == 0) {
                des_node = meta_child;
              }
              xmlFree(div_class);
            }
          } else if (xmlStrcmp(meta_child->name, (const xmlChar *)"a") == 0) {
            xmlChar *a_class = xmlGetProp(meta_child, (const xmlChar *)"class");
            if (a_class && xmlStrstr(a_class, (const xmlChar *)"tit") != NULL) {
              tit_node = meta_child;
            }
            if (a_class)
              xmlFree(a_class);
          }
        }
      }
      if (class)
        xmlFree(class);
    }
  }

  xmlChar *iurl =
      img_node ? xmlGetProp(img_node, (const xmlChar *)"src") : NULL;
  xmlChar *full_url =
      thumb_link ? xmlGetProp(thumb_link, (const xmlChar *)"href") : NULL;
  xmlChar *title = des_node ? xmlNodeGetContent(des_node)
                            : (tit_node ? xmlNodeGetContent(tit_node) : NULL);
  xmlChar *rurl =
      tit_node ? xmlGetProp(tit_node, (const xmlChar *)"href") : NULL;

  if (!iurl || strlen((char *)iurl) == 0) {
    if (iurl)
      xmlFree(iurl);
    if (title)
      xmlFree(title);
    if (rurl)
      xmlFree(rurl);
    if (full_url)
      xmlFree(full_url);
    return 0;
  }

  char *proxy_url = proxy_wrap_image_url((char *)iurl);
  result->thumbnail_url = proxy_url ? proxy_url : strdup((char *)iurl);
  result->title = strdup(title ? (char *)title : "Image");
  result->page_url = strdup(rurl ? (char *)rurl : "#");
  result->full_url = strdup(full_url ? (char *)full_url : "#");

  if (iurl)
    xmlFree(iurl);
  if (title)
    xmlFree(title);
  if (rurl)
    xmlFree(rurl);
  if (full_url)
    xmlFree(full_url);

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

  htmlDocPtr doc = htmlReadMemory(resp.memory, resp.size, NULL, NULL,
                                  HTML_PARSE_RECOVER | HTML_PARSE_NOERROR);
  if (!doc) {
    http_response_free(&resp);
    return -1;
  }

  xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
  if (!xpathCtx) {
    xmlFreeDoc(doc);
    http_response_free(&resp);
    return -1;
  }

  xmlXPathObjectPtr xpathObj =
      xmlXPathEvalExpression((const xmlChar *)"//div[@class='item']", xpathCtx);

  if (!xpathObj || !xpathObj->nodesetval) {
    if (xpathObj)
      xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);
    http_response_free(&resp);
    return 0;
  }

  int nodes = xpathObj->nodesetval->nodeNr;
  int max_images =
      (nodes < IMAGE_RESULTS_PER_PAGE) ? nodes : IMAGE_RESULTS_PER_PAGE;

  ImageResult *results = malloc(sizeof(ImageResult) * max_images);
  if (!results) {
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);
    http_response_free(&resp);
    return -1;
  }

  int count = 0;
  for (int i = 0; i < nodes && count < IMAGE_RESULTS_PER_PAGE; i++) {
    xmlNodePtr node = xpathObj->nodesetval->nodeTab[i];
    if (parse_image_node(node, &results[count])) {
      count++;
    }
  }

  xmlXPathFreeObject(xpathObj);
  xmlXPathFreeContext(xpathCtx);
  xmlFreeDoc(doc);
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

int scrape_images(const char *query, int page, const ImageFilters *filters,
                  ImageResult **out_results, int *out_count) {
  *out_results = NULL;
  *out_count = 0;

  ImageResult *sets[4] = {NULL, NULL, NULL, NULL};
  int counts[4] = {0, 0, 0, 0};
  scrape_images_bing(query, page, filters, &sets[0], &counts[0]);
  scrape_images_openverse(query, page, &sets[1], &counts[1]);
  scrape_images_commons(query, page, &sets[2], &counts[2]);
  scrape_images_qwant(query, page, &sets[3], &counts[3]);

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
