#include "Suggest.h"
#include "../Utility/HttpClient.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUGGEST_PROVIDER_URL "https://duckduckgo.com/ac/?q=%s&type=list"

static size_t url_encode_into(const char *in, char *out, size_t out_size) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)in;
       *p && o + 4 < out_size; p++) {
    if (isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~') {
      out[o++] = (char)*p;
    } else {
      int n = snprintf(out + o, out_size - o, "%%%02X", *p);
      if (n < 0)
        break;
      o += (size_t)n;
    }
  }
  if (out_size > 0)
    out[o < out_size ? o : out_size - 1] = '\0';
  return o;
}

int suggest_handler(UrlParams *params) {
  const char *raw_query = "";
  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0)
        raw_query = params->params[i].value;
    }
  }

  if (!raw_query || raw_query[0] == '\0') {
    const char *empty = "[\"\",[]]";
    serve_data(empty, strlen(empty), "application/json; charset=UTF-8");
    return 0;
  }

  char encoded[1024];
  url_encode_into(raw_query, encoded, sizeof(encoded));

  char url[1600];
  snprintf(url, sizeof(url), SUGGEST_PROVIDER_URL, encoded);

  HttpResponse resp = http_get(url, "Seek-Suggest/1.0");
  if (resp.memory && resp.size > 0) {
    serve_data(resp.memory, resp.size, "application/json; charset=UTF-8");
  } else {
    char *buf = NULL;
    size_t blen = 0;
    FILE *fp = open_memstream(&buf, &blen);
    if (fp) {
      fprintf(fp, "[\"%s\",[]]", raw_query);
      fclose(fp);
      if (buf && blen > 0)
        serve_data(buf, blen, "application/json; charset=UTF-8");
      free(buf);
    }
  }
  http_response_free(&resp);
  return 0;
}
