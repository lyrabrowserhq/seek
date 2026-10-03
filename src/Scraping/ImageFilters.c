#include "ImageFilters.h"
#include "Config.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static int str_in_list(const char *value, const char *const *list, int count) {
  if (!value || value[0] == '\0')
    return 0;
  for (int i = 0; i < count; i++) {
    if (strcmp(value, list[i]) == 0)
      return 1;
  }
  return 0;
}

static void set_if_valid(char *dest, size_t dest_size, const char *value,
                         const char *const *allowed, int allowed_count) {
  if (!dest || dest_size == 0)
    return;
  dest[0] = '\0';
  if (value && str_in_list(value, allowed, allowed_count)) {
    strncpy(dest, value, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
}

static const char *const REGIONS[] = {
    "en-US", "en-GB", "en-CA", "en-AU", "en-IN", "de-DE", "fr-FR",
    "es-ES", "it-IT", "pt-BR", "ja-JP", "ko-KR", "zh-CN", "ru-RU",
    "nl-NL", "pl-PL", "sv-SE", "tr-TR", "id-ID", "ar-SA",
};

static const char *const SAFE[] = {"strict", "moderate", "off"};
static const char *const TIME[] = {"day", "week", "month", "year"};
static const char *const SIZES[] = {"small", "medium", "large", "wallpaper"};
static const char *const COLORS[] = {
    "color", "bw", "red", "orange", "yellow", "green", "teal", "blue",
    "purple", "pink", "white", "gray", "black", "brown",
};
static const char *const TYPES[] = {
    "photo", "clipart", "linedrawing", "transparent", "animated",
};
static const char *const LAYOUTS[] = {"square", "wide", "tall"};
static const char *const LICENSES[] = {
    "any", "public", "share", "sharecommercial", "modify", "modifycommercial",
};

void image_filters_defaults(ImageFilters *filters) {
  if (!filters)
    return;
  memset(filters, 0, sizeof(*filters));
  strncpy(filters->safe, "moderate", sizeof(filters->safe) - 1);
}

static void read_param(ImageFilters *filters, const char *key, const char *value) {
  if (!filters || !key || !value)
    return;

  if (strcmp(key, "mkt") == 0) {
    set_if_valid(filters->region, sizeof(filters->region), value, REGIONS,
                 (int)(sizeof(REGIONS) / sizeof(REGIONS[0])));
  } else if (strcmp(key, "safe") == 0) {
    set_if_valid(filters->safe, sizeof(filters->safe), value, SAFE,
                 (int)(sizeof(SAFE) / sizeof(SAFE[0])));
  } else if (strcmp(key, "when") == 0) {
    set_if_valid(filters->time, sizeof(filters->time), value, TIME,
                 (int)(sizeof(TIME) / sizeof(TIME[0])));
  } else if (strcmp(key, "size") == 0) {
    set_if_valid(filters->size, sizeof(filters->size), value, SIZES,
                 (int)(sizeof(SIZES) / sizeof(SIZES[0])));
  } else if (strcmp(key, "color") == 0) {
    set_if_valid(filters->color, sizeof(filters->color), value, COLORS,
                 (int)(sizeof(COLORS) / sizeof(COLORS[0])));
  } else if (strcmp(key, "type") == 0) {
    set_if_valid(filters->type, sizeof(filters->type), value, TYPES,
                 (int)(sizeof(TYPES) / sizeof(TYPES[0])));
  } else if (strcmp(key, "layout") == 0) {
    set_if_valid(filters->layout, sizeof(filters->layout), value, LAYOUTS,
                 (int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])));
  } else if (strcmp(key, "license") == 0) {
    set_if_valid(filters->license, sizeof(filters->license), value, LICENSES,
                 (int)(sizeof(LICENSES) / sizeof(LICENSES[0])));
  }
}

