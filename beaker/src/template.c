#include "../beaker.h"
#include "beaker_globals.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *render_template_segment(const char *template_segment,
                                     TemplateContext *ctx);

static ContextVar *find_context_var(TemplateContext *ctx, const char *key) {
  if (ctx == NULL || key == NULL) {
    beaker_log("ERROR", "find_context_var: Invalid NULL input (ctx or key).\n");
    return NULL;
  }
  for (int i = 0; i < ctx->count; i++) {
    if (strcmp(ctx->vars[i].key, key) == 0) {
      return &ctx->vars[i];
    }
  }
  return NULL;
}

static void free_string_array_var(ContextVar *var) {
  if (var == NULL || var->value.string_array_data.values == NULL) {
    return;
  }
  for (int i = 0; i < var->value.string_array_data.count; i++) {
    if (var->value.string_array_data.values[i] != NULL) {
      free(var->value.string_array_data.values[i]);
      var->value.string_array_data.values[i] = NULL;
    }
  }
  free(var->value.string_array_data.values);
  var->value.string_array_data.values = NULL;
  var->value.string_array_data.count = 0;
}

static void free_string_2d_array_var(ContextVar *var) {
  if (var == NULL || var->value.string_2d_array_data.values == NULL) {
    return;
  }
  for (int i = 0; i < var->value.string_2d_array_data.outer_count; i++) {
    if (var->value.string_2d_array_data.values[i] != NULL) {
      for (int j = 0; j < var->value.string_2d_array_data.inner_counts[i];
           j++) {
        if (var->value.string_2d_array_data.values[i][j] != NULL) {
          free(var->value.string_2d_array_data.values[i][j]);
          var->value.string_2d_array_data.values[i][j] = NULL;
        }
      }
      free(var->value.string_2d_array_data.values[i]);
      var->value.string_2d_array_data.values[i] = NULL;
    }
  }
  free(var->value.string_2d_array_data.values);
  var->value.string_2d_array_data.values = NULL;

  if (var->value.string_2d_array_data.inner_counts != NULL) {
    free(var->value.string_2d_array_data.inner_counts);
    var->value.string_2d_array_data.inner_counts = NULL;
  }
  var->value.string_2d_array_data.outer_count = 0;
}

static void free_context_var(ContextVar *var) {
  if (var == NULL) {
    return;
  }

  switch (var->type) {
  case CONTEXT_TYPE_STRING:
    break;
  case CONTEXT_TYPE_STRING_ARRAY:
    free_string_array_var(var);
    break;
  case CONTEXT_TYPE_STRING_2D_ARRAY:
    free_string_2d_array_var(var);
    break;
  }
}

TemplateContext new_context() {
  TemplateContext ctx;
  ctx.count = 0;
  return ctx;
}

void context_set(TemplateContext *ctx, const char *key, const char *value) {
  if (ctx == NULL || key == NULL || value == NULL) {
    beaker_log("ERROR",
               "context_set: Invalid NULL input (ctx, key, or value).\n");
    return;
  }

  ContextVar *var = find_context_var(ctx, key);
  if (var != NULL) {
    free_context_var(var);

    strncpy(var->value.string_val, value, MAX_VALUE_LEN - 1);
    var->value.string_val[MAX_VALUE_LEN - 1] = '\0';
    var->type = CONTEXT_TYPE_STRING;
    return;
  }

  if (ctx->count < MAX_CONTEXT_VARS) {
    ContextVar *new_var = &ctx->vars[ctx->count];
    strncpy(new_var->key, key, MAX_KEY_LEN - 1);
    new_var->key[MAX_KEY_LEN - 1] = '\0';
    strncpy(new_var->value.string_val, value, MAX_VALUE_LEN - 1);
    new_var->value.string_val[MAX_VALUE_LEN - 1] = '\0';
    new_var->type = CONTEXT_TYPE_STRING;
    ctx->count++;
  } else {
    beaker_log("WARN",
               "context_set: TemplateContext is full. Cannot add key "
               "'%s'.\n",
               key);
  }
}

void context_set_string_array(TemplateContext *ctx, const char *key,
                              char *values[], int count) {
  if (ctx == NULL || key == NULL || values == NULL) {
    beaker_log("ERROR", "context_set_string_array: Invalid NULL input "
                        "(ctx, key, or values).\n");
    return;
  }
  if (count < 0 || count > MAX_OUTER_ARRAY_ITEMS) {
    beaker_log("ERROR",
               "context_set_string_array: Invalid count %d for string "
               "array context '%s'. Max %d allowed.\n",
               count, key, MAX_OUTER_ARRAY_ITEMS);
    return;
  }

  ContextVar *var = find_context_var(ctx, key);
  if (var != NULL) {
    free_context_var(var);
  } else {

    if (ctx->count < MAX_CONTEXT_VARS) {
      var = &ctx->vars[ctx->count];
      strncpy(var->key, key, MAX_KEY_LEN - 1);
      var->key[MAX_KEY_LEN - 1] = '\0';
      ctx->count++;
    } else {
      beaker_log("WARN",
                 "context_set_string_array: TemplateContext is full. "
                 "Cannot add string array key '%s'.\n",
                 key);
      return;
    }
  }

  var->type = CONTEXT_TYPE_STRING_ARRAY;
  var->value.string_array_data.values = (char **)malloc(sizeof(char *) * count);
  if (var->value.string_array_data.values == NULL) {
    beaker_log_errno_format(
        "ERROR",
        "context_set_string_array: Allocation failed for values "
        "for key '%s'.\n",
        key);
    return;
  }
  var->value.string_array_data.count = count;

  for (int i = 0; i < count; i++) {
    if (values[i] == NULL) {
      var->value.string_array_data.values[i] = NULL;
      continue;
    }
    var->value.string_array_data.values[i] = strdup(values[i]);
    if (var->value.string_array_data.values[i] == NULL) {
      beaker_log_errno_format(
          "ERROR",
          "context_set_string_array: Failed to duplicate value for "
          "item %d of key '%s'.\n",
          i, key);

      for (int j = 0; j < i; j++) {
        if (var->value.string_array_data.values[j] != NULL) {
          free(var->value.string_array_data.values[j]);
        }
      }
      free(var->value.string_array_data.values);
      var->value.string_array_data.values = NULL;
      var->value.string_array_data.count = 0;
      return;
    }
  }
}

