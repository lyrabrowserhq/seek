CC      := cc
UNAME_S := $(shell uname -s)
PKG_CONFIG ?= pkg-config
PKG_DEPS   := libxml-2.0 libcurl openssl zlib
BEAKER_LIB := beaker/build/libbeaker.a

ifeq ($(UNAME_S),Darwin)
DEP_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKG_DEPS) 2>/dev/null)
DEP_LIBS   := $(shell $(PKG_CONFIG) --libs $(PKG_DEPS) 2>/dev/null)
HARDEN_CFLAGS := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
CFLAGS  := -Wall -Wextra -O2 -Isrc -Ibeaker $(DEP_CFLAGS) $(HARDEN_CFLAGS)
LIBS    := $(BEAKER_LIB) $(DEP_LIBS) -lpthread -lm
LDFLAGS :=
else
HARDEN_CFLAGS := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
HARDEN_LDFLAGS := -Wl,-z,relro,-z,now -pie
CFLAGS  := -Wall -Wextra -O2 -Isrc -Ibeaker -I/usr/include/libxml2 -D_GNU_SOURCE $(HARDEN_CFLAGS)
LDFLAGS := $(HARDEN_LDFLAGS)
LIBS    := $(BEAKER_LIB) -lcurl -lxml2 -lpthread -lm -lssl -lcrypto -lz
endif

SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := obj

SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET := $(BIN_DIR)/seeker

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

all: $(TARGET)

.DEFAULT_GOAL := all

$(TARGET): $(OBJS) $(BEAKER_LIB)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "Build complete: $(TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled: $<"

run: $(TARGET)
	./$(TARGET)

TEST_ENGINE_OBJS = $(OBJ_DIR)/Config.o $(OBJ_DIR)/Cache/Cache.o \
	$(OBJ_DIR)/Proxy/Proxy.o $(OBJ_DIR)/Scraping/Scraping.o \
	$(OBJ_DIR)/Scraping/ScrapingHttp.o $(OBJ_DIR)/Scraping/ScrapingParsers.o \
	$(OBJ_DIR)/Scraping/JsonEngines.o \
	$(OBJ_DIR)/Utility/XmlHelper.o $(OBJ_DIR)/Utility/Unescape.o \
	$(OBJ_DIR)/Utility/Utility.o
