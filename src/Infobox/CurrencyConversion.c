#include "CurrencyConversion.h"
#include "../Cache/Cache.h"
#include "../Utility/HtmlEscape.h"
#include "../Utility/HttpClient.h"
#include "../Utility/JsonHelper.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CURRENCY_NORM_MAX 512

static void trim_in(const char *in, char *out, size_t cap) {
  if (!in || cap == 0) {
    if (cap > 0)
      out[0] = '\0';
    return;
  }
  while (*in && isspace((unsigned char)*in))
    in++;
  size_t len = strlen(in);
  while (len > 0 && isspace((unsigned char)in[len - 1]))
    len--;
  if (len >= cap)
    len = cap - 1;
  memcpy(out, in, len);
  out[len] = '\0';
}

static void trim_inplace_mut(char *s) {
  char *start = s;
  while (*start && isspace((unsigned char)*start))
    start++;
  if (start != s)
    memmove(s, start, strlen(start) + 1);
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1]))
    len--;
  s[len] = '\0';
}

static void strip_currency_prefixes(char *s) {
  int again;
  do {
    again = 0;
    if (strncasecmp(s, "currency conversion ", 20) == 0) {
      memmove(s, s + 20, strlen(s + 20) + 1);
      again = 1;
    } else if (strncasecmp(s, "convert ", 8) == 0) {
      memmove(s, s + 8, strlen(s + 8) + 1);
      again = 1;
    } else if (strncasecmp(s, "currency ", 9) == 0) {
      memmove(s, s + 9, strlen(s + 9) + 1);
      again = 1;
    } else if (strncasecmp(s, "exchange ", 9) == 0) {
      memmove(s, s + 9, strlen(s + 9) + 1);
      again = 1;
    }
  } while (again);
}

static void normalize_currency_query(const char *query, char *out, size_t cap) {
  trim_in(query, out, cap);
  strip_currency_prefixes(out);
  trim_inplace_mut(out);
}

typedef struct {
  const char *code;
  const char *name;
  const char *symbol;
  int is_crypto;
} CurrencyDef;