void context_set_array_of_arrays(TemplateContext *ctx, const char *key,
                                 char **values_2d[], int outer_count,
                                 int inner_counts[]) {
  if (ctx == NULL || key == NULL || values_2d == NULL || inner_counts == NULL) {
    beaker_log("ERROR", "context_set_array_of_arrays: Invalid NULL input "
                        "(ctx, key, values_2d, or inner_counts).\n");
    return;
  }
  if (outer_count < 0 || outer_count > MAX_OUTER_ARRAY_ITEMS) {
    beaker_log("ERROR",
               "context_set_array_of_arrays: Invalid outer count %d for "
               "array of arrays context '%s'. Max %d allowed.\n",
               outer_count, key, MAX_OUTER_ARRAY_ITEMS);
    return;
  }

  ContextVar *var = find_context_var(ctx, key);
  if (var != NULL) {
    free_context_var(var);
  } else {

    if (ctx->count < MAX_CONTEXT_VARS) {
      var = &ctx->vars[ctx->count];
      strncpy(var->key, key, MAX_KEY_LEN - 1);
      var->key[MAX_KEY_LEN - 1] = '\0';
      ctx->count++;
    } else {
      beaker_log("WARN",
                 "context_set_array_of_arrays: TemplateContext is full. "
                 "Cannot add array of arrays key '%s'.\n",
                 key);
      return;
    }
  }

  var->type = CONTEXT_TYPE_STRING_2D_ARRAY;
  var->value.string_2d_array_data.values =
      (char ***)malloc(sizeof(char **) * outer_count);
  var->value.string_2d_array_data.inner_counts =
      (int *)malloc(sizeof(int) * outer_count);

  if (var->value.string_2d_array_data.values == NULL ||
      var->value.string_2d_array_data.inner_counts == NULL) {
    beaker_log_errno_format(
        "ERROR",
        "context_set_array_of_arrays: Allocation failed for key "
        "'%s'.\n",
        key);
    free(var->value.string_2d_array_data.values);
    free(var->value.string_2d_array_data.inner_counts);
    var->value.string_2d_array_data.values = NULL;
    var->value.string_2d_array_data.inner_counts = NULL;
    return;
  }
  var->value.string_2d_array_data.outer_count = outer_count;

  for (int i = 0; i < outer_count; i++) {
    int current_inner_count = inner_counts[i];
    if (current_inner_count < 0 ||
        current_inner_count > MAX_INNER_ARRAY_ITEMS) {
      beaker_log("ERROR",
                 "context_set_array_of_arrays: Invalid inner count %d for "
                 "item %d in 2D array '%s'. Max %d allowed.\n",
                 current_inner_count, i, key, MAX_INNER_ARRAY_ITEMS);

      for (int k = 0; k < i; k++) {
        if (var->value.string_2d_array_data.values[k] != NULL) {
          for (int l = 0; l < var->value.string_2d_array_data.inner_counts[k];
               l++) {
            if (var->value.string_2d_array_data.values[k][l] != NULL) {
              free(var->value.string_2d_array_data.values[k][l]);
            }
          }
          free(var->value.string_2d_array_data.values[k]);
        }
      }
      free(var->value.string_2d_array_data.values);
      free(var->value.string_2d_array_data.inner_counts);
      var->value.string_2d_array_data.values = NULL;
      var->value.string_2d_array_data.inner_counts = NULL;
      return;
    }

    var->value.string_2d_array_data.values[i] =
        (char **)malloc(sizeof(char *) * current_inner_count);
    if (var->value.string_2d_array_data.values[i] == NULL) {
      beaker_log_errno_format(
          "ERROR",
          "context_set_array_of_arrays: Allocation failed for "
          "inner array %d of key '%s'.\n",
          i, key);

      for (int k = 0; k < i; k++) {
        if (var->value.string_2d_array_data.values[k] != NULL) {
          for (int l = 0; l < var->value.string_2d_array_data.inner_counts[k];
               l++) {
            if (var->value.string_2d_array_data.values[k][l] != NULL) {
              free(var->value.string_2d_array_data.values[k][l]);
            }
          }
          free(var->value.string_2d_array_data.values[k]);
        }
      }
      free(var->value.string_2d_array_data.values);
      free(var->value.string_2d_array_data.inner_counts);
      var->value.string_2d_array_data.values = NULL;
      var->value.string_2d_array_data.inner_counts = NULL;
      return;
    }
    var->value.string_2d_array_data.inner_counts[i] = current_inner_count;

    for (int j = 0; j < current_inner_count; j++) {
      if (values_2d[i][j] == NULL) {
        var->value.string_2d_array_data.values[i][j] = NULL;
        continue;
      }
      var->value.string_2d_array_data.values[i][j] = strdup(values_2d[i][j]);
      if (var->value.string_2d_array_data.values[i][j] == NULL) {
        beaker_log_errno_format(
            "ERROR",
            "context_set_array_of_arrays: Failed to duplicate "
            "value for item [%d][%d] of key '%s'.\n",
            i, j, key);

        for (int k = 0; k <= i; k++) {
          if (var->value.string_2d_array_data.values[k] != NULL) {
            for (int l = 0;
                 l <
                 (k == i ? j : var->value.string_2d_array_data.inner_counts[k]);
                 l++) {
              if (var->value.string_2d_array_data.values[k][l] != NULL) {
                free(var->value.string_2d_array_data.values[k][l]);
              }
            }
            free(var->value.string_2d_array_data.values[k]);
          }
        }
        free(var->value.string_2d_array_data.values);
        free(var->value.string_2d_array_data.inner_counts);
        var->value.string_2d_array_data.values = NULL;
        var->value.string_2d_array_data.inner_counts = NULL;
        return;
      }
    }
  }
}

void free_context(TemplateContext *ctx) {
  if (ctx == NULL) {
    return;
  }
  for (int i = 0; i < ctx->count; i++) {
    free_context_var(&ctx->vars[i]);
  }
  ctx->count = 0;
}

