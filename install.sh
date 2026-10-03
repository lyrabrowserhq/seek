#!/bin/bash

# Seek Automated Installation Script
# Builds the vendored beaker library (static) and Seek, detects your init system, and sets up the service.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Seek Installation ===${NC}"

# 1. Dependency Check
echo -e "\n${BLUE}[1/3] Checking dependencies...${NC}"
DEPENDENCIES=("cc" "make" "pkg-config" "curl" "libxml-2.0")
MISSING=()

for dep in "${DEPENDENCIES[@]}"; do
    if [[ "$dep" == "libxml-2.0" ]]; then
        if ! pkg-config --exists libxml-2.0; then MISSING+=("libxml2-dev"); fi
    elif [[ "$dep" == "libcurl" ]]; then
        if ! pkg-config --exists libcurl; then MISSING+=("libcurl-dev"); fi
    else
        if ! command -v "$dep" &> /dev/null; then MISSING+=("$dep"); fi
    fi
done

if [ ${#MISSING[@]} -ne 0 ]; then
    echo -e "${RED}Error: Missing dependencies: ${MISSING[*]}${NC}"
    echo "Please install them using your package manager (pacman, apt, dnf, etc.) and try again."
    exit 1
fi
echo -e "${GREEN}Dependencies OK.${NC}"

# 2. Build Seek (Makefile builds beaker/build/libbeaker.a and links it statically)
echo -e "\n${BLUE}[2/3] Building Seek...${NC}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
if [ ! -f beaker/Makefile ] || [ ! -f beaker/beaker.h ]; then
    echo -e "${RED}Error: vendored beaker/ is missing. Clone with submodules or copy the beaker tree.${NC}"
    exit 1
fi
make clean
make
echo -e "${GREEN}Seek built successfully.${NC}"

# 3. Detect Init System and Install
echo -e "\n${BLUE}[3/3] Detecting init system...${NC}"

DETECTED_INIT=""

if [ -d /run/systemd/system ]; then
    DETECTED_INIT="systemd"
elif [ -f /etc/init.d/functions.sh ] || [ -f /sbin/openrc ]; then
    DETECTED_INIT="openrc"
elif command -v dinit &> /dev/null; then
    DETECTED_INIT="dinit"
elif command -v runsv &> /dev/null; then
    DETECTED_INIT="runit"
elif command -v s6-svscan &> /dev/null; then
    DETECTED_INIT="s6"
fi

if [ -n "$DETECTED_INIT" ]; then
    echo -e "Detected init system: ${GREEN}$DETECTED_INIT${NC}"
    read -p "Do you want to install the service for $DETECTED_INIT? [Y/n]: " CONFIRM
    CONFIRM=${CONFIRM:-Y}
    if [[ "$CONFIRM" =~ ^[Yy]$ ]]; then
        SELECTED_INIT=$DETECTED_INIT
    fi
fi

if [ -z "$SELECTED_INIT" ]; then
    echo -e "${BLUE}Please select your init system for service installation:${NC}"
    options=("systemd" "openrc" "dinit" "runit" "s6" "none")
    select opt in "${options[@]}"; do
        case $opt in
            "systemd"|"openrc"|"dinit"|"runit"|"s6")
                SELECTED_INIT=$opt
                break
                ;;
            "none")
                echo "Skipping service installation."
                exit 0
                ;;
            *) echo "Invalid option $REPLY";;
        esac
    done
fi

if [ -n "$SELECTED_INIT" ]; then
    echo -e "Installing Seek with ${GREEN}$SELECTED_INIT${NC}..."
    sudo make "install-$SELECTED_INIT"
    echo -e "\n${GREEN}Seek has been installed successfully!${NC}"
    echo -e "Configuration is located at ${BLUE}/etc/seeker/config.ini${NC}"
fi
