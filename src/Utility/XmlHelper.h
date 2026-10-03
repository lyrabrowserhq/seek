#ifndef XMLHELPER_H
#define XMLHELPER_H

#include "../Scraping/Scraping.h"
#include <libxml/xpath.h>

SearchResult *xml_result_alloc(int count, int max_results);
void xml_result_free(SearchResult *results, int count);

xmlXPathObjectPtr xml_xpath_eval(xmlXPathContextPtr ctx, const char *xpath);
char *xml_node_content(xmlNodePtr node);
char *xpath_text(xmlDocPtr doc, const char *xpath);

#endif