test-engine: tools/test_engine.c $(TEST_ENGINE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o bin/test_engine tools/test_engine.c \
		$(TEST_ENGINE_OBJS) $(LDFLAGS) \
		-lcurl -lxml2 -lpthread -lm -lssl -lcrypto
	@echo "Run: ./bin/test_engine [engine_index] [query]"
	@echo "  engine_index: see ENGINE_REGISTRY in ScrapingParsers.c"

TEST_DIR         := tests
BEAKER_SRCS      := $(wildcard beaker/src/*.c)
FUZZ_CC          ?= clang
FUZZ_RUNS        ?= 1000
TEST_CFLAGS      := -Wall -Wextra -O2 -Itests
BEAKER_TEST_CFLAGS := -Wall -Wextra -O2 -Ibeaker -Itests
TEST_OBJ_UNESCAPE := $(OBJ_DIR)/Utility/Unescape.o $(OBJ_DIR)/Utility/Utility.o
TEST_OBJ_JSON     := $(OBJ_DIR)/Utility/JsonHelper.o
TEST_OBJ_HTTP     := $(OBJ_DIR)/Utility/HttpClient.o $(OBJ_DIR)/Cache/Cache.o \
	$(OBJ_DIR)/Proxy/Proxy.o $(OBJ_DIR)/Config.o
TEST_OBJ_HTML     := $(OBJ_DIR)/Utility/HtmlEscape.o

ifeq ($(UNAME_S),Darwin)
TEST_REST_LINK  := $(DEP_LIBS) -lpthread -lm
TEST_CACHE_LINK := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
TEST_XML_LINK   := $(shell $(PKG_CONFIG) --libs libxml-2.0 2>/dev/null)
else
TEST_REST_LINK  := -lcurl -lxml2 -lpthread -lm -lssl -lcrypto
TEST_CACHE_LINK := -lssl -lcrypto -lm
TEST_XML_LINK   := -lxml2
endif

$(BEAKER_LIB): $(BEAKER_SRCS) beaker/Makefile
	$(MAKE) -C beaker all

ASAN_CFLAGS   := -fsanitize=address,undefined -g -O1
ASAN_OBJ_DIR  := obj-asan
ASAN_OBJS     := $(SRCS:$(SRC_DIR)/%.c=$(ASAN_OBJ_DIR)/%.o)
ASAN_BEAKER_LIB := beaker/build-asan/libbeaker.a
ASAN_TARGET   := $(BIN_DIR)/seeker-asan

asan: $(ASAN_TARGET)

$(ASAN_BEAKER_LIB): $(BEAKER_SRCS) beaker/Makefile
	$(MAKE) -C beaker BUILD_DIR=build-asan \
		"CFLAGS=-Wall -fPIC -I. -Isrc $(ASAN_CFLAGS)" all

$(ASAN_TARGET): $(ASAN_OBJS) $(ASAN_BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(ASAN_CFLAGS) $(ASAN_OBJS) -o $@ $(LDFLAGS) \
		$(LIBS:$(BEAKER_LIB)=$(ASAN_BEAKER_LIB))
	@echo "Build complete: $(ASAN_TARGET)"

$(ASAN_OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ASAN_CFLAGS) -c $< -o $@
	@echo "Compiled (asan): $<"

$(BIN_DIR)/test_unescape: $(TEST_DIR)/test_unescape.c $(TEST_OBJ_UNESCAPE) $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_OBJ_UNESCAPE) $(BEAKER_LIB) -o $@ -lm -lpthread -lz

$(BIN_DIR)/test_json: $(TEST_DIR)/test_json.c $(TEST_OBJ_JSON) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_OBJ_JSON) -o $@ -lm

$(BIN_DIR)/test_parse_url: $(TEST_DIR)/test_parse_url.c $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(BEAKER_TEST_CFLAGS) $< $(BEAKER_LIB) -o $@ -lm -lpthread -lz

$(BIN_DIR)/test_property: $(TEST_DIR)/test_property.c $(TEST_OBJ_UNESCAPE) $(TEST_OBJ_JSON) $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) $< $(TEST_OBJ_UNESCAPE) $(TEST_OBJ_JSON) $(BEAKER_LIB) -o $@ -lm -lpthread -lz

$(BIN_DIR)/fuzz_unescape: $(TEST_DIR)/fuzz_unescape.c $(TEST_OBJ_UNESCAPE) $(BEAKER_LIB) | $(BIN_DIR)
	$(FUZZ_CC) -fsanitize=fuzzer,address -g -O1 $(TEST_DIR)/fuzz_unescape.c $(TEST_OBJ_UNESCAPE) $(BEAKER_LIB) -o $@ -lm -lpthread

$(BIN_DIR)/fuzz_parse_url: $(TEST_DIR)/fuzz_parse_url.c $(BEAKER_LIB) | $(BIN_DIR)
	$(FUZZ_CC) -fsanitize=fuzzer,address -g -O1 $(BEAKER_TEST_CFLAGS) $(TEST_DIR)/fuzz_parse_url.c $(BEAKER_LIB) -o $@ -lm -lpthread

$(BIN_DIR)/fuzz_json: $(TEST_DIR)/fuzz_json.c $(TEST_OBJ_JSON) | $(BIN_DIR)
	$(FUZZ_CC) -fsanitize=fuzzer,address -g -O1 $(TEST_DIR)/fuzz_json.c $(TEST_OBJ_JSON) -o $@ -lm

$(BIN_DIR)/fuzz_security_headers: $(TEST_DIR)/fuzz_security_headers.c $(BEAKER_LIB) | $(BIN_DIR)
	$(FUZZ_CC) -fsanitize=fuzzer,address -g -O1 $(BEAKER_TEST_CFLAGS) -Ibeaker/src \
		$(TEST_DIR)/fuzz_security_headers.c $(BEAKER_LIB) -o $@ -lm -lpthread

$(BIN_DIR)/fuzz_html_escape: $(TEST_DIR)/fuzz_html_escape.c $(TEST_OBJ_HTML) | $(BIN_DIR)
	$(FUZZ_CC) -fsanitize=fuzzer,address -g -O1 -Itests -Isrc \
		$(TEST_DIR)/fuzz_html_escape.c $(TEST_OBJ_HTML) -o $@

$(BIN_DIR)/test_html_escape: $(TEST_DIR)/test_html_escape.c $(TEST_OBJ_HTML) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) -Isrc $< $(TEST_OBJ_HTML) -o $@

$(BIN_DIR)/test_utility: $(TEST_DIR)/test_utility.c $(OBJ_DIR)/Utility/Utility.o $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) -Isrc $< $(OBJ_DIR)/Utility/Utility.o $(BEAKER_LIB) -o $@ -lm -lpthread -lz

$(BIN_DIR)/test_slop_detect: $(TEST_DIR)/test_slop_detect.c $(OBJ_DIR)/Utility/SlopDetect.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Utility/SlopDetect.o -o $@ $(LDFLAGS) -lm

$(BIN_DIR)/test_display: $(TEST_DIR)/test_display.c $(OBJ_DIR)/Utility/Display.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Utility/Display.o -o $@ $(LDFLAGS)

$(BIN_DIR)/test_bangs: $(TEST_DIR)/test_bangs.c $(OBJ_DIR)/Bangs.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Bangs.o -o $@ $(LDFLAGS) $(TEST_REST_LINK)

$(BIN_DIR)/test_security_headers: $(TEST_DIR)/test_security_headers.c $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(BEAKER_TEST_CFLAGS) -Ibeaker/src $< $(BEAKER_LIB) -o $@ -lm -lpthread -lz

$(BIN_DIR)/test_config: $(TEST_DIR)/test_config.c $(OBJ_DIR)/Config.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Config.o -o $@ $(LDFLAGS)

$(BIN_DIR)/test_cache: $(TEST_DIR)/test_cache.c $(OBJ_DIR)/Cache/Cache.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Cache/Cache.o -o $@ $(LDFLAGS) $(TEST_CACHE_LINK)

$(BIN_DIR)/test_ratelimit: $(TEST_DIR)/test_ratelimit.c $(OBJ_DIR)/Limiter/RateLimit.o $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests -Ibeaker $< $(OBJ_DIR)/Limiter/RateLimit.o $(BEAKER_LIB) -o $@ $(LDFLAGS) -lpthread -lz -lcrypto

$(BIN_DIR)/test_xmlhelper: $(TEST_DIR)/test_xmlhelper.c $(OBJ_DIR)/Utility/XmlHelper.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Utility/XmlHelper.o -o $@ $(LDFLAGS) $(TEST_XML_LINK)

$(BIN_DIR)/test_httpclient: $(TEST_DIR)/test_httpclient.c $(TEST_OBJ_HTTP) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(TEST_OBJ_HTTP) -o $@ $(LDFLAGS) $(TEST_REST_LINK)

$(BIN_DIR)/test_proxy: $(TEST_DIR)/test_proxy.c $(OBJ_DIR)/Proxy/Proxy.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Proxy/Proxy.o -o $@ $(LDFLAGS) $(TEST_REST_LINK)

$(BIN_DIR)/test_rank: $(TEST_DIR)/test_rank.c $(OBJ_DIR)/Utility/Rank.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Utility/Rank.o -o $@ $(LDFLAGS)

$(BIN_DIR)/test_json_engines: $(TEST_DIR)/test_json_engines.c \
	$(OBJ_DIR)/Scraping/JsonEngines.o $(OBJ_DIR)/Scraping/ScrapingParsers.o \
	$(OBJ_DIR)/Utility/XmlHelper.o $(OBJ_DIR)/Utility/Unescape.o \
	$(OBJ_DIR)/Utility/Utility.o $(OBJ_DIR)/Config.o $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Scraping/JsonEngines.o \
		$(OBJ_DIR)/Scraping/ScrapingParsers.o $(OBJ_DIR)/Utility/XmlHelper.o \
		$(OBJ_DIR)/Utility/Unescape.o $(OBJ_DIR)/Utility/Utility.o \
		$(OBJ_DIR)/Config.o $(BEAKER_LIB) -o $@ $(LDFLAGS) $(TEST_REST_LINK) -lz

$(BIN_DIR)/test_engine_registry: $(TEST_DIR)/test_engine_registry.c \
	$(OBJ_DIR)/Scraping/JsonEngines.o $(OBJ_DIR)/Scraping/ScrapingParsers.o \
	$(OBJ_DIR)/Utility/XmlHelper.o $(OBJ_DIR)/Utility/Unescape.o \
	$(OBJ_DIR)/Utility/Utility.o $(OBJ_DIR)/Config.o $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Scraping/JsonEngines.o \
		$(OBJ_DIR)/Scraping/ScrapingParsers.o $(OBJ_DIR)/Utility/XmlHelper.o \
		$(OBJ_DIR)/Utility/Unescape.o $(OBJ_DIR)/Utility/Utility.o \
		$(OBJ_DIR)/Config.o $(BEAKER_LIB) -o $@ $(LDFLAGS) $(TEST_REST_LINK) -lz

$(BIN_DIR)/test_today: $(TEST_DIR)/test_today.c $(OBJ_DIR)/Infobox/Today.o \
	$(TEST_OBJ_HTTP) $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Infobox/Today.o $(TEST_OBJ_HTTP) \
		$(BEAKER_LIB) -o $@ $(LDFLAGS) $(TEST_REST_LINK) -lz

$(BIN_DIR)/test_favicon: $(TEST_DIR)/test_favicon.c $(OBJ_DIR)/Routes/Favicon.o \
	$(TEST_OBJ_HTTP) $(BEAKER_LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(OBJ_DIR)/Routes/Favicon.o $(TEST_OBJ_HTTP) \
		$(BEAKER_LIB) -o $@ $(LDFLAGS) $(TEST_REST_LINK) -lz

$(BIN_DIR)/smoke_http: $(TEST_DIR)/smoke_http.c $(TEST_OBJ_HTTP) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Itests $< $(TEST_OBJ_HTTP) -o $@ $(LDFLAGS) $(TEST_REST_LINK)

test-unit: $(BIN_DIR)/test_unescape $(BIN_DIR)/test_json $(BIN_DIR)/test_parse_url \
	$(BIN_DIR)/test_html_escape $(BIN_DIR)/test_utility $(BIN_DIR)/test_slop_detect
	./$(BIN_DIR)/test_unescape
	./$(BIN_DIR)/test_json
	./$(BIN_DIR)/test_parse_url
	./$(BIN_DIR)/test_html_escape
	./$(BIN_DIR)/test_utility
	./$(BIN_DIR)/test_slop_detect

test-property: $(BIN_DIR)/test_property
	./$(BIN_DIR)/test_property

test-rest: $(BIN_DIR)/test_config $(BIN_DIR)/test_cache $(BIN_DIR)/test_xmlhelper \
	$(BIN_DIR)/test_httpclient $(BIN_DIR)/test_proxy $(BIN_DIR)/test_ratelimit \
	$(BIN_DIR)/test_rank $(BIN_DIR)/test_json_engines $(BIN_DIR)/test_engine_registry \
	$(BIN_DIR)/test_today $(BIN_DIR)/test_favicon $(BIN_DIR)/smoke_http
	./$(BIN_DIR)/test_config
	./$(BIN_DIR)/test_cache
	./$(BIN_DIR)/test_xmlhelper
	./$(BIN_DIR)/test_httpclient
	./$(BIN_DIR)/test_proxy
	./$(BIN_DIR)/test_ratelimit
	./$(BIN_DIR)/test_rank
	./$(BIN_DIR)/test_json_engines
	./$(BIN_DIR)/test_engine_registry
	./$(BIN_DIR)/test_today
	./$(BIN_DIR)/test_favicon
	./$(BIN_DIR)/smoke_http

test-app: $(BIN_DIR)/test_bangs $(BIN_DIR)/test_display $(BIN_DIR)/test_security_headers
	./$(BIN_DIR)/test_bangs
	./$(BIN_DIR)/test_display
	./$(BIN_DIR)/test_security_headers

smoke-rest: $(BIN_DIR)/smoke_http
	./$(BIN_DIR)/smoke_http

test-config: $(BIN_DIR)/test_config
	./$(BIN_DIR)/test_config

test-cache: $(BIN_DIR)/test_cache
	./$(BIN_DIR)/test_cache

test-xmlhelper: $(BIN_DIR)/test_xmlhelper
	./$(BIN_DIR)/test_xmlhelper

test-httpclient: $(BIN_DIR)/test_httpclient
	./$(BIN_DIR)/test_httpclient

test-proxy: $(BIN_DIR)/test_proxy
	./$(BIN_DIR)/test_proxy

test: test-unit test-property test-rest test-app

test-all: test fuzz-smoke

fuzz-smoke: $(BIN_DIR)/fuzz_unescape $(BIN_DIR)/fuzz_parse_url $(BIN_DIR)/fuzz_json \
	$(BIN_DIR)/fuzz_security_headers $(BIN_DIR)/fuzz_html_escape
	./$(BIN_DIR)/fuzz_unescape -runs=$(FUZZ_RUNS)
	./$(BIN_DIR)/fuzz_parse_url -runs=$(FUZZ_RUNS)
	./$(BIN_DIR)/fuzz_json -runs=$(FUZZ_RUNS)
	./$(BIN_DIR)/fuzz_security_headers -runs=$(FUZZ_RUNS)
	./$(BIN_DIR)/fuzz_html_escape -runs=$(FUZZ_RUNS)

fuzz-all: $(BIN_DIR)/fuzz_unescape $(BIN_DIR)/fuzz_parse_url $(BIN_DIR)/fuzz_json \
	$(BIN_DIR)/fuzz_security_headers $(BIN_DIR)/fuzz_html_escape

check: $(TARGET) test-all smoke-local

smoke-local: $(TARGET)
	sh scripts/smoke-local.sh

fuzz-unescape: $(BIN_DIR)/fuzz_unescape

fuzz-parse-url: $(BIN_DIR)/fuzz_parse_url

fuzz-json: $(BIN_DIR)/fuzz_json

fuzz-security-headers: $(BIN_DIR)/fuzz_security_headers

fuzz-html-escape: $(BIN_DIR)/fuzz_html_escape

test-unescape: $(BIN_DIR)/test_unescape
	./$(BIN_DIR)/test_unescape

test-json: $(BIN_DIR)/test_json
	./$(BIN_DIR)/test_json

test-parse-url: $(BIN_DIR)/test_parse_url
	./$(BIN_DIR)/test_parse_url

smoke-http: $(BIN_DIR)/smoke_http
	./$(BIN_DIR)/smoke_http

.PHONY: smoke-podman
smoke-podman:
	./scripts/smoke-container.sh

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(ASAN_OBJ_DIR) beaker/build-asan
	@echo "Cleaned build artifacts"

rebuild: clean all

info:
	@echo "Compiler:    $(CC)"
	@echo "CFlags:      $(CFLAGS)"
	@echo ""
	@echo "Sources to compile:"
	@echo "$(SRCS)" | tr ' ' '\n'
	@echo ""
	@echo "Object files to generate:"
	@echo "$(OBJS)" | tr ' ' '\n'

ifeq ($(UNAME_S),Darwin)
PREFIX      ?= /usr/local
DATA_DIR    ?= $(PREFIX)/etc/seeker
CONF_DIR    ?= $(DATA_DIR)
VAR_DIR     ?= $(PREFIX)/var/lib/seeker
LOG_DIR     ?= $(PREFIX)/var/log/seeker
CACHE_DIR   ?= $(PREFIX)/var/cache/seeker
else
PREFIX      ?= /usr
DATA_DIR    ?= /etc/seeker
CONF_DIR    ?= /etc/seeker
VAR_DIR     ?= /var/lib/seeker
LOG_DIR     ?= /var/log/seeker
CACHE_DIR   ?= /var/cache/seeker
endif

INSTALL_BIN_DIR := $(PREFIX)/bin
USER        := seeker
GROUP       := seeker

SYSTEMD_DIR := /etc/systemd/system
OPENRC_DIR  := /etc/init.d
DINIT_DIR   := /etc/dinit.d
LAUNCHD_DIR ?= /Library/LaunchDaemons
LAUNCHD_LABEL ?= monster.bwaaa.seeker
LAUNCHD_PLIST := $(LAUNCHD_DIR)/$(LAUNCHD_LABEL).plist

install:
	@echo "Available install targets:"
	@echo "  make install-systemd"
	@echo "  make install-openrc"
	@echo "  make install-runit"
	@echo "  make install-s6"
	@echo "  make install-dinit"
	@if [ "$(UNAME_S)" = "Darwin" ]; then echo "  make install-launchd"; fi
	@echo ""
	@echo "Example: doas/sudo make install-openrc"

install-launchd: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(INSTALL_BIN_DIR) $(LOG_DIR)
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@mkdir -p $(LAUNCHD_DIR)
	@sed \
		-e 's|@INSTALL_BIN_DIR@|$(INSTALL_BIN_DIR)|g' \
		-e 's|@DATA_DIR@|$(DATA_DIR)|g' \
		-e 's|@LOG_DIR@|$(LOG_DIR)|g' \
		-e 's|@LAUNCHD_LABEL@|$(LAUNCHD_LABEL)|g' \
		init/launchd/omnisearch.plist.in > $(LAUNCHD_PLIST)
	@chmod 644 $(LAUNCHD_PLIST)
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Installed launchd plist to $(LAUNCHD_PLIST)"
	@echo "Load with: sudo launchctl bootstrap system $(LAUNCHD_PLIST)"
	@echo "Enable with: sudo launchctl enable system/$(LAUNCHD_LABEL)"
	@echo "Start with: sudo launchctl kickstart -k system/$(LAUNCHD_LABEL)"

install-systemd: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(LOG_DIR) $(CACHE_DIR)
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@echo "Setting up user '$(USER)'..."
	@(grep -q '^$(GROUP):' /etc/group || groupadd $(GROUP)) 2>/dev/null || true
	@id -u $(USER) >/dev/null 2>&1 || useradd --system --home $(DATA_DIR) --shell /usr/sbin/nologin -g $(GROUP) $(USER)
	@chown -R $(USER):$(GROUP) $(LOG_DIR) $(CACHE_DIR) $(VAR_DIR) $(DATA_DIR) 2>/dev/null || true
	@chown $(USER):$(GROUP) $(DATA_DIR)/config.ini 2>/dev/null || true
	install -m 644 init/systemd/seeker.service $(SYSTEMD_DIR)/seeker.service
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Edit config with: nano $(DATA_DIR)/config.ini"
	@echo "Installed systemd service to $(SYSTEMD_DIR)/seeker.service"
	@echo "Run 'systemctl enable --now seeker' to start"

install-openrc: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(LOG_DIR) $(CACHE_DIR)
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@echo "Setting up user '$(USER)'..."
	@(grep -q '^$(GROUP):' /etc/group || groupadd $(GROUP)) 2>/dev/null || true
	@id -u $(USER) >/dev/null 2>&1 || useradd --system --home $(DATA_DIR) --shell /usr/sbin/nologin -g $(GROUP) $(USER)
	@chown -R $(USER):$(GROUP) $(LOG_DIR) $(CACHE_DIR) $(VAR_DIR) $(DATA_DIR) 2>/dev/null || true
	@chown $(USER):$(GROUP) $(DATA_DIR)/config.ini 2>/dev/null || true
	install -m 755 init/openrc/seeker $(OPENRC_DIR)/seeker
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Edit config with: nano $(DATA_DIR)/config.ini"
	@echo "Installed openrc service to $(OPENRC_DIR)/seeker"
	@echo "Run 'rc-update add seeker default' to enable"

install-runit: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(LOG_DIR) $(CACHE_DIR) /etc/service/seeker/log/
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@echo "Setting up user '$(USER)'..."
	@(grep -q '^$(GROUP):' /etc/group || groupadd $(GROUP)) 2>/dev/null || true
	@id -u $(USER) >/dev/null 2>&1 || useradd --system --home $(DATA_DIR) --shell /usr/sbin/nologin -g $(GROUP) $(USER)
	@chown -R $(USER):$(GROUP) $(LOG_DIR) $(CACHE_DIR) $(VAR_DIR) $(DATA_DIR) 2>/dev/null || true
	@chown $(USER):$(GROUP) $(DATA_DIR)/config.ini 2>/dev/null || true
	@mkdir -p /etc/service/seeker/log/supervise/control
	install -m 755 init/runit/run /etc/service/seeker/run
	install -m 755 init/runit/log/run /etc/service/seeker/log/run
	install -m 755 init/runit/log/run /etc/service/seeker/log/supervise/control
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Edit config with: nano $(DATA_DIR)/config.ini"
	@echo "Installed runit service to /etc/service/seeker"
	@echo "Service will start automatically"
	@echo "Void: ln -s /etc/service/seeker/ /var/service"
	@echo "Artix: ln -s /etc/service/seeker/ /run/runit/"

install-s6: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(LOG_DIR) $(CACHE_DIR)
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@echo "Setting up user '$(USER)'..."
	@(grep -q '^$(GROUP):' /etc/group || groupadd $(GROUP)) 2>/dev/null || true
	@id -u $(USER) >/dev/null 2>&1 || useradd --system --home $(DATA_DIR) --shell /usr/sbin/nologin -g $(GROUP) $(USER)
	@chown -R $(USER):$(GROUP) $(LOG_DIR) $(CACHE_DIR) $(VAR_DIR) $(DATA_DIR) 2>/dev/null || true
	@chown $(USER):$(GROUP) $(DATA_DIR)/config.ini 2>/dev/null || true
	@mkdir -p /var/service/seeker/log/supervise/control
	install -m 755 init/s6/run /var/service/seeker/run
	install -m 755 init/s6/log/run /var/service/seeker/log/run
	install -m 755 init/s6/log/run /var/service/seeker/log/supervise/control
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Edit config with: nano $(DATA_DIR)/config.ini"
	@echo "Installed s6 service to /var/service/seeker"
	@echo "Service will start automatically"

install-dinit: $(TARGET)
	@mkdir -p $(DATA_DIR)/templates $(DATA_DIR)/static $(LOG_DIR) $(CACHE_DIR)
	@cp -rf templates/* $(DATA_DIR)/templates/
	@cp -rf static/* $(DATA_DIR)/static/
	@mkdir -p $(DATA_DIR)/locales
	@cp -rf locales/* $(DATA_DIR)/locales/
	@cp -n example-config.ini $(DATA_DIR)/config.ini || true
	install -m 755 $(TARGET) $(INSTALL_BIN_DIR)/seeker
	@echo "Setting up user '$(USER)'..."
	@(grep -q '^$(GROUP):' /etc/group || groupadd $(GROUP)) 2>/dev/null || true
	@id -u $(USER) >/dev/null 2>&1 || useradd --system --home $(DATA_DIR) --shell /usr/sbin/nologin -g $(GROUP) $(USER)
	@chown -R $(USER):$(GROUP) $(LOG_DIR) $(CACHE_DIR) $(VAR_DIR) $(DATA_DIR) 2>/dev/null || true
	@chown $(USER):$(GROUP) $(DATA_DIR)/config.ini 2>/dev/null || true
	install -m 644 init/dinit/seeker $(DINIT_DIR)/seeker
	@echo ""
	@echo "Config: $(DATA_DIR)/config.ini"
	@echo "Edit config with: nano $(DATA_DIR)/config.ini"
	@echo "Installed dinit service to $(DINIT_DIR)/seeker"
	@echo "Run 'dinitctl enable seeker' to start"

uninstall:
	rm -f $(INSTALL_BIN_DIR)/seeker
	rm -rf $(DATA_DIR)
	rm -f $(SYSTEMD_DIR)/seeker.service
	rm -f $(OPENRC_DIR)/seeker
	rm -f $(DINIT_DIR)/seeker
	rm -f $(LAUNCHD_PLIST)
	rm -rf /etc/service/seeker
	rm -rf /var/service/seeker
	@id -u $(USER) >/dev/null 2>&1 && userdel $(USER) 2>/dev/null || true
	@grep -q '^$(GROUP):' /etc/group 2>/dev/null && groupdel $(GROUP) 2>/dev/null || true
	@echo "Uninstalled seeker"

.PHONY: all run clean rebuild info test-engine test test-all test-unit test-property asan \
	test-rest test-app test-config test-cache test-xmlhelper test-httpclient test-proxy \
	smoke-rest smoke-http smoke-local check \
	test-unescape test-json test-parse-url fuzz-all fuzz-smoke fuzz-unescape fuzz-parse-url fuzz-json \
	fuzz-security-headers fuzz-html-escape