static char *html_escape(const char *input) {
  if (input == NULL) {
    return strdup("");
  }

  size_t input_len = strlen(input);

  size_t estimated_len = input_len * 5 + 1;
  char *output = (char *)malloc(estimated_len);
  if (output == NULL) {
    beaker_log_errno("Failed to allocate memory for HTML escape output");
    return NULL;
  }
  output[0] = '\0';
  size_t current_output_len = 0;

  for (size_t i = 0; i < input_len; i++) {
    const char *replacement = NULL;

    switch (input[i]) {
    case '&':
      replacement = "&amp;";
      break;
    case '<':
      replacement = "&lt;";
      break;
    case '>':
      replacement = "&gt;";
      break;
    case '"':
      replacement = "&quot;";
      break;
    case '\'':
      replacement = "&apos;";
      break;
    default:
      break;
    }

    if (replacement) {
      size_t repl_len = strlen(replacement);

      if (current_output_len + repl_len + 1 > estimated_len) {
        estimated_len = (current_output_len + repl_len + 1) * 2;
        char *new_output = (char *)realloc(output, estimated_len);
        if (new_output == NULL) {
          beaker_log_errno(
              "Failed to reallocate memory for HTML escape output");
          free(output);
          return NULL;
        }
        output = new_output;
      }
      strcat(output, replacement);
      current_output_len += repl_len;
    } else {

      if (current_output_len + 1 + 1 > estimated_len) {
        estimated_len = (current_output_len + 1 + 1) * 2;
        char *new_output = (char *)realloc(output, estimated_len);
        if (new_output == NULL) {
          beaker_log_errno(
              "Failed to reallocate memory for HTML escape output");
          free(output);
          return NULL;
        }
        output = new_output;
      }
      output[current_output_len++] = input[i];
      output[current_output_len] = '\0';
    }
  }
  return output;
}

static void append_to_buffer(char **buffer, size_t *current_len,
                             size_t *max_len, const char *str_to_add) {
  if (str_to_add == NULL) {
    beaker_log("WARN", "append_to_buffer: Attempted to append NULL "
                       "string. Skipping.\n");
    return;
  }

  size_t add_len = strlen(str_to_add);

  if (*current_len + add_len + 1 > *max_len) {
    *max_len = (*current_len + add_len + 1) * 2;

    if (*max_len < BUFFER_SIZE * 2) {
      *max_len = BUFFER_SIZE * 2;
    }

    char *new_buffer = (char *)realloc(*buffer, *max_len);
    if (new_buffer == NULL) {
      beaker_log_errno_format(
          "ERROR",
          "append_to_buffer: Reallocation failed (requested %zu "
          "bytes).\n",
          *max_len);
      free(*buffer);
      *buffer = NULL;

      exit(EXIT_FAILURE);
    }
    *buffer = new_buffer;
  }

  strcat(*buffer, str_to_add);
  *current_len += add_len;
}

static char *parse_indexed_tag(const char *tag_content, int *index_val) {
  *index_val = -1;

  const char *open_bracket = strchr(tag_content, '[');
  if (open_bracket == NULL) {

    char *key_name = strdup(tag_content);
    if (key_name == NULL) {
      beaker_log_errno_format(
          "ERROR", "parse_indexed_tag: strdup failed for '%s'.\n", tag_content);
    }
    return key_name;
  }

  const char *close_bracket = strchr(open_bracket, ']');
  if (close_bracket == NULL) {
    beaker_log("ERROR",
               "parse_indexed_tag: Unclosed bracket in tag '%s'. "
               "Returning raw tag content.\n",
               tag_content);

    char *key_name = strdup(tag_content);
    if (key_name == NULL) {
      beaker_log_errno("Failed to duplicate malformed tag content");
    }
    return key_name;
  }

  size_t key_len = open_bracket - tag_content;
  char *key_name = (char *)malloc(key_len + 1);
  if (key_name == NULL) {
    beaker_log_errno_format(
        "ERROR", "parse_indexed_tag: Allocation failed for key_name.\n");
    return NULL;
  }
  strncpy(key_name, tag_content, key_len);
  key_name[key_len] = '\0';

  size_t index_str_len = close_bracket - (open_bracket + 1);
  char *index_str = (char *)malloc(index_str_len + 1);
  if (index_str == NULL) {
    beaker_log_errno_format(
        "ERROR", "parse_indexed_tag: Allocation failed for index_str.\n");
    free(key_name);
    return NULL;
  }
  strncpy(index_str, open_bracket + 1, index_str_len);
  index_str[index_str_len] = '\0';

  *index_val = atoi(index_str);
  free(index_str);

  return key_name;
}

typedef enum {
  CONDITION_NONE,
  CONDITION_TRUTHY,
  CONDITION_EXISTS,
  CONDITION_EQUAL,
  CONDITION_NOT_EQUAL,
} ConditionType;

typedef struct {
  ConditionType type;
  char var_name[MAX_KEY_LEN];
  char compare_value[MAX_VALUE_LEN];
  int compare_index;
  bool negate;
} Condition;

static const char *resolve_compare_value(const Condition *cond,
                                         TemplateContext *ctx,
                                         char compare_buf[MAX_VALUE_LEN]) {
  const char *compare_value = cond->compare_value;
  ContextVar *compare_var = find_context_var(ctx, cond->compare_value);

  if (cond->compare_index >= 0) {
    if (compare_var != NULL && compare_var->type == CONTEXT_TYPE_STRING_ARRAY &&
        cond->compare_index < compare_var->value.string_array_data.count) {
      compare_value =
          compare_var->value.string_array_data.values[cond->compare_index];
    }
  } else if (compare_var != NULL && compare_var->type == CONTEXT_TYPE_STRING) {
    strncpy(compare_buf, compare_var->value.string_val, MAX_VALUE_LEN - 1);
    compare_buf[MAX_VALUE_LEN - 1] = '\0';
    compare_value = compare_buf;
  }

  return compare_value;
}