static const CurrencyDef CURRENCIES[] = {
    {"USD", "US Dollar", "$", 0},
    {"EUR", "Euro", "€", 0},
    {"GBP", "British Pound", "£", 0},
    {"JPY", "Japanese Yen", "¥", 0},
    {"AUD", "Australian Dollar", "A$", 0},
    {"CAD", "Canadian Dollar", "C$", 0},
    {"CHF", "Swiss Franc", "CHF", 0},
    {"CNY", "Chinese Yuan", "¥", 0},
    {"INR", "Indian Rupee", "₹", 0},
    {"KRW", "South Korean Won", "₩", 0},
    {"BRL", "Brazilian Real", "R$", 0},
    {"RUB", "Russian Ruble", "₽", 0},
    {"ZAR", "South African Rand", "R", 0},
    {"MXN", "Mexican Peso", "MX$", 0},
    {"SGD", "Singapore Dollar", "S$", 0},
    {"HKD", "Hong Kong Dollar", "HK$", 0},
    {"NOK", "Norwegian Krone", "kr", 0},
    {"SEK", "Swedish Krona", "kr", 0},
    {"DKK", "Danish Krone", "kr", 0},
    {"NZD", "New Zealand Dollar", "NZ$", 0},
    {"TRY", "Turkish Lira", "₺", 0},
    {"PLN", "Polish Zloty", "zł", 0},
    {"THB", "Thai Baht", "฿", 0},
    {"IDR", "Indonesian Rupiah", "Rp", 0},
    {"HUF", "Hungarian Forint", "Ft", 0},
    {"CZK", "Czech Koruna", "Kč", 0},
    {"ILS", "Israeli Shekel", "₪", 0},
    {"CLP", "Chilean Peso", "CLP$", 0},
    {"PHP", "Philippine Peso", "₱", 0},
    {"AED", "UAE Dirham", "د.إ", 0},
    {"COP", "Colombian Peso", "COP$", 0},
    {"SAR", "Saudi Riyal", "﷼", 0},
    {"MYR", "Malaysian Ringgit", "RM", 0},
    {"RON", "Romanian Leu", "lei", 0},
    {"ARS", "Argentine Peso", "ARS$", 0},
    {"PKR", "Pakistani Rupee", "₨", 0},
    {"EGP", "Egyptian Pound", "£", 0},
    {"VND", "Vietnamese Dong", "₫", 0},
    {"NGN", "Nigerian Naira", "₦", 0},
    {"BDT", "Bangladeshi Taka", "৳", 0},
    {"UAH", "Ukrainian Hryvnia", "₴", 0},
    {"PEN", "Peruvian Sol", "S/", 0},
    {"BGN", "Bulgarian Lev", "лв", 0},
    {"HRK", "Croatian Kuna", "kn", 0},
    {"ISK", "Icelandic Krona", "kr", 0},
    {"MAD", "Moroccan Dirham", "د.م.", 0},
    {"KES", "Kenyan Shilling", "KSh", 0},
    {"QAR", "Qatari Riyal", "﷼", 0},
    {"KWD", "Kuwaiti Dinar", "د.ك", 0},
    {"BHD", "Bahraini Dinar", ".د.ب", 0},
    {"OMR", "Omani Rial", "﷼", 0},
    {"JOD", "Jordanian Dinar", "د.ا", 0},
    {"TWD", "Taiwan Dollar", "NT$", 0},

    {"BTC", "Bitcoin", "₿", 1},
    {"ETH", "Ethereum", "Ξ", 1},
    {"USDT", "Tether", "₮", 1},
    {"BNB", "Binance Coin", "BNB", 1},
    {"XRP", "Ripple", "XRP", 1},
    {"USDC", "USD Coin", "$", 1},
    {"ADA", "Cardano", "ADA", 1},
    {"DOGE", "Dogecoin", "Ð", 1},
    {"SOL", "Solana", "SOL", 1},
    {"TRX", "Tron", "TRX", 1},
    {"DOT", "Polkadot", "DOT", 1},
    {"MATIC", "Polygon", "MATIC", 1},
    {"LTC", "Litecoin", "Ł", 1},
    {"SHIB", "Shiba Inu", "SHIB", 1},
    {"AVAX", "Avalanche", "AVAX", 1},
    {"LINK", "Chainlink", "LINK", 1},
    {"ATOM", "Cosmos", "ATOM", 1},
    {"XMR", "Monero", "XMR", 1},
    {"ETC", "Ethereum Classic", "ETC", 1},
    {"XLM", "Stellar", "XLM", 1},
    {"BCH", "Bitcoin Cash", "BCH", 1},
    {"ALGO", "Algorand", "ALGO", 1},
    {"VET", "VeChain", "VET", 1},
    {"FIL", "Filecoin", "FIL", 1},
    {"NEAR", "NEAR Protocol", "NEAR", 1},
    {"APT", "Aptos", "APT", 1},
    {"ARB", "Arbitrum", "ARB", 1},
    {"OP", "Optimism", "OP", 1},
    {"SAND", "The Sandbox", "SAND", 1},
    {"MANA", "Decentraland", "MANA", 1},
    {"AXS", "Axie Infinity", "AXS", 1},
    {"AAVE", "Aave", "AAVE", 1},
    {"MKR", "Maker", "MKR", 1},
    {"GRT", "The Graph", "GRT", 1},
    {"FTM", "Fantom", "FTM", 1},
    {"CRO", "Cronos", "CRO", 1},
    {"NEAR", "NEAR Protocol", "NEAR", 1},
    {"INJ", "Injective", "INJ", 1},
    {"RUNE", "THORChain", "RUNE", 1},
    {"LDO", "Lido DAO", "LDO", 1},
    {"QNT", "Quant", "QNT", 1},
    {"RNDR", "Render", "RNDR", 1},
    {"STX", "Stacks", "STX", 1},
    {"IMX", "Immutable X", "IMX", 1},
    {"SNX", "Synthetix", "SNX", 1},
    {"THETA", "Theta", "THETA", 1},
    {"XTZ", "Tezos", "XTZ", 1},
    {"EOS", "EOS", "EOS", 1},
    {"FLOW", "Flow", "FLOW", 1},
    {"CHZ", "Chiliz", "CHZ", 1},
    {"CRV", "Curve DAO", "CRV", 1},
    {"APE", "ApeCoin", "APE", 1},
    {"PEPE", "Pepe", "PEPE", 1},
    {"WIF", "dogwifhat", "WIF", 1},
    {"SUI", "Sui", "SUI", 1},
    {"SEI", "Sei", "SEI", 1},
    {"TIA", "Celestia", "TIA", 1},
    {"PYTH", "Pyth Network", "PYTH", 1},
    {"BONK", "Bonk", "BONK", 1},
    {"FET", "Fetch.ai", "FET", 1},
    {"AGIX", "SingularityNET", "AGIX", 1},
    {"RNDR", "Render Token", "RNDR", 1},
};

static const int CURRENCY_COUNT = sizeof(CURRENCIES) / sizeof(CURRENCIES[0]);

