#include <beaker.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int templating_handler(UrlParams *params) {

  TemplateContext ctx = new_context();

  context_set(&ctx, "title", "Beaker Example");
  context_set(&ctx, "page_heading", "Dynamic Content with Beaker Templates");
  context_set(&ctx, "username", "John Doe");
  context_set(&ctx, "favourite_colour", "blue");

  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  char current_year_str[5];
  snprintf(current_year_str, sizeof(current_year_str), "%d",
           tm->tm_year + 1900);
  context_set(&ctx, "current_year", current_year_str);

  char timestamp_str[64];
  strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", tm);
  context_set(&ctx, "timestamp", timestamp_str);

  context_set(&ctx, "safe_html", "This is <b>bold</b> and <i>italic</i> HTML.");
  context_set(&ctx, "unsafe_html", "<script>alert(0);");

  context_set(&ctx, "user_logged_in", "true");
  context_set(&ctx, "role", "admin");
  context_set(&ctx, "status", "active");

  char *features[] = {"Fast and Lightweight", "Simple API",
                      "Basic Routing",        "Templating Engine",
                      "Static File Serving",  "Cookie Management"};
  int num_features = sizeof(features) / sizeof(features[0]);
  context_set_string_array(&ctx, "features", features, num_features);

  char *user1[] = {"Alice", "30", "New York"};
  char *user2[] = {"Bob", "24", "Paris"};
  char *user3[] = {"Charlie", "35", "London"};
  char **users_2d[] = {user1, user2, user3};

  int user_inner_counts[] = {3, 3, 3};
  int num_users = sizeof(users_2d) / sizeof(users_2d[0]);
  context_set_array_of_arrays(&ctx, "users", users_2d, num_users,
                              user_inner_counts);

  char *rendered_html = render_template("index.html", &ctx);
  if (rendered_html == NULL) {
    fprintf(stderr, "[APP] Error: Failed to render template.\n");
    free_context(&ctx);
    send_response(
        "<h1>500 Internal Server Error</h1><p>Failed to render template.</p>");
    return -1;
  }

  send_response(rendered_html);

  free(rendered_html);
  free_context(&ctx);

  return 0;
}

int main() {

  set_handler("/", templating_handler);

  int port = 8080;
  int result = beaker_run("127.0.0.1", &port);

  if (result != 0) {
    fprintf(stderr, "[APP] Error: Beaker server failed to start.\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