static bool evaluate_condition(const Condition *cond, TemplateContext *ctx) {
  if (cond == NULL || cond->type == CONDITION_NONE) {
    return false;
  }

  ContextVar *var = find_context_var(ctx, cond->var_name);
  const char *var_value = NULL;

  if (var == NULL) {
    if (cond->type == CONDITION_TRUTHY) {
      return false;
    }
    var_value = "";
  } else if (var->type == CONTEXT_TYPE_STRING_ARRAY &&
             cond->compare_index >= 0) {
    if (cond->compare_index >= 0 &&
        cond->compare_index < var->value.string_array_data.count) {
      var_value = var->value.string_array_data.values[cond->compare_index];
    } else {
      var_value = "";
    }
  } else if (var->type == CONTEXT_TYPE_STRING) {
    var_value = var->value.string_val;
  } else {
    if (cond->type == CONDITION_TRUTHY) {
      return false;
    }
    var_value = "";
  }

  switch (cond->type) {
  case CONDITION_TRUTHY:
    return (var_value != NULL && strlen(var_value) > 0);

  case CONDITION_EXISTS: {
    bool exists = (var != NULL);
    return cond->negate ? !exists : exists;
  }

  case CONDITION_EQUAL:
  case CONDITION_NOT_EQUAL: {
    char compare_buf[MAX_VALUE_LEN];
    const char *compare_value = resolve_compare_value(cond, ctx, compare_buf);
    bool values_equal = strcmp(var_value, compare_value) == 0;
    return cond->type == CONDITION_EQUAL ? values_equal : !values_equal;
  }

  default:
    return false;
  }
}

