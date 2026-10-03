#include "XmlHelper.h"
#include <stdlib.h>
#include <string.h>

SearchResult *xml_result_alloc(int count, int max_results) {
  if (count <= 0 || max_results <= 0) {
    return NULL;
  }
  int actual = (count < max_results) ? count : max_results;
  return (SearchResult *)calloc(actual, sizeof(SearchResult));
}

void xml_result_free(SearchResult *results, int count) {
  if (!results) {
    return;
  }
  for (int i = 0; i < count; i++) {
    free(results[i].url);
    free(results[i].title);
    free(results[i].snippet);
  }
  free(results);
}

xmlXPathObjectPtr xml_xpath_eval(xmlXPathContextPtr ctx, const char *xpath) {
  if (!ctx || !xpath) {
    return NULL;
  }
  return xmlXPathEvalExpression((const xmlChar *)xpath, ctx);
}

char *xml_node_content(xmlNodePtr node) {
  if (!node) {
    return NULL;
  }
  char *content = (char *)xmlNodeGetContent(node);
  return content;
}

char *xpath_text(xmlDocPtr doc, const char *xpath) {
  if (!doc || !xpath) {
    return NULL;
  }

  xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
  if (!ctx) {
    return NULL;
  }

  xmlXPathObjectPtr obj = xmlXPathEvalExpression((const xmlChar *)xpath, ctx);
  xmlXPathFreeContext(ctx);

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    if (obj)
      xmlXPathFreeObject(obj);
    return NULL;
  }

  xmlChar *content = xmlNodeGetContent(obj->nodesetval->nodeTab[0]);
  char *result = content ? strdup((char *)content) : NULL;
  if (content)
    xmlFree(content);
  xmlXPathFreeObject(obj);
  return result;
}
