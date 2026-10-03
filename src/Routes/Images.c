#include "Images.h"
#include "../Routes/ImageProxy.h"
#include "../Scraping/ImageFilters.h"
#include "../Scraping/ImageScraping.h"
#include "../Cache/Cache.h"
#include "../Limiter/RateLimit.h"
#include "../Utility/ErrorPage.h"
#include "../Utility/Unescape.h"
#include "../Utility/Utility.h"
#include "Config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

extern Config global_config;

static char *build_images_request_cache_key(const char *query, int page,
                                            const char *client_key) {
  char scope_key[BUFFER_SIZE_MEDIUM];
  snprintf(scope_key, sizeof(scope_key), "images_request:%s",
           client_key ? client_key : "unknown");
  return cache_compute_key(query, page, scope_key);
}

static void format_elapsed_time(double secs, char *buf, size_t bufsize) {
  if (secs < 1.0) {
    snprintf(buf, bufsize, "%.2f seconds", secs);
  } else if (secs < 60.0) {
    snprintf(buf, bufsize, "%.2f seconds", secs);
  } else if (secs < 3600.0) {
    int m = (int)(secs / 60);
    int s = (int)(secs - m * 60);
    if (s > 0)
      snprintf(buf, bufsize, "%d minute%s %d second%s", m, m == 1 ? "" : "s",
               s, s == 1 ? "" : "s");
    else
      snprintf(buf, bufsize, "%d minute%s", m, m == 1 ? "" : "s");
  } else {
    int h = (int)(secs / 3600);
    int m = (int)((secs - h * 3600) / 60);
    if (m > 0)
      snprintf(buf, bufsize, "%d hour%s %d minute%s", h, h == 1 ? "" : "s", m,
               m == 1 ? "" : "s");
    else
      snprintf(buf, bufsize, "%d hour%s", h, h == 1 ? "" : "s");
  }
}

static void set_filter_context(TemplateContext *ctx, const ImageFilters *filters) {
  context_set(ctx, "filter_mkt", filters->region);
  context_set(ctx, "filter_safe", filters->safe);
  context_set(ctx, "filter_when", filters->time);
  context_set(ctx, "filter_size", filters->size);
  context_set(ctx, "filter_color", filters->color);
  context_set(ctx, "filter_type", filters->type);
  context_set(ctx, "filter_layout", filters->layout);
  context_set(ctx, "filter_license", filters->license);
}

