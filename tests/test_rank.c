#include "../src/Utility/Rank.h"
#include "test_harness.h"

static void test_wiki_boost(void) {
  TEST_ASSERT(rank_host_weight("https://en.wikipedia.org/wiki/C", 1) > 2.0);
  TEST_ASSERT(rank_host_weight("https://wiki.archlinux.org/title/Pacman", 1) >
              2.0);
}

static void test_forum_gate(void) {
  double on = rank_host_weight("https://news.ycombinator.com/item?id=1", 1);
  double off = rank_host_weight("https://news.ycombinator.com/item?id=1", 0);
  TEST_ASSERT(on > off);
}

static void test_query_forums(void) {
  TEST_ASSERT_EQ(rank_query_allows_forums("site:reddit.com rust"), 1);
  TEST_ASSERT_EQ(rank_query_allows_forums("linux kernel"), 0);
}

static void test_so_docs(void) {
  TEST_ASSERT(rank_host_weight("https://stackoverflow.com/q/1", 0) > 2.0);
  TEST_ASSERT(rank_host_weight("https://stackoverflow.com/q/1", 1) > 2.0);
}

static void test_code_hosts(void) {
  TEST_ASSERT(rank_host_weight("https://docs.rs/tokio/latest/tokio/", 0) > 2.0);
  TEST_ASSERT(rank_host_weight("https://crates.io/crates/serde", 0) > 2.0);
  TEST_ASSERT(rank_host_weight("https://pkg.go.dev/fmt", 0) > 2.0);
  TEST_ASSERT(rank_host_weight("https://github.com/torvalds/linux", 0) > 1.2);
  TEST_ASSERT(rank_host_weight("https://minecraft.wiki/w/Creeper", 0) > 2.0);
}

static void test_image_query_score(void) {
  int hit = image_query_score(
      "meshchatx", "MeshChatX screenshot",
      "https://github.com/quad4/meshchatx",
      "https://github.com/quad4/meshchatx/raw/main/shot.png");
  int meme = image_query_score(
      "meshchatx", "When the wifi is gone", "https://imgflip.com/i/abc",
      "https://i.imgflip.com/abc.jpg");
  TEST_ASSERT(hit > meme);
  TEST_ASSERT(hit >= 20);
  TEST_ASSERT(meme < 0);
  TEST_ASSERT_EQ(image_query_has_distinctive_token("meshchatx"), 1);
  TEST_ASSERT_EQ(image_query_has_distinctive_token("cat"), 0);
  TEST_ASSERT(image_query_score("meshchatx", "Witch coloring",
                                "https://www.freepik.com/witch",
                                "https://img.freepik.com/witch.jpg") < 0);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_wiki_boost();
  test_forum_gate();
  test_query_forums();
  test_so_docs();
  test_code_hosts();
  test_image_query_score();
  TEST_MAIN_RETURN();
}
