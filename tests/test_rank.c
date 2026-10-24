#include "../src/Utility/Rank.h"
#include "test_harness.h"

static void test_wiki_boost(void) {
  TEST_ASSERT(rank_host_weight("https://en.wikipedia.org/wiki/C", 1) > 2.0);
  TEST_ASSERT(rank_host_weight("https://wiki.archlinux.org/title/Pacman", 1) >
              2.0);
}

static void test_forum_detection(void);

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

static void test_lens_weights(void) {
  TEST_ASSERT_EQ(lens_is_valid("forums"), 1);
  TEST_ASSERT_EQ(lens_is_valid("nope"), 0);
  TEST_ASSERT_EQ(lens_forces_forums("forums"), 1);
  TEST_ASSERT_EQ(lens_forces_forums("docs"), 0);
  TEST_ASSERT(lens_host_weight("https://lobste.rs/s/x", "forums") >
              lens_host_weight("https://docs.rs/x", "forums"));
  TEST_ASSERT(lens_host_weight("https://docs.rs/serde", "docs") > 2.0);
  TEST_ASSERT(lens_host_weight("https://en.wikipedia.org/wiki/X", "wiki") >
              lens_host_weight("https://medium.com/x", "wiki"));
  TEST_ASSERT_EQ(lens_host_weight("https://any.test/x", ""), 1.0);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_wiki_boost();
  test_forum_gate();
  test_forum_detection();
  test_query_forums();
  test_so_docs();
  test_code_hosts();
  test_image_query_score();
  test_lens_weights();
  TEST_MAIN_RETURN();
}

/* added forum shapes: discourse subdomains, phpbb/vbulletin paths, hosted
   platforms, mailing list archives */
static void test_forum_detection(void) {
  struct { const char *url; int want; } cases[] = {
      {"https://discuss.python.org/t/async/1", 1},
      {"https://talk.jekyllrb.com/t/topic/1", 1},
      {"https://meta.discourse.org/t/x", 1},
      {"https://forum.example.com/viewtopic.php?t=9", 1},
      {"https://bbs.boards.net/post/1", 1},
      {"https://www.phpbb.com/community/viewtopic.php?f=1&t=2", 1},
      {"https://xenforo.com/community/threads/test.123/", 1},
      {"https://vanilla.example.com/discussion/42/x", 1},
      {"https://lists.gnu.org/archive/html/bug/2024/msg001.html", 1},
      {"https://lore.kernel.org/lkml/abc/", 1},
      {"https://example.com/blog/post", 0},
      {"https://example.com/technology/x", 0},
      {"https://www.reddit.com/r/rust", 1},
      {"https://quora.com/question", 1},
      {"https://myabandonware.com/game/x", 0},
  };
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
    double w = lens_host_weight(cases[i].url, "forums");
    if (cases[i].want) {
      if (!(w > 3.0)) { printf("MISS forum: %s\n", cases[i].url); TEST_ASSERT(0); }
    } else {
      if (!(w < 1.0)) { printf("MISS demote: %s\n", cases[i].url); TEST_ASSERT(0); }
    }
  }
}