static Condition parse_condition(const char *condition_str) {
  Condition cond;
  cond.type = CONDITION_NONE;
  cond.var_name[0] = '\0';
  cond.compare_value[0] = '\0';
  cond.compare_index = -1;
  cond.negate = false;

  if (condition_str == NULL || *condition_str == '\0') {
    return cond;
  }

  const char *eq_pos = strstr(condition_str, "==");
  const char *neq_pos = strstr(condition_str, "!=");

  if (eq_pos != NULL && neq_pos != NULL) {
    if (eq_pos < neq_pos) {
      neq_pos = NULL;
    } else {
      eq_pos = NULL;
    }
  }

  if (eq_pos != NULL) {
    size_t var_len = eq_pos - condition_str;
    if (var_len >= MAX_KEY_LEN) {
      var_len = MAX_KEY_LEN - 1;
    }
    strncpy(cond.var_name, condition_str, var_len);
    cond.var_name[var_len] = '\0';

    const char *value_start = eq_pos + 2;
    while (*value_start == ' ' || *value_start == '\t') {
      value_start++;
    }

    if (*value_start == '"') {
      value_start++;
      const char *value_end = strchr(value_start, '"');
      if (value_end != NULL) {
        size_t val_len = value_end - value_start;
        if (val_len >= MAX_VALUE_LEN) {
          val_len = MAX_VALUE_LEN - 1;
        }
        strncpy(cond.compare_value, value_start, val_len);
        cond.compare_value[val_len] = '\0';
      }
    } else {
      size_t val_len = strlen(value_start);
      if (val_len >= MAX_VALUE_LEN) {
        val_len = MAX_VALUE_LEN - 1;
      }
      strncpy(cond.compare_value, value_start, val_len);
      cond.compare_value[val_len] = '\0';
    }

    cond.type = CONDITION_EQUAL;
  } else if (neq_pos != NULL) {
    size_t var_len = neq_pos - condition_str;
    if (var_len >= MAX_KEY_LEN) {
      var_len = MAX_KEY_LEN - 1;
    }
    strncpy(cond.var_name, condition_str, var_len);
    cond.var_name[var_len] = '\0';

    const char *value_start = neq_pos + 2;
    while (*value_start == ' ' || *value_start == '\t') {
      value_start++;
    }

    if (*value_start == '"') {
      value_start++;
      const char *value_end = strchr(value_start, '"');
      if (value_end != NULL) {
        size_t val_len = value_end - value_start;
        if (val_len >= MAX_VALUE_LEN) {
          val_len = MAX_VALUE_LEN - 1;
        }
        strncpy(cond.compare_value, value_start, val_len);
        cond.compare_value[val_len] = '\0';
      }
    } else {
      size_t val_len = strlen(value_start);
      if (val_len >= MAX_VALUE_LEN) {
        val_len = MAX_VALUE_LEN - 1;
      }
      strncpy(cond.compare_value, value_start, val_len);
      cond.compare_value[val_len] = '\0';
    }

    cond.type = CONDITION_NOT_EQUAL;
  } else if (strncmp(condition_str, "exists ", 7) == 0) {
    const char *var_start = condition_str + 7;
    while (*var_start == ' ' || *var_start == '\t') {
      var_start++;
    }
    size_t var_len = strlen(var_start);
    if (var_len >= MAX_KEY_LEN) {
      var_len = MAX_KEY_LEN - 1;
    }
    strncpy(cond.var_name, var_start, var_len);
    cond.var_name[var_len] = '\0';
    cond.type = CONDITION_EXISTS;
  } else if (strncmp(condition_str, "not exists ", 11) == 0) {
    const char *var_start = condition_str + 11;
    while (*var_start == ' ' || *var_start == '\t') {
      var_start++;
    }
    size_t var_len = strlen(var_start);
    if (var_len >= MAX_KEY_LEN) {
      var_len = MAX_KEY_LEN - 1;
    }
    strncpy(cond.var_name, var_start, var_len);
    cond.var_name[var_len] = '\0';
    cond.type = CONDITION_EXISTS;
    cond.negate = true;
  } else {
    size_t var_len = strlen(condition_str);
    if (var_len >= MAX_KEY_LEN) {
      var_len = MAX_KEY_LEN - 1;
    }
    strncpy(cond.var_name, condition_str, var_len);
    cond.var_name[var_len] = '\0';
    cond.type = CONDITION_TRUTHY;
  }

  char *p = cond.var_name;
  char *end = cond.var_name + strlen(cond.var_name) - 1;
  while (p < end && (*end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }

  char *var_bracket = strchr(cond.var_name, '[');
  if (var_bracket != NULL) {
    char *close_bracket = strchr(var_bracket, ']');
    if (close_bracket != NULL) {
      *var_bracket = '\0';
      cond.compare_index = atoi(var_bracket + 1);
    }
  }

  char *bracket = strchr(cond.compare_value, '[');
  if (bracket != NULL) {
    char *close_bracket = strchr(bracket, ']');
    if (close_bracket != NULL) {
      *bracket = '\0';
      cond.compare_index = atoi(bracket + 1);
    }
  }

  return cond;
}

static char *render_template_segment(const char *template_segment,
                                     TemplateContext *ctx) {
  size_t initial_max_len = BUFFER_SIZE;
  char *rendered_buffer = (char *)malloc(initial_max_len);
  if (rendered_buffer == NULL) {
    beaker_log_errno_format(
        "ERROR",
        "render_template_segment: Failed to allocate initial %zu "
        "bytes for rendered_buffer.\n",
        initial_max_len);
    return NULL;
  }
  rendered_buffer[0] = '\0';
  size_t current_len = 0;
  size_t max_len = initial_max_len;

  const char *current_pos = template_segment;
  const char *start_tag;

  while ((start_tag = strstr(current_pos, "{{")) != NULL) {

    size_t text_len = start_tag - current_pos;
    if (text_len > 0) {
      char *text_before_tag = (char *)malloc(text_len + 1);
      if (text_before_tag == NULL) {
        beaker_log_errno_format("ERROR",
                                "render_template_segment: Allocation failed "
                                "for text_before_tag.\n");
        free(rendered_buffer);
        return NULL;
      }
      strncpy(text_before_tag, current_pos, text_len);
      text_before_tag[text_len] = '\0';
      append_to_buffer(&rendered_buffer, &current_len, &max_len,
                       text_before_tag);
      free(text_before_tag);
    }

    const char *end_tag = strstr(start_tag, "}}");
    if (end_tag == NULL) {
      beaker_log("ERROR", "render_template_segment: Unclosed '{{' tag. "
                          "Appending remaining template content as-is.\n");
      append_to_buffer(&rendered_buffer, &current_len, &max_len, start_tag);
      return rendered_buffer;
    }

    size_t tag_content_len = end_tag - (start_tag + 2);
    char *tag_content_raw = (char *)malloc(tag_content_len + 1);
    if (tag_content_raw == NULL) {
      beaker_log_errno_format("ERROR",
                              "render_template_segment: Allocation failed for "
                              "tag_content_raw.\n");
      free(rendered_buffer);
      return NULL;
    }
    strncpy(tag_content_raw, start_tag + 2, tag_content_len);
    tag_content_raw[tag_content_len] = '\0';

    char *trimmed_tag_content = tag_content_raw;
    while (*trimmed_tag_content != '\0' &&
           (*trimmed_tag_content == ' ' || *trimmed_tag_content == '\t' ||
            *trimmed_tag_content == '\n' || *trimmed_tag_content == '\r')) {
      trimmed_tag_content++;
    }

    if (*trimmed_tag_content == '\0') {
      free(tag_content_raw);
      current_pos = end_tag + 2;
      continue;
    } else {
      char *end_trimmed = trimmed_tag_content + strlen(trimmed_tag_content) - 1;
      while (end_trimmed >= trimmed_tag_content &&
             (*end_trimmed == ' ' || *end_trimmed == '\t' ||
              *end_trimmed == '\n' || *end_trimmed == '\r')) {
        end_trimmed--;
      }
      *(end_trimmed + 1) = '\0';
    }

    if (strncmp(trimmed_tag_content, "for ", 4) == 0) {
      char loop_var[MAX_KEY_LEN];
      char list_var[MAX_KEY_LEN];
      if (sscanf(trimmed_tag_content, "for %s in %s", loop_var, list_var) ==
          2) {
        ContextVar *list_ctx_var = find_context_var(ctx, list_var);

        const char *loop_end_tag = strstr(end_tag + 2, "{{endfor}}");
        if (loop_end_tag == NULL) {
          append_to_buffer(&rendered_buffer, &current_len, &max_len,
                           trimmed_tag_content);
          free(tag_content_raw);
          current_pos = end_tag + 2;
          continue;
        }

        if (list_ctx_var != NULL) {

          bool is_empty = false;
          if (list_ctx_var->type == CONTEXT_TYPE_STRING_2D_ARRAY) {
            is_empty =
                (list_ctx_var->value.string_2d_array_data.outer_count == 0);
          } else if (list_ctx_var->type == CONTEXT_TYPE_STRING_ARRAY) {
            is_empty = (list_ctx_var->value.string_array_data.count == 0);
          } else {

            is_empty = true;
          }

          if (is_empty) {
            current_pos = loop_end_tag + strlen("{{endfor}}");
            free(tag_content_raw);
            continue;
          }

          const char *loop_inner_start = end_tag + 2;
          size_t loop_inner_len = loop_end_tag - loop_inner_start;
          char *loop_inner_template = (char *)malloc(loop_inner_len + 1);
          if (loop_inner_template == NULL) {
            beaker_log_errno_format("ERROR",
                                    "render_template_segment: Allocation "
                                    "failed for loop_inner_template.\n");
            free(rendered_buffer);
            free(tag_content_raw);
            return NULL;
          }
          strncpy(loop_inner_template, loop_inner_start, loop_inner_len);
          loop_inner_template[loop_inner_len] = '\0';

          if (list_ctx_var->type == CONTEXT_TYPE_STRING_2D_ARRAY) {
            for (int i = 0;
                 i < list_ctx_var->value.string_2d_array_data.outer_count;
                 i++) {
              TemplateContext loop_ctx = new_context();

              for (int j = 0; j < ctx->count; j++) {
                if (ctx->vars[j].type == CONTEXT_TYPE_STRING) {
                  context_set(&loop_ctx, ctx->vars[j].key,
                              ctx->vars[j].value.string_val);
                }
              }

              context_set_string_array(
                  &loop_ctx, loop_var,
                  list_ctx_var->value.string_2d_array_data.values[i],
                  list_ctx_var->value.string_2d_array_data.inner_counts[i]);

              char *rendered_loop_item =
                  render_template_segment(loop_inner_template, &loop_ctx);
              if (rendered_loop_item) {
                append_to_buffer(&rendered_buffer, &current_len, &max_len,
                                 rendered_loop_item);
                free(rendered_loop_item);
              }
              free_context(&loop_ctx);
            }
          } else if (list_ctx_var->type == CONTEXT_TYPE_STRING_ARRAY) {
            for (int i = 0; i < list_ctx_var->value.string_array_data.count;
                 i++) {
              TemplateContext loop_ctx = new_context();

              for (int j = 0; j < ctx->count; j++) {
                if (ctx->vars[j].type == CONTEXT_TYPE_STRING) {
                  context_set(&loop_ctx, ctx->vars[j].key,
                              ctx->vars[j].value.string_val);
                }
              }

              context_set(&loop_ctx, loop_var,
                          list_ctx_var->value.string_array_data.values[i]);

              char *rendered_loop_item =
                  render_template_segment(loop_inner_template, &loop_ctx);
              if (rendered_loop_item) {
                append_to_buffer(&rendered_buffer, &current_len, &max_len,
                                 rendered_loop_item);
                free(rendered_loop_item);
              }
              free_context(&loop_ctx);
            }
          } else {
            beaker_log(
                "DEBUG",
                "render_template_segment: List variable '%s' "
                "(type %d) is not an iterable array type for 'for' loop.\n",
                list_var, list_ctx_var->type);
          }
          free(loop_inner_template);
          current_pos = loop_end_tag + strlen("{{endfor}}");
          free(tag_content_raw);
          continue;
        } else {

          beaker_log("DEBUG",
                     "render_template_segment: List variable "
                     "'%s' not found for 'for' loop. Skipping loop block.\n",
                     list_var);
          current_pos = loop_end_tag + strlen("{{endfor}}");
          free(tag_content_raw);
          continue;
        }
      } else {
        beaker_log(
            "ERROR",
            "render_template_segment: Malformed 'for' loop tag: "
            "'%s'. Expected 'for var in list'. Appending loop tag as-is.\n",
            trimmed_tag_content);
      }
    }

    else if (strcmp(trimmed_tag_content, "endfor") == 0) {
      beaker_log("WARN", "render_template_segment: '{{endfor}}' without "
                         "matching '{{for}}'. Appending endfor tag as-is.\n");
    }

    else if (strncmp(trimmed_tag_content, "if ", 3) == 0) {
      const char *condition_str = trimmed_tag_content + 3;
      Condition cond = parse_condition(condition_str);

      const char *else_tag = NULL;
      const char *elif_tag = NULL;
      const char *endif_tag = NULL;

      int if_depth = 1;
      const char *search_pos = end_tag + 2;
      while (*search_pos != '\0' && if_depth > 0) {
        const char *next_if = strstr(search_pos, "{{if ");
        const char *next_else = strstr(search_pos, "{{else}}");
        const char *next_elif = strstr(search_pos, "{{elif ");
        const char *next_endif = strstr(search_pos, "{{endif}}");

        const char *candidates[4] = {next_if, next_else, next_elif, next_endif};
        const char *earliest = NULL;
        int earliest_idx = -1;

        for (int i = 0; i < 4; i++) {
          if (candidates[i] != NULL) {
            if (earliest == NULL || candidates[i] < earliest) {
              earliest = candidates[i];
              earliest_idx = i;
            }
          }
        }

        if (earliest == NULL) {
          break;
        }

        if (earliest_idx == 0) {
          if_depth++;
          search_pos = earliest + strlen("{{if ");
          continue;
        } else if (earliest_idx == 1) {
          else_tag = earliest;
          search_pos = earliest + strlen("{{else}}");
          continue;
        } else if (earliest_idx == 2) {
          if (elif_tag == NULL) {
            elif_tag = earliest;
          }
          search_pos = earliest + strlen("{{elif ");
          const char *elif_end = strstr(search_pos, "}}");
          if (elif_end != NULL) {
            search_pos = elif_end + 2;
          }
          continue;
        } else {
          if_depth--;
          if (if_depth == 0) {
            endif_tag = earliest;
          }
          search_pos = earliest + strlen("{{endif}}");
          continue;
        }
      }

      if (endif_tag == NULL) {
        beaker_log("ERROR", "render_template_segment: Unclosed '{{if}}' tag. "
                            "Skipping if block.\n");
        current_pos = end_tag + 2;
        free(tag_content_raw);
        continue;
      }

      if (endif_tag == NULL) {
        beaker_log("ERROR", "render_template_segment: Unclosed '{{if}}' tag. "
                            "Skipping if block.\n");
        current_pos = end_tag + 2;
        free(tag_content_raw);
        continue;
      }

      bool condition_met = evaluate_condition(&cond, ctx);

      const char *block_start = end_tag + 2;
      const char *block_end = NULL;

      if (condition_met) {
        if (elif_tag != NULL && elif_tag < endif_tag) {
          block_end = elif_tag;
        } else if (else_tag != NULL) {
          block_end = else_tag;
        } else {
          block_end = endif_tag;
        }
      } else {
        bool elif_matched = false;
        if (elif_tag != NULL && elif_tag < endif_tag) {
          const char *test_elif = elif_tag;
          while (test_elif != NULL && test_elif < endif_tag) {
            const char *elif_cond_start = test_elif + strlen("{{elif ");
            const char *elif_cond_end = strstr(elif_cond_start, "}}");
            if (elif_cond_end == NULL || elif_cond_end >= endif_tag) {
              break;
            }

            size_t elif_cond_len = elif_cond_end - elif_cond_start;
            char *elif_cond_str = (char *)malloc(elif_cond_len + 1);
            if (elif_cond_str != NULL) {
              strncpy(elif_cond_str, elif_cond_start, elif_cond_len);
              elif_cond_str[elif_cond_len] = '\0';

              Condition elif_cond = parse_condition(elif_cond_str);
              if (evaluate_condition(&elif_cond, ctx)) {
                block_start = test_elif + strlen("{{elif ");
                const char *elif_end = strstr(block_start, "}}");
                if (elif_end != NULL) {
                  block_start = elif_end + 2;
                }
                block_end = else_tag ? else_tag : endif_tag;
                elif_matched = true;
                free(elif_cond_str);
                break;
              }
              free(elif_cond_str);
            }

            test_elif = strstr(test_elif + strlen("{{elif "), "{{elif ");
            if (test_elif == NULL || test_elif >= endif_tag) {
              break;
            }
          }
        }

        if (!elif_matched) {
          if (else_tag && else_tag < endif_tag) {
            block_start = else_tag + strlen("{{else}}");
            block_end = endif_tag;
          } else {
            block_end = endif_tag;
            block_start = endif_tag;
          }
        }
      }

      size_t block_len = block_end - block_start;
      if (block_len > 0) {
        char *block_template = (char *)malloc(block_len + 1);
        if (block_template != NULL) {
          strncpy(block_template, block_start, block_len);
          block_template[block_len] = '\0';

          char *rendered_block = render_template_segment(block_template, ctx);
          if (rendered_block != NULL) {
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             rendered_block);
            free(rendered_block);
          }
          free(block_template);
        }
      }

      current_pos = endif_tag + strlen("{{endif}}");
      free(tag_content_raw);
      continue;
    }

    else if (strcmp(trimmed_tag_content, "elif ") == 0 ||
             strcmp(trimmed_tag_content, "elif") == 0) {
      beaker_log("WARN", "render_template_segment: '{{elif}}' without "
                         "matching '{{if}}'. Appending elif tag as-is.\n");
    }

    else if (strcmp(trimmed_tag_content, "else") == 0) {
      beaker_log("WARN", "render_template_segment: '{{else}}' without "
                         "matching '{{if}}'. Appending else tag as-is.\n");
    }

    else if (strcmp(trimmed_tag_content, "endif") == 0) {
      beaker_log("WARN", "render_template_segment: '{{endif}}' without "
                         "matching '{{if}}'. Appending endif tag as-is.\n");
    }

    else if (strncmp(trimmed_tag_content, "include ", 8) == 0) {
      const char *filename_start = trimmed_tag_content + strlen("include ");
      if (*filename_start == '"') {
        filename_start++;
        const char *filename_end = strchr(filename_start, '"');
        if (filename_end != NULL) {
          size_t filename_len = filename_end - filename_start;
          char *included_filename = (char *)malloc(filename_len + 1);
          if (included_filename == NULL) {
            beaker_log_errno_format("ERROR",
                                    "render_template_segment: Allocation "
                                    "failed for included_filename.\n");
            free(rendered_buffer);
            free(tag_content_raw);
            return NULL;
          }
          strncpy(included_filename, filename_start, filename_len);
          included_filename[filename_len] = '\0';

          if (strstr(included_filename, "..") != NULL ||
              strchr(included_filename, '/') != NULL) {
            beaker_log("SECURITY",
                       "render_template_segment: Path traversal attempt in "
                       "include: %s\n",
                       included_filename);
            free(included_filename);
            append_to_buffer(
                &rendered_buffer, &current_len, &max_len,
                "<!-- Include blocked: path traversal not allowed -->");
            current_pos = end_tag + 2;
            free(tag_content_raw);
            continue;
          }

          char *included_html = render_template(included_filename, ctx);
          if (included_html) {
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             included_html);
            free(included_html);
          } else {
            beaker_log("WARN",
                       "render_template_segment: Failed to render "
                       "included template '%s'.\n",
                       included_filename);

            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             "<!-- Failed to include ");
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             included_filename);
            append_to_buffer(&rendered_buffer, &current_len, &max_len, " -->");
          }
          free(included_filename);
          current_pos = end_tag + 2;
          free(tag_content_raw);
          continue;
        } else {
          beaker_log("ERROR",
                     "render_template_segment: Malformed include tag "
                     "'%s'. Missing closing quote.\n",
                     trimmed_tag_content);

          append_to_buffer(&rendered_buffer, &current_len, &max_len, "{{");
          append_to_buffer(&rendered_buffer, &current_len, &max_len,
                           trimmed_tag_content);
          append_to_buffer(&rendered_buffer, &current_len, &max_len, "}}");
          free(tag_content_raw);

          current_pos = end_tag + 2;

          continue;
        }
      } else {
        beaker_log("ERROR",
                   "render_template_segment: Malformed include tag '%s'. "
                   "Expected quoted filename.\n",
                   trimmed_tag_content);

        append_to_buffer(&rendered_buffer, &current_len, &max_len, "{{");
        append_to_buffer(&rendered_buffer, &current_len, &max_len,
                         trimmed_tag_content);
        append_to_buffer(&rendered_buffer, &current_len, &max_len, "}}");
      }
    }

    else if (strncmp(trimmed_tag_content, "l(\"", 3) == 0) {
      const char *key_start = trimmed_tag_content + 3;
      const char *key_end = strstr(key_start, "\")");
      if (key_end == NULL) {
        beaker_log("ERROR",
                   "render_template_segment: Malformed l() tag '%s'. "
                   "Expected l(\"key\"). Appending as-is.\n",
                   trimmed_tag_content);
        append_to_buffer(&rendered_buffer, &current_len, &max_len, "{{");
        append_to_buffer(&rendered_buffer, &current_len, &max_len,
                         trimmed_tag_content);
        append_to_buffer(&rendered_buffer, &current_len, &max_len, "}}");
        free(tag_content_raw);
        current_pos = end_tag + 2;
        continue;
      }

      size_t key_len = key_end - key_start;
      char l10n_key[MAX_KEY_LEN];
      if (key_len >= MAX_KEY_LEN)
        key_len = MAX_KEY_LEN - 1;
      strncpy(l10n_key, key_start, key_len);
      l10n_key[key_len] = '\0';

      bool is_safe = false;
      const char *after_close = key_end + 2;
      if (strncmp(after_close, "|safe", 5) == 0) {
        is_safe = true;
      }

      const char *value_to_append = NULL;

      ContextVar *locale_var = find_context_var(ctx, "__locale");
      if (locale_var != NULL && locale_var->type == CONTEXT_TYPE_STRING) {
        const char *current_locale_id = locale_var->value.string_val;
        for (int i = 0; i < locale_count; i++) {
          if (strcmp(locales[i].meta.id, current_locale_id) == 0) {
            for (int j = 0; j < locales[i].key_count; j++) {
              if (strcmp(locales[i].keys[j].key, l10n_key) == 0) {
                value_to_append = locales[i].keys[j].value;
                break;
              }
            }
            break;
          }
        }
      }

      if (value_to_append != NULL) {
        if (is_safe) {
          append_to_buffer(&rendered_buffer, &current_len, &max_len,
                           value_to_append);
        } else {
          char *escaped = html_escape(value_to_append);
          if (escaped) {
            append_to_buffer(&rendered_buffer, &current_len, &max_len, escaped);
            free(escaped);
          } else {
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             value_to_append);
          }
        }
      } else {
        beaker_log("WARN",
                   "render_template_segment: l10n key '%s' not found "
                   "for current locale. Appending key as-is.\n",
                   l10n_key);
        append_to_buffer(&rendered_buffer, &current_len, &max_len, l10n_key);
      }

      free(tag_content_raw);
      current_pos = end_tag + 2;
      continue;
    }

    else {
      bool is_safe = false;
      char *processing_tag_content = strdup(trimmed_tag_content);
      if (processing_tag_content == NULL) {
        beaker_log_errno("Failed to duplicate tag content for processing");
        free(rendered_buffer);
        free(tag_content_raw);
        return NULL;
      }

      char *safe_flag_pos_in_processing =
          strstr(processing_tag_content, "|safe");
      if (safe_flag_pos_in_processing != NULL) {
        is_safe = true;
        *safe_flag_pos_in_processing = '\0';
      }

      int index_val = -1;
      char *key_name = parse_indexed_tag(processing_tag_content, &index_val);
      free(processing_tag_content);

      if (key_name == NULL) {
        beaker_log("ERROR",
                   "render_template_segment: parse_indexed_tag failed for "
                   "'%s'.\n",
                   trimmed_tag_content);
        free(rendered_buffer);
        free(tag_content_raw);
        return NULL;
      }

      const char *value_to_append = NULL;
      ContextVar *var = find_context_var(ctx, key_name);

      if (var != NULL) {
        if (index_val == -1) {
          if (var->type == CONTEXT_TYPE_STRING) {
            value_to_append = var->value.string_val;
          } else {
            beaker_log(
                "WARN",
                "render_template_segment: Variable '%s' is not a "
                "simple string (type %d). Not rendering as simple string.\n",
                key_name, var->type);

            append_to_buffer(&rendered_buffer, &current_len, &max_len, "{{");
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             trimmed_tag_content);
            append_to_buffer(&rendered_buffer, &current_len, &max_len, "}}");
          }
        } else {
          if (var->type == CONTEXT_TYPE_STRING_ARRAY) {
            if (index_val >= 0 &&
                index_val < var->value.string_array_data.count) {
              value_to_append = var->value.string_array_data.values[index_val];
            } else {
              beaker_log("ERROR",
                         "render_template_segment: Index %d out of bounds "
                         "for '%s' (count %d). Appending tag as-is.\n",
                         index_val, key_name,
                         var->value.string_array_data.count);
            }
          } else {
            beaker_log(
                "ERROR",
                "render_template_segment: Variable '%s' (type %d) is "
                "not a string array for indexed access. Appending tag as-is.\n",
                key_name, var->type);
          }
        }
      } else {
        beaker_log("WARN",
                   "render_template_segment: Key '%s' not found in "
                   "context. Appending tag as-is.\n",
                   key_name);
      }

      if (value_to_append != NULL) {
        if (is_safe) {
          append_to_buffer(&rendered_buffer, &current_len, &max_len,
                           value_to_append);
        } else {
          char *escaped_value = html_escape(value_to_append);
          if (escaped_value) {
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             escaped_value);
            free(escaped_value);
          } else {
            beaker_log("ERROR",
                       "render_template_segment: Failed to HTML escape "
                       "value for key '%s'. Appending raw.\n",
                       key_name);
            append_to_buffer(&rendered_buffer, &current_len, &max_len,
                             value_to_append);
          }
        }
      }

      free(key_name);

      free(tag_content_raw);

      current_pos = end_tag + 2;

      continue;
    }

    free(tag_content_raw);
    current_pos = end_tag + 2;
  }

  append_to_buffer(&rendered_buffer, &current_len, &max_len, current_pos);
  return rendered_buffer;
}