void image_filters_from_params(ImageFilters *filters, UrlParams *params) {
  image_filters_defaults(filters);
  if (!params)
    return;

  for (int i = 0; i < params->count; i++) {
    read_param(filters, params->params[i].key, params->params[i].value);
  }
}

static int append_qft_part(char *qft, size_t qft_size, const char *part) {
  size_t len = strlen(qft);
  if (len >= qft_size - 1)
    return -1;
  int written = snprintf(qft + len, qft_size - len, "%s", part);
  return (written < 0 || (size_t)written >= qft_size - len) ? -1 : 0;
}

static int build_qft(const ImageFilters *filters, char *qft, size_t qft_size) {
  if (!filters || !qft || qft_size == 0)
    return -1;

  qft[0] = '\0';

  if (filters->size[0]) {
    if (strcmp(filters->size, "wallpaper") == 0) {
      if (append_qft_part(qft, qft_size, "+filterui:imagesize-wallpaper") != 0)
        return -1;
    } else {
      char part[64];
      snprintf(part, sizeof(part), "+filterui:imagesize-%s", filters->size);
      if (append_qft_part(qft, qft_size, part) != 0)
        return -1;
    }
  }

  if (filters->color[0]) {
    char part[64];
    if (strcmp(filters->color, "color") == 0) {
      snprintf(part, sizeof(part), "+filterui:color2-color");
    } else if (strcmp(filters->color, "bw") == 0) {
      snprintf(part, sizeof(part), "+filterui:color2-bw");
    } else {
      char upper[16];
      size_t i;
      for (i = 0; filters->color[i] && i < sizeof(upper) - 1; i++) {
        char c = filters->color[i];
        upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
      }
      upper[i] = '\0';
      snprintf(part, sizeof(part), "+filterui:color2-FGcls_%s", upper);
    }
    if (append_qft_part(qft, qft_size, part) != 0)
      return -1;
  }

  if (filters->type[0]) {
    char part[64];
    if (strcmp(filters->type, "animated") == 0) {
      snprintf(part, sizeof(part), "+filterui:photo-animatedgif");
    } else {
      snprintf(part, sizeof(part), "+filterui:photo-%s", filters->type);
    }
    if (append_qft_part(qft, qft_size, part) != 0)
      return -1;
  }

  if (filters->layout[0]) {
    char part[64];
    snprintf(part, sizeof(part), "+filterui:aspect-%s", filters->layout);
    if (append_qft_part(qft, qft_size, part) != 0)
      return -1;
  }

  if (filters->license[0]) {
    char part[80];
    if (strcmp(filters->license, "any") == 0) {
      snprintf(part, sizeof(part), "+filterui:licenseType-Any");
    } else if (strcmp(filters->license, "public") == 0) {
      snprintf(part, sizeof(part), "+filterui:license-L1");
    } else if (strcmp(filters->license, "share") == 0) {
      snprintf(part, sizeof(part), "+filterui:license-L2_L3_L4_L5_L6_L7");
    } else if (strcmp(filters->license, "sharecommercial") == 0) {
      snprintf(part, sizeof(part), "+filterui:license-L2_L3_L4");
    } else if (strcmp(filters->license, "modify") == 0) {
      snprintf(part, sizeof(part), "+filterui:license-L2_L3_L5_L6");
    } else if (strcmp(filters->license, "modifycommercial") == 0) {
      snprintf(part, sizeof(part), "+filterui:license-L2_L3");
    }
    if (append_qft_part(qft, qft_size, part) != 0)
      return -1;
  }

  if (filters->time[0]) {
    char part[64];
    if (strcmp(filters->time, "day") == 0) {
      snprintf(part, sizeof(part), "+filterui:age-lt1440");
    } else if (strcmp(filters->time, "week") == 0) {
      snprintf(part, sizeof(part), "+filterui:age-lt10080");
    } else if (strcmp(filters->time, "month") == 0) {
      snprintf(part, sizeof(part), "+filterui:age-lt43200");
    } else if (strcmp(filters->time, "year") == 0) {
      snprintf(part, sizeof(part), "+filterui:age-lt525600");
    }
    if (append_qft_part(qft, qft_size, part) != 0)
      return -1;
  }

  return 0;
}