static int is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const CurrencyDef *find_currency(const char *str) {
  if (!str || !*str)
    return NULL;

  size_t len = strlen(str);
  if (len < 2 || len > 10)
    return NULL;

  char normalized[16] = {0};
  size_t j = 0;
  for (size_t i = 0; i < len && j < 15; i++) {
    normalized[j++] = toupper((unsigned char)str[i]);
  }
  normalized[j] = '\0';

  for (int i = 0; i < CURRENCY_COUNT; i++) {
    if (strcmp(normalized, CURRENCIES[i].code) == 0) {
      return &CURRENCIES[i];
    }
  }
  return NULL;
}

int is_currency_query(const char *query) {
  if (!query)
    return 0;

  char buf[CURRENCY_NORM_MAX];
  normalize_currency_query(query, buf, sizeof buf);

  const char *patterns[] = {" to ", " in ", " into ", " = ", " equals ", NULL};

  int has_pattern = 0;
  for (int i = 0; patterns[i]; i++) {
    if (strstr(buf, patterns[i])) {
      has_pattern = 1;
      break;
    }
  }

  if (!has_pattern) {
    const char *last_space = strrchr(buf, ' ');
    if (last_space) {
      const CurrencyDef *c = find_currency(last_space + 1);
      if (c) {
        const char *before = buf;
        while (*before && is_whitespace(*before))
          before++;
        const char *num_end = before;
        while (*num_end &&
               (isdigit(*num_end) || *num_end == '.' || *num_end == ',' ||
                *num_end == '-' || *num_end == '+')) {
          num_end++;
        }
        if (num_end > before)
          has_pattern = 1;
      }
    }
  }

  return has_pattern;
}

static double parse_value(const char **ptr) {
  const char *p = *ptr;
  while (*p && is_whitespace(*p))
    p++;

  double value = 0.0;
  int has_decimal = 0;
  double decimal_place = 1.0;

  if (*p == '-' || *p == '+')
    p++;

  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (*p - '0');
    if (has_decimal)
      decimal_place *= 10;
    p++;
  }

  if (*p == '.' || *p == ',') {
    has_decimal = 1;
    p++;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      decimal_place *= 10;
      p++;
    }
  }

  if (has_decimal)
    value /= decimal_place;

  *ptr = p;
  return value;
}

static int parse_currency_query(const char *query, double *value,
                                const CurrencyDef **from_curr,
                                const CurrencyDef **to_curr) {
  char buf[CURRENCY_NORM_MAX];
  normalize_currency_query(query, buf, sizeof buf);

  *value = 0;
  *from_curr = NULL;
  *to_curr = NULL;

  const char *value_end = buf;
  *value = parse_value(&value_end);

  if (value_end == buf)
    return 0;

  const char *p = value_end;
  while (*p && is_whitespace(*p))
    p++;

  size_t remaining = strlen(p);
  if (remaining < 2)
    return 0;

  const char *to_keywords[] = {" to ", " in ", " into ", " -> ",
                               " → ",  " = ",  NULL};
  const char *to_pos = NULL;
  size_t keyword_len = 0;
  for (int i = 0; to_keywords[i]; i++) {
    const char *found = strstr(p, to_keywords[i]);
    if (found) {
      to_pos = found + strlen(to_keywords[i]);
      keyword_len = strlen(to_keywords[i]);
      break;
    }
  }

  if (!to_pos) {
    const char *last_space = strrchr(p, ' ');
    if (last_space && last_space > p) {
      char from_part[32] = {0};
      size_t len = last_space - p;
      if (len < 31) {
        strncpy(from_part, p, len);
        *from_curr = find_currency(from_part);
        if (*from_curr) {
          *to_curr = find_currency(last_space + 1);
          return *to_curr ? 1 : 0;
        }
      }
    }
    return 0;
  }

  char from_part[32] = {0};
  size_t from_len = to_pos - p - keyword_len;
  if (from_len > 31)
    from_len = 31;
  strncpy(from_part, p, from_len);

  char *end_from = from_part + from_len;
  while (end_from > from_part && is_whitespace(end_from[-1]))
    end_from--;
  *end_from = '\0';

  *from_curr = find_currency(from_part);
  if (!*from_curr) {
    return 0;
  }

  while (*to_pos && is_whitespace(*to_pos))
    to_pos++;

  if (!*to_pos)
    return 0;

  char to_part[32] = {0};
  size_t to_len = 0;
  const char *tp = to_pos;
  while (*tp && !is_whitespace(*tp) && to_len < 31) {
    to_part[to_len++] = *tp++;
  }
  to_part[to_len] = '\0';

  *to_curr = find_currency(to_part);
  if (!*to_curr) {
    char try_buf[32] = {0};
    strncpy(try_buf, to_pos, 31);
    *to_curr = find_currency(try_buf);
  }

  return *to_curr ? 1 : 0;
}

