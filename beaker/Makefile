CC      ?= cc
UNAME_S := $(shell uname -s)

AR        := ar
CFLAGS    := -Wall -fPIC -I. -Isrc
BUILD_DIR ?= build
OBJ_DIR   := $(BUILD_DIR)/obj

ifeq ($(UNAME_S),Darwin)
INSTALL_PREFIX ?= /usr/local/
SHARED_EXT     := dylib
LDCONFIG       := true
LDFLAGS        := -dynamiclib -lpthread -lz -Wl,-install_name,$(INSTALL_PREFIX)lib/libbeaker.$(SHARED_EXT)
else
INSTALL_PREFIX ?= /usr/
SHARED_EXT     := so
LDCONFIG       := ldconfig
LDFLAGS        := -shared -lpthread -lz
endif

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))

LIB_BASE   := $(BUILD_DIR)/libbeaker
STATIC_LIB := $(LIB_BASE).a
SHARED_LIB := $(LIB_BASE).$(SHARED_EXT)
HEADER     := beaker.h

.PHONY: all clean install uninstall info

all: $(SHARED_LIB) $(STATIC_LIB)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(STATIC_LIB): $(OBJS)
	@echo "Linking static library $@..."
	$(AR) rcs $@ $^
	@echo "Successfully built $@"

$(SHARED_LIB): $(OBJS)
	@echo "Linking shared library $@..."
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lm
	@echo "Successfully built $@"

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up object files and libraries..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete."

install: all
	@echo "Installing libraries to $(INSTALL_PREFIX)lib"
	@mkdir -p $(INSTALL_PREFIX)lib
ifeq ($(UNAME_S),Darwin)
	@rm -f $(INSTALL_PREFIX)lib/libbeaker.so
endif
	@cp $(SHARED_LIB) $(STATIC_LIB) $(INSTALL_PREFIX)lib/
	@$(LDCONFIG) || true
	@echo "Installing $(HEADER) to $(INSTALL_PREFIX)include"
	@mkdir -p $(INSTALL_PREFIX)include
	@cp $(HEADER) $(INSTALL_PREFIX)include
	@echo "Installation complete."

uninstall:
	@echo "Uninstalling from $(INSTALL_PREFIX)..."
	@rm -f $(INSTALL_PREFIX)lib/libbeaker.$(SHARED_EXT)
	@rm -f $(INSTALL_PREFIX)lib/libbeaker.a
ifeq ($(UNAME_S),Darwin)
	@rm -f $(INSTALL_PREFIX)lib/libbeaker.so
endif
	@rm -f $(INSTALL_PREFIX)include/$(HEADER)
	@$(LDCONFIG) || true
	@echo "Uninstallation complete."

info:
	@echo "Detected OS:      '$(UNAME_S)'"
	@echo "Compiler:         $(CC)"
	@echo "Install Prefix:   $(INSTALL_PREFIX)"
	@echo "Shared Library:   $(SHARED_LIB)"
	@echo "Source Files:     $(SRCS)"
