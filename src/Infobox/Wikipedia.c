#include "Wikipedia.h"
#include "../Cache/Cache.h"
#include "../Scraping/Scraping.h"
#include "../Utility/HttpClient.h"
#include "Config.h"
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void shorten_summary(char **extract_ptr, int max_chars) {
  if (!extract_ptr || !*extract_ptr)
    return;

  char *text = *extract_ptr;
  int len = strlen(text);

  if (len <= max_chars)
    return;

  int end_pos = max_chars;
  for (int i = max_chars; i > (max_chars / 2); i--) {
    if (text[i] == '.' || text[i] == '!' || text[i] == '?') {
      end_pos = i + 1;
      break;
    }
  }

  char *new_text = (char *)malloc(end_pos + 4);

  if (new_text) {
    strncpy(new_text, text, end_pos);
    new_text[end_pos] = '\0';
    strcat(new_text, "...");
    free(*extract_ptr);
    *extract_ptr = new_text;
  }
}

static void extract_wiki_info(xmlNode *node, InfoBox *info) {
  xmlNode *cur_node = NULL;

  for (cur_node = node; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (strcmp((const char *)cur_node->name, "page") == 0) {
        xmlChar *title = xmlGetProp(cur_node, (const xmlChar *)"title");
        if (title) {
          if (info->title) {
            free(info->title);
          }
          info->title = strdup((const char *)title);

          const char *base_article_url = "https://en.wikipedia.org/wiki/";
          char *formatted_title = strdup((const char *)title);
          for (int i = 0; formatted_title[i]; i++) {
            if (formatted_title[i] == ' ')
              formatted_title[i] = '_';
          }

          if (info->url) {
            free(info->url);
          }
          info->url =
              malloc(strlen(base_article_url) + strlen(formatted_title) + 1);
          if (info->url) {
            strcpy(info->url, base_article_url);
            strcat(info->url, formatted_title);
          }
          free(formatted_title);
          xmlFree(title);
        }
      }

      if (strcmp((const char *)cur_node->name, "thumbnail") == 0) {
        xmlChar *source = xmlGetProp(cur_node, (const xmlChar *)"source");
        if (source) {
          const char *src = (const char *)source;
          if (src[0] && (strncmp(src, "http://", 7) == 0 ||
                         strncmp(src, "https://", 8) == 0)) {
            if (info->thumbnail_url)
              free(info->thumbnail_url);
            info->thumbnail_url = strdup(src);
          }
          xmlFree(source);
        }
      }

      if (strcmp((const char *)cur_node->name, "extract") == 0) {
        xmlChar *content = xmlNodeGetContent(cur_node);
        if (content) {
          if (info->extract) {
            free(info->extract);
          }
          info->extract = strdup((const char *)content);

          shorten_summary(&(info->extract), WIKI_SUMMARY_MAX_CHARS);
          xmlFree(content);
        }
      }
    }
    extract_wiki_info(cur_node->children, info);
  }
}

InfoBox fetch_wiki_data(char *api_url) {
  InfoBox info = {NULL, NULL, NULL, NULL};

  if (!api_url) {
    return info;
  }

  char *cache_key = cache_compute_key(api_url, 0, "wikipedia");
  if (cache_key && get_cache_ttl_infobox() > 0) {
    char *cached_data = NULL;
    size_t cached_size = 0;
    if (cache_get(cache_key, get_cache_ttl_infobox(), &cached_data,
                  &cached_size) == 0 &&
        cached_data && cached_size > 0) {
      xmlDocPtr doc =
          xmlReadMemory(cached_data, cached_size, "noname.xml", NULL, 0);
      if (doc != NULL) {
        xmlNode *root_element = xmlDocGetRootElement(doc);
        extract_wiki_info(root_element, &info);
        xmlFreeDoc(doc);
      }
      free(cached_data);
      free(cache_key);
      return info;
    }
    free(cached_data);
  }
  free(cache_key);

  HttpResponse resp = http_get(api_url, SEEK_HTTP_UA);
  if (resp.memory && resp.size > 0) {
    cache_key = cache_compute_key(api_url, 0, "wikipedia");
    if (cache_key && get_cache_ttl_infobox() > 0) {
      cache_set(cache_key, resp.memory, resp.size);
    }
    free(cache_key);

    xmlDocPtr doc =
        xmlReadMemory(resp.memory, resp.size, "noname.xml", NULL, 0);
    if (doc != NULL) {
      xmlNode *root_element = xmlDocGetRootElement(doc);
      extract_wiki_info(root_element, &info);
      xmlFreeDoc(doc);
    }
  }

  http_response_free(&resp);
  return info;
}

static xmlNode *find_node_recursive(xmlNode *node, const char *target_name) {
  for (xmlNode *cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE &&
        strcmp((const char *)cur->name, target_name) == 0) {
      return cur;
    }
    xmlNode *found = find_node_recursive(cur->children, target_name);
    if (found)
      return found;
  }
  return NULL;
}

static char *get_first_search_result(const char *search_term) {
  char *escaped_term = curl_easy_escape(NULL, search_term, 0);
  const char *search_base =
      "https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=";
  const char *search_suffix = "&format=xml&origin=*&srlimit=1";

  char *search_url = malloc(strlen(search_base) + strlen(escaped_term) +
                            strlen(search_suffix) + 1);
  if (!search_url) {
    curl_free(escaped_term);
    return NULL;
  }

  strcpy(search_url, search_base);
  strcat(search_url, escaped_term);
  strcat(search_url, search_suffix);

  curl_free(escaped_term);

  HttpResponse resp = http_get(search_url, SEEK_HTTP_UA);
  free(search_url);

  char *first_title = NULL;
  if (resp.memory && resp.size > 0) {
    xmlDocPtr doc =
        xmlReadMemory(resp.memory, resp.size, "noname.xml", NULL, 0);
    if (doc) {
      xmlNode *root = xmlDocGetRootElement(doc);
      xmlNode *search_node = find_node_recursive(root, "search");
      if (search_node) {
        for (xmlNode *sr = search_node->children; sr; sr = sr->next) {
          if (sr->type == XML_ELEMENT_NODE &&
              strcmp((const char *)sr->name, "p") == 0) {
            xmlChar *title = xmlGetProp(sr, (const xmlChar *)"title");
            if (title) {
              first_title = strdup((const char *)title);
              xmlFree(title);
              break;
            }
          }
        }
      }
      xmlFreeDoc(doc);
    }
  }

  http_response_free(&resp);
  return first_title;
}

char *construct_wiki_url(const char *search_term) {
  char *first_title = get_first_search_result(search_term);
  if (!first_title)
    return NULL;

  char *escaped_title = curl_easy_escape(NULL, first_title, 0);
  const char *base = "https://en.wikipedia.org/w/"
                     "api.php?action=query&prop=extracts|pageimages&exintro&"
                     "explaintext&pithumbsize=400&format=xml&origin=*&titles=";

  char *full_url = malloc(strlen(base) + strlen(escaped_title) + 1);
  if (full_url) {
    strcpy(full_url, base);
    strcat(full_url, escaped_title);
  }

  curl_free(escaped_title);
  free(first_title);
  return full_url;
}
