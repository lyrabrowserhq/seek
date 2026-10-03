#include "Display.h"
#include "Config.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *pretty_display_url(const char *input) {
  if (!input)
    return NULL;

  const char *start = input;

  const char *protocol_pos = strstr(input, "://");
  if (protocol_pos) {
    start = protocol_pos + 3;
  }

  if (strncasecmp(start, "www.", 4) == 0) {
    start += 4;
  }

  size_t input_len = strlen(start);
  char temp[BUFFER_SIZE_MEDIUM];
  strncpy(temp, start, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = '\0';

  char *query = strchr(temp, '?');
  if (query) {
    *query = '\0';
    input_len = strlen(temp);
  }

  if (input_len > 0 && temp[input_len - 1] == '/') {
    temp[input_len - 1] = '\0';
  }

  char *output = (char *)malloc(strlen(temp) * 3 + 1);
  if (!output)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; temp[i] != '\0'; i++) {
    if (temp[i] == '/') {
      output[j++] = ' ';
      output[j++] = '>';
      output[j++] = ' ';
    } else {
      output[j++] = (char)tolower((unsigned char)temp[i]);
    }
  }
  output[j] = '\0';

  return output;
}
