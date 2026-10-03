#ifndef BEAKER_H
#define BEAKER_H

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONTEXT_VARS 32
#define MAX_KEY_LEN 64
#define MAX_VALUE_LEN 8192
#define MAX_PATH_LEN 8192
#define MAX_HANDLERS 32
#define BUFFER_SIZE 4096
#define MAX_URL_PARAMS 16
#define MAX_COOKIES 10
#define MAX_OUTER_ARRAY_ITEMS 100
#define MAX_INNER_ARRAY_ITEMS 200

#define TEMPLATES_DIR "templates/"
#define STATIC_DIR "static/"
#define LOCALES_DIR "locales/"

#define INITIAL_LOCALES_CAPACITY 8
#define INITIAL_LOCALE_KEYS_CAPACITY 16
#define MAX_LOCALES_HARD 1024
#define MAX_LOCALE_KEYS_HARD 65536
#define MAX_LOCALE_ID_LEN 64
#define MAX_LOCALE_VALUE_LEN 512

typedef enum {
  CONTEXT_TYPE_STRING,
  CONTEXT_TYPE_STRING_ARRAY,
  CONTEXT_TYPE_STRING_2D_ARRAY,
} ContextType;

typedef struct {
  char key[MAX_KEY_LEN];
  ContextType type;
  union {
    char string_val[MAX_VALUE_LEN];
    struct {
      char **values;
      int count;
    } string_array_data;
    struct {
      char ***values;
      int outer_count;
      int *inner_counts;
    } string_2d_array_data;
  } value;
} ContextVar;

typedef struct {
  ContextVar vars[MAX_CONTEXT_VARS];
  int count;
} TemplateContext;

typedef struct {
  char key[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
} UrlParam;

typedef struct {
  UrlParam params[MAX_URL_PARAMS];
  int count;
} UrlParams;

typedef struct {
  char name[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
  char expires[MAX_VALUE_LEN];
  char path[MAX_KEY_LEN];
  bool http_only;
  bool secure;
} Cookie;

typedef struct {
  char id[MAX_LOCALE_ID_LEN];
  char name[MAX_VALUE_LEN];
  char direction[16];
} LocaleMeta;

typedef struct {
  char key[MAX_KEY_LEN];
  char value[MAX_LOCALE_VALUE_LEN];
} LocaleKV;

typedef struct {
  LocaleMeta meta;
  LocaleKV *keys;
  int key_count;
  int key_capacity;
} Locale;

typedef struct {
  LocaleMeta meta;
} LocaleInfo;

typedef int (*RequestHandler)(UrlParams *params);

typedef struct {
  char path[MAX_PATH_LEN];
  RequestHandler handler;
} RouteHandler;

typedef struct {
  char remote_addr[32];
} RequestInfo;

TemplateContext new_context();
void context_set(TemplateContext *ctx, const char *key, const char *value);
void context_set_string_array(TemplateContext *ctx, const char *key,
                              char *values[], int count);
void context_set_array_of_arrays(TemplateContext *ctx, const char *key,
                                 char **values_2d[], int outer_count,
                                 int inner_counts[]);
void free_context(TemplateContext *ctx);
char *render_template(const char *template_file, TemplateContext *ctx);

int beaker_load_locales(void);
void beaker_set_locale(TemplateContext *ctx, const char *locale_id);
int beaker_get_all_locales(LocaleInfo *out, int max_count);
const LocaleMeta *beaker_get_locale_meta(const char *locale_id);
const char *beaker_get_locale_value(const char *locale_id, const char *key);
void beaker_free_locales(void);

void send_response(const char *html);
void send_response_with_status(const char *status_line, const char *html,
                               const char *extra_headers);
void send_redirect(const char *location);

void beaker_set_csp_policy(const char *policy);
void beaker_set_permissions_policy(const char *policy);
void beaker_set_hsts_max_age_sec(long max_age_sec);
void beaker_set_hsts_include_subdomains(int include);

void send_status(const char *status_line);
void set_cookie(const char *name, const char *value, const char *expires,
                const char *path, bool http_only, bool secure);
char *get_cookie(const char *cookie_name);

void set_handler(const char *path, RequestHandler handler);
char *parse_request_url(const char *request_line, UrlParams *params);
const char *get_mime_type(const char *file_path);
bool serve_static_file(const char *request_path_relative_to_static);
bool serve_static_file_with_mime(const char *request_path_relative_to_static,
                                 const char *mime_type);
bool serve_data(const char *data, size_t size, const char *mime_type);

int beaker_run(const char *ip, int *port);
int beaker_run_with_threads(const char *ip, int *port, int num_workers);

const char *beaker_get_remote_addr(void);
const char *beaker_get_header(const char *name);
void beaker_set_request_buffer(const char *buffer);

#endif
