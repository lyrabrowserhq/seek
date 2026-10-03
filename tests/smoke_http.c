#include "../src/Utility/HttpClient.h"
#include <stdlib.h>

int main(void) {
  HttpResponse r =
      http_get("http://127.0.0.1:9/", "seeker-smoke/1.0");
  http_response_free(&r);
  return 0;
}