static double get_rate_from_json(const char *json, const char *target_code) {
  JsonFloatMap map;
  if (json_parse_float_map(json, "rates", &map)) {
    for (size_t i = 0; i < map.count; i++) {
      if (strcmp(map.keys[i], target_code) == 0) {
        return map.values[i];
      }
    }
  }
  return 0;
}

static void format_number(double val, char *buf, size_t bufsize) {
  if (bufsize == 0)
    return;
  if (val == 0) {
    snprintf(buf, bufsize, "0");
    return;
  }
  snprintf(buf, bufsize, "%.2f", val);
}

static char *build_html(double value, const CurrencyDef *from,
                        const CurrencyDef *to, double result) {
  char val_buf[64], res_buf[64];

  format_number(value, val_buf, sizeof(val_buf));
  format_number(result, res_buf, sizeof(res_buf));

  char ev[96], ef[32], et[32];
  html_escape_attr(val_buf, ev, sizeof(ev));
  html_escape_attr(from->code, ef, sizeof(ef));
  html_escape_attr(to->code, et, sizeof(et));

  double rate = value != 0 ? result / value : 0.0;

  char fb_raw[512];
  snprintf(fb_raw, sizeof(fb_raw), "%s %s = %s %s. 1 %s = %.4f %s", val_buf,
           from->code, res_buf, to->code, from->code, rate, to->code);
  char fb_esc[640];
  html_escape_text(fb_raw, fb_esc, sizeof(fb_esc));

  char *html = malloc(2048);
  if (!html)
    return NULL;
  snprintf(html, 2048,
           "<div class=\"widget widget-currency\" data-widget=\"currency\" "
           "data-value=\"%s\" data-from=\"%s\" data-to=\"%s\" data-rate=\"%.12g\">"
           "<div class=\"widget-currency-root\"></div>"
           "<noscript><p class=\"widget-fallback\">%s</p></noscript></div>",
           ev, ef, et, rate, fb_esc);
  return html;
}

InfoBox fetch_currency_data(const char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  double value = 0;
  const CurrencyDef *from_curr = NULL;
  const CurrencyDef *to_curr = NULL;

  if (!parse_currency_query(query, &value, &from_curr, &to_curr))
    return info;
  if (!from_curr || !to_curr)
    return info;
  if (strcmp(from_curr->code, to_curr->code) == 0)
    return info;

  char cache_key[128];
  snprintf(cache_key, sizeof(cache_key), "currency_%s_%s", from_curr->code,
           to_curr->code);

  char *cached_data = NULL;
  size_t cached_size = 0;
  double rate = 0;
  int use_cache = 0;

  int is_crypto = from_curr->is_crypto || to_curr->is_crypto;

  if (get_cache_ttl_infobox() > 0) {
    if (cache_get(cache_key, get_cache_ttl_infobox(), &cached_data,
                  &cached_size) == 0 &&
        cached_data && cached_size > 0) {
      if (is_crypto) {
        rate = json_get_float(cached_data, to_curr->code);
      } else {
        rate = get_rate_from_json(cached_data, to_curr->code);
      }
      if (rate > 0) {
        use_cache = 1;
      }
      free(cached_data);
    }
  }

  if (!use_cache) {
    char url[512];

    if (is_crypto) {
      snprintf(url, sizeof(url),
               "https://min-api.cryptocompare.com/data/price?fsym=%s&tsyms=%s",
               from_curr->code, to_curr->code);
    } else {
      snprintf(url, sizeof(url),
               "https://api.exchangerate-api.com/v4/latest/%s",
               from_curr->code);
    }

    HttpResponse resp = http_get(url, SEEK_HTTP_UA);
    if (resp.memory && resp.size > 0) {
      if (is_crypto) {
        rate = json_get_float(resp.memory, to_curr->code);
      } else {
        rate = get_rate_from_json(resp.memory, to_curr->code);
      }
      if (rate > 0 && get_cache_ttl_infobox() > 0) {
        cache_set(cache_key, resp.memory, resp.size);
      }
    }
    http_response_free(&resp);
  }

  if (rate <= 0)
    return info;

  double result = value * rate;

  info.title = strdup("Currency Conversion");
  info.extract = build_html(value, from_curr, to_curr, result);
  if (!info.extract) {
    return info;
  }
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");

  return info;
}
