#include "../src/Utility/SlopDetect.h"
#include "test_harness.h"
#include <string.h>

static void test_emdash_title_alone(void) {
  double s = slop_detect_page("Best Laptops \xe2\x80\x94 2026 Guide", NULL,
                              "buy stuff");
  TEST_ASSERT(s < 0.25);
}

static void test_en_dash_not_seo(void) {
  double s = slop_detect_page(
      "The Nameless Virtue \xe2\x80\x93 Let's figure out what matters", NULL,
      "From a civilization I have never heard of I was drawn to the stele.");
  TEST_ASSERT(s < 0.25);
}

static void test_human_prose(void) {
  const char *text =
      "Firefox is a web browser. It ships with a PDF viewer and "
      "you can change search engines in settings. The cat sat on "
      "the mat and the dog ran in the yard. Gwen Tuinman is a "
      "Canadian author of historical fiction, fascinated by the "
      "landscape of human tenacity, writing about the lives of women.";
  double s = slop_detect_page("Firefox", NULL, text);
  TEST_ASSERT(s < 0.25);
}

static void test_wikipedia_style(void) {
  const char *text =
      "The is a grammatical article in English, denoting nouns that "
      "are already or about to be mentioned, under discussion, implied "
      "or otherwise presumed familiar to listeners, readers, or speakers. "
      "It is the definite article in English. It is also used as an adverb.";
  double s = slop_detect_page("The", NULL, text);
  TEST_ASSERT(s < 0.25);
}

static void test_news_lede(void) {
  const char *text =
      "London (Reuters) - The Bank of England held interest rates "
      "steady on Thursday after inflation cooled more than expected. "
      "Governor Andrew Bailey said risks remain on both sides.";
  double s = slop_detect_page("Bank of England holds rates", NULL, text);
  TEST_ASSERT(s < 0.25);
}

static void test_chat_artifact_high(void) {
  const char *text =
      "Great question. As an AI I hope this helps. In today's "
      "fast-paced world it is important to note that this stands "
      "as a testament to progress, highlighting the journey, "
      "paving the way for a myriad of seamless solutions.";
  double s = slop_detect_page("A complete guide \xe2\x80\x94 2026", NULL, text);
  TEST_ASSERT(s > 0.5);
}

static void test_code_double_dash(void) {
  double s = slop_detect_page(
      "curl --socks5 10.64.0.1:1080", NULL,
      "Pass --socks5 to curl. Use --fail and --silent together.");
  TEST_ASSERT(s < 0.25);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_emdash_title_alone();
  test_en_dash_not_seo();
  test_human_prose();
  test_wikipedia_style();
  test_news_lede();
  test_chat_artifact_high();
  test_code_double_dash();
  TEST_MAIN_RETURN();
}
