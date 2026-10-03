#include <beaker.h>
#include <stdio.h>
#include <stdlib.h>

int hello_world_handler(UrlParams *params) {
  send_response("Hello, World!");
  return 0;
}

int main() {
  set_handler("/", hello_world_handler);
  int port = 8080;
  int result = beaker_run("127.0.0.1", &port);

  if (result != 0) {
    fprintf(stderr, "[APP] Error: Beaker server failed to start.\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