char *render_template(const char *template_file, TemplateContext *ctx) {
  char full_path[MAX_PATH_LEN];

  if (strstr(template_file, "..") != NULL) {
    beaker_log("SECURITY", "render_template: Path traversal attempt: %s\n",
               template_file);
    return NULL;
  }

  snprintf(full_path, sizeof(full_path), "%s%s", TEMPLATES_DIR, template_file);

  FILE *fp = fopen(full_path, "r");
  if (fp == NULL) {
    beaker_log_errno_format(
        "ERROR", "render_template: Could not open template '%s'. %s\n",
        full_path, strerror(errno));
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  if (file_size < 0) {
    beaker_log_errno_format(
        "ERROR", "render_template: Failed to get size of '%s'.\n", full_path);
    fclose(fp);
    return NULL;
  }
  fseek(fp, 0, SEEK_SET);

  if (file_size == 0) {
    char *empty_result = render_template_segment("", ctx);
    fclose(fp);
    return empty_result;
  }

  char *template_content = (char *)malloc((size_t)file_size + 1);
  if (template_content == NULL) {
    beaker_log_errno_format("ERROR",
                            "render_template: Failed to allocate %ld bytes for "
                            "template content.\n",
                            file_size + 1);
    fclose(fp);
    return NULL;
  }

  size_t bytes_read = fread(template_content, 1, (size_t)file_size, fp);
  if (bytes_read != (size_t)file_size) {
    beaker_log_errno_format(
        "ERROR", "render_template: Failed to read complete template '%s'.\n",
        full_path);
    free(template_content);
    fclose(fp);
    return NULL;
  }
  template_content[file_size] = '\0';
  fclose(fp);

  char *rendered_html = render_template_segment(template_content, ctx);
  free(template_content);
  return rendered_html;
}