int images_handler(UrlParams *params) {
  TemplateContext ctx = new_context();
  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  const char *rate_limit_msg =
      beaker_get_locale_value(locale, "rate_limit_images");
  if (!rate_limit_msg)
    rate_limit_msg = "Slow down! Too many image searches from you!";
  const char *error_images_msg =
      beaker_get_locale_value(locale, "error_images");
  if (!error_images_msg)
    error_images_msg = "Error fetching images";
  const char *error_render_msg =
      beaker_get_locale_value(locale, "error_render");
  if (!error_render_msg)
    error_render_msg = "Error rendering results";
  free(locale);
  char *raw_query = "";
  int page = 1;
  ImageFilters filters;
  image_filters_from_params(&filters, params);

  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0) {
        raw_query = params->params[i].value;
      } else if (strcmp(params->params[i].key, "p") == 0) {
        int parsed = atoi(params->params[i].value);
        if (parsed > 1)
          page = parsed;
      }
    }
  }

  if (!raw_query || strlen(raw_query) == 0) {
    char *rendered = render_template("images-home.html", &ctx);
    if (rendered) {
      send_response(rendered);
      free(rendered);
    } else {
      send_redirect("/");
    }
    free_context(&ctx);
    return 0;
  }

  char filter_qs[512];
  if (image_filters_build_query_string(&filters, filter_qs, sizeof(filter_qs)) != 0)
    filter_qs[0] = '\0';

  char page_str[16], prev_str[16], next_str[16], two_prev_str[16],
      two_next_str[16];

  snprintf(page_str, sizeof(page_str), "%d", page);
  snprintf(prev_str, sizeof(prev_str), "%d", page > 1 ? page - 1 : 0);
  snprintf(next_str, sizeof(next_str), "%d", page + 1);
  snprintf(two_prev_str, sizeof(two_prev_str), "%d", page > 2 ? page - 2 : 0);
  snprintf(two_next_str, sizeof(two_next_str), "%d", page + 2);
  context_set(&ctx, "query", raw_query);
  context_set(&ctx, "page", page_str);
  context_set(&ctx, "prev_page", prev_str);
  context_set(&ctx, "next_page", next_str);
  context_set(&ctx, "two_prev_page", two_prev_str);
  context_set(&ctx, "two_next_page", two_next_str);
  context_set(&ctx, "filter_qs", filter_qs);
  if (filter_qs[0])
    context_set(&ctx, "has_image_filters", "1");
  set_filter_context(&ctx, &filters);

  char *display_query = url_decode_query(raw_query);
  context_set(&ctx, "query", display_query);

  char client_key[BUFFER_SIZE_SMALL];
  rate_limit_get_client_key(client_key, sizeof(client_key));

  char *request_cache_key =
      build_images_request_cache_key(raw_query, page, client_key);
  int request_is_cached = 0;

  if (request_cache_key && get_cache_ttl_infobox() > 0) {
    char *cached_marker = NULL;
    size_t cached_marker_size = 0;

    if (cache_get(request_cache_key, (time_t)get_cache_ttl_infobox(),
                  &cached_marker, &cached_marker_size) == 0) {
      request_is_cached = 1;
    }

    free(cached_marker);
  }

  if (!request_is_cached) {
    RateLimitConfig rate_limit_config = {
        .max_requests = global_config.rate_limit_images_requests,
        .interval_seconds = global_config.rate_limit_images_interval,
    };
    RateLimitResult rate_limit_result =
        rate_limit_check("images", &rate_limit_config);
    if (rate_limit_result.limited) {
      char retry_after_header[64];
      char *page_html = render_error_page(rate_limit_msg, "", "/images");
      snprintf(retry_after_header, sizeof(retry_after_header),
               "Retry-After: %d", rate_limit_result.retry_after_seconds);
      if (page_html) {
        send_response_with_status("429 Too Many Requests", page_html,
                                  retry_after_header);
        free(page_html);
      } else {
        send_response_with_status("429 Too Many Requests", "<h1>429</h1>",
                                  retry_after_header);
      }
      free(request_cache_key);
      free(display_query);
      free_context(&ctx);
      return -1;
    }

    if (request_cache_key && get_cache_ttl_infobox() > 0) {
      cache_set(request_cache_key, "1", 1);
    }
  }
  free(request_cache_key);

  ImageResult *results = NULL;
  int result_count = 0;

  struct timeval tv_start, tv_end;
  gettimeofday(&tv_start, NULL);
  int scrape_ok = scrape_images(raw_query, page, &filters, &results,
                                &result_count) == 0;
  gettimeofday(&tv_end, NULL);
  double elapsed =
      (tv_end.tv_sec - tv_start.tv_sec) +
      (tv_end.tv_usec - tv_start.tv_usec) / 1000000.0;
  char search_time_buf[64];
  format_elapsed_time(elapsed, search_time_buf, sizeof(search_time_buf));
  context_set(&ctx, "search_time", search_time_buf);

  if (!scrape_ok) {
    char *page_html = render_error_page(error_images_msg, "", "/images");
    if (page_html) {
      send_response_with_status("502 Bad Gateway", page_html, NULL);
      free(page_html);
    } else {
      send_status("502 Bad Gateway");
    }
    free(display_query);
    free_context(&ctx);
    return -1;
  }

  char ***image_matrix = NULL;
  int *inner_counts = NULL;
  if (result_count > 0) {
    image_matrix = malloc(sizeof(char **) * result_count);
    inner_counts = malloc(sizeof(int) * result_count);

    if (!image_matrix || !inner_counts) {
      if (image_matrix)
        free(image_matrix);
      if (inner_counts)
        free(inner_counts);
      free_image_results(results, result_count);
      free(display_query);
      free_context(&ctx);
      return -1;
    }

    for (int i = 0; i < result_count; i++) {
      image_matrix[i] = malloc(sizeof(char *) * IMAGE_RESULT_FIELDS);
      image_matrix[i][0] = strdup(results[i].thumbnail_url);
      image_matrix[i][1] = strdup(results[i].title);
      image_matrix[i][2] = strdup(results[i].page_url);
      char *full_wrapped = proxy_wrap_image_url(results[i].full_url);
      image_matrix[i][3] =
          full_wrapped ? full_wrapped : strdup(results[i].full_url);
      inner_counts[i] = IMAGE_RESULT_FIELDS;
    }

    context_set_array_of_arrays(&ctx, "images", image_matrix, result_count,
                                inner_counts);
  }

  char *rendered = render_template("images.html", &ctx);
  if (rendered) {
    send_response(rendered);
    free(rendered);
  } else {
    char *page_html = render_error_page(error_render_msg, "", "/images");
    if (page_html) {
      send_response_with_status("500 Internal Server Error", page_html, NULL);
      free(page_html);
    } else {
      send_status("500 Internal Server Error");
    }
  }

  if (image_matrix)
    free_string_matrix(image_matrix, inner_counts, result_count);

  free_image_results(results, result_count);
  free(display_query);
  free_context(&ctx);

  return 0;
}
