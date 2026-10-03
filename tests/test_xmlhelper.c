#include "../src/Utility/XmlHelper.h"
#include "test_harness.h"
#include <libxml/parser.h>
#include <stdlib.h>
#include <string.h>

static void test_alloc_edges(void) {
  TEST_ASSERT_NULL(xml_result_alloc(0, 10));
  TEST_ASSERT_NULL(xml_result_alloc(10, 0));

  SearchResult *r = xml_result_alloc(10, 3);
  TEST_ASSERT_NOT_NULL(r);
  xml_result_free(r, 3);
  xml_result_free(NULL, 0);
}

static void test_xpath_eval_null(void) {
  TEST_ASSERT_NULL(xml_xpath_eval(NULL, "//a"));
}

static void test_xpath_text_simple(void) {
  const char *xml = "<root><item>hello</item></root>";
  xmlDocPtr doc =
      xmlReadMemory(xml, (int)strlen(xml), "t", NULL, XML_PARSE_RECOVER);
  TEST_ASSERT_NOT_NULL(doc);
  char *t = xpath_text(doc, "//item");
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT(strcmp(t, "hello") == 0);
  free(t);
  xmlFreeDoc(doc);
}

static void test_xpath_text_bad(void) {
  xmlDocPtr doc = xmlReadMemory("<a/>", 4, "t", NULL, XML_PARSE_RECOVER);
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_NULL(xpath_text(doc, "//missing"));
  xmlFreeDoc(doc);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_alloc_edges();
  test_xpath_eval_null();
  test_xpath_text_simple();
  test_xpath_text_bad();
  TEST_MAIN_RETURN();
}