static int append_to_url(char *url, size_t url_size, const char *fragment) {
  size_t len = strlen(url);
  if (len >= url_size - 1)
    return -1;
  int written = snprintf(url + len, url_size - len, "%s", fragment);
  return (written < 0 || (size_t)written >= url_size - len) ? -1 : 0;
}

int image_filters_append_to_url(const ImageFilters *filters, char *url,
                                size_t url_size) {
  if (!filters || !url || url_size == 0)
    return -1;

  if (strcmp(filters->safe, "strict") == 0) {
    if (append_to_url(url, url_size, "&adlt=strict") != 0)
      return -1;
  } else if (strcmp(filters->safe, "off") == 0) {
    if (append_to_url(url, url_size, "&adlt=off") != 0)
      return -1;
  } else if (strcmp(filters->safe, "moderate") == 0) {
    if (append_to_url(url, url_size, "&adlt=moderate") != 0)
      return -1;
  }

  if (filters->region[0]) {
    char part[64];
    snprintf(part, sizeof(part), "&setmkt=%s", filters->region);
    if (append_to_url(url, url_size, part) != 0)
      return -1;
  }

  char qft[512];
  if (build_qft(filters, qft, sizeof(qft)) != 0)
    return -1;
  if (qft[0] == '\0')
    return 0;

  CURL *curl = curl_easy_init();
  if (!curl)
    return -1;

  char *encoded = curl_easy_escape(curl, qft, (int)strlen(qft));
  curl_easy_cleanup(curl);
  if (!encoded)
    return -1;

  char part[1024];
  snprintf(part, sizeof(part), "&qft=%s", encoded);
  curl_free(encoded);

  return append_to_url(url, url_size, part);
}

static int append_query_param(char *buf, size_t bufsize, const char *key,
                              const char *value) {
  size_t len = strlen(buf);
  if (len >= bufsize - 1)
    return -1;

  CURL *curl = curl_easy_init();
  if (!curl)
    return -1;

  char *encoded = curl_easy_escape(curl, value, 0);
  curl_easy_cleanup(curl);
  if (!encoded)
    return -1;

  int written =
      snprintf(buf + len, bufsize - len, "%s%s=%s", len ? "&" : "&", key, encoded);
  curl_free(encoded);
  return (written < 0 || (size_t)written >= bufsize - len) ? -1 : 0;
}

int image_filters_build_query_string(const ImageFilters *filters, char *buf,
                                     size_t bufsize) {
  if (!filters || !buf || bufsize == 0)
    return -1;

  buf[0] = '\0';

  if (filters->region[0] &&
      append_query_param(buf, bufsize, "mkt", filters->region) != 0)
    return -1;

  if (filters->safe[0] && strcmp(filters->safe, "moderate") != 0 &&
      append_query_param(buf, bufsize, "safe", filters->safe) != 0)
    return -1;

  if (filters->time[0] &&
      append_query_param(buf, bufsize, "when", filters->time) != 0)
    return -1;

  if (filters->size[0] &&
      append_query_param(buf, bufsize, "size", filters->size) != 0)
    return -1;

  if (filters->color[0] &&
      append_query_param(buf, bufsize, "color", filters->color) != 0)
    return -1;

  if (filters->type[0] &&
      append_query_param(buf, bufsize, "type", filters->type) != 0)
    return -1;

  if (filters->layout[0] &&
      append_query_param(buf, bufsize, "layout", filters->layout) != 0)
    return -1;

  if (filters->license[0] &&
      append_query_param(buf, bufsize, "license", filters->license) != 0)
    return -1;

  return 0;
}
