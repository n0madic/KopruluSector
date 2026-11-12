# Shortcuts for converting the original data and building the game. `make` alone prints the help.
# The real work is done by kp_convert (built here) and tools/product.sh; this file only chains them.
# Works with the GNU make 3.81 that macOS ships.

SHELL := /bin/bash

# --- Settings (override on the command line: make convert SC=/path/to/StarCraft) ---------------------------------
SC       ?=
SCR      ?=
OUT      ?= generated
PYTHON   ?= python3
VARIANT  ?= release
PORT     ?= 8793
ARGS     ?=
BUILD_DIR ?= build/app
TOOLS_DIR ?= build/cmake

# The C converter (ADR 0019): verify, the repack (the cinematics, the languages, the HD art and interface too), the
# test scenarios, the terrain checks, the icon and the HD interface's placement table
KP_CONVERT := $(TOOLS_DIR)/kp_convert

# The bundles read the converter's output from here (tools/product.sh)
export KOPRULU_GENERATED := $(OUT)

PLATFORMS := macos web android ios ios-sim linux windows

.DEFAULT_GOAL := help
.PHONY: help convert check-source check-sc check-scr verify repack \
	scenarios terrain icon hd-ui-table locale-check convert-tool toolchain app run test serve \
	install-android clean $(PLATFORMS)

help:
	@echo "Koprulu Sector: convert the original data, then build the game."
	@echo ""
	@echo "Convert (SC = a StarCraft: Brood War 1.17.0.1 folder, SCR = a StarCraft: Remastered folder):"
	@echo "  make convert SC=DIR          verify, the repack, the test scenarios, the terrain checks and the icon"
	@echo "                               from 1.17"
	@echo "  make convert SCR=DIR         the repack, the test scenarios and the terrain checks from Remastered"
	@echo "  make repack SC=DIR|SCR=DIR   the original files the game reads: OUT/repack (groups: the phones, the"
	@echo "                               web) and OUT/repack-single (one koprulu.mpq: the computers), the"
	@echo "                               cinematics apart in both (1.17's transcoded into WebM, Remastered's"
	@echo "                               SD and HD2 sets as they are); from Remastered a group per language it"
	@echo "                               holds (its texts and speech), the HD art's sets (the computers' hd2"
	@echo "                               and hd, the web's hd2, the phones' hd2-astc and hd2-etc2) with their"
	@echo "                               interface and portraits, and the consoles' art"
	@echo "  make scenarios SC=DIR|SCR=DIR  the test scenarios' CHKs OUT/scenarios/ (tests, the oracle's tools)"
	@echo "  make terrain SC=DIR|SCR=DIR    the terrain checks' CRCs OUT/viewer/ (koprulu --autotest)"
	@echo "  make hd-ui-table SCR=DIR     the HD interface's placement table defs/scr/ made again (after a"
	@echo "                               Remastered update that changes its interface art)"
	@echo "  make locale-check SCR=DIR    every string of each language Remastered holds checked, the reports"
	@echo "                               OUT/reports/locale-<LOC>.json"
	@echo ""
	@echo "Build a bundle into dist/product/<platform>/ [VARIANT=release|debug]:"
	@echo "  make macos | windows | linux | web | android | ios | ios-sim"
	@echo "  make serve                   serve the web build on http://localhost:$(PORT)/"
	@echo "  make install-android         install the APK on the attached device"
	@echo ""
	@echo "Develop:"
	@echo "  make app                     desktop build in $(BUILD_DIR)"
	@echo "  make run [ARGS=\"--graphics hd2\"]"
	@echo "  make test                    tools/ci.sh"
	@echo "  make clean                   remove build/ and dist/ (keeps $(OUT)/)"
	@echo ""
	@echo "The converted data goes to OUT (now $(OUT)). The bundles take what was converted; KOPRULU_CINEMATICS,"
	@echo "KOPRULU_LOCALES and KOPRULU_PHONE_BLOCKS narrow it (see README.md)."

# --- Conversion ---------------------------------------------------------------------------------------------------

# The source of the repack, the scenarios and the terrain: the 1.17 folder if given, else the Remastered one
SOURCE := $(if $(strip $(SC)),$(SC),$(SCR))

check-source:
	@if [ -z "$(strip $(SC))$(strip $(SCR))" ]; then \
		echo "make: give SC=<StarCraft 1.17.0.1 folder> or SCR=<StarCraft: Remastered folder>" >&2; exit 2; fi

check-sc:
	@if [ -z "$(strip $(SC))" ]; then echo "make: $(MAKECMDGOALS) needs SC=<StarCraft 1.17.0.1 folder>" >&2; exit 2; fi

check-scr:
	@if [ -z "$(strip $(SCR))" ]; then echo "make: $(MAKECMDGOALS) needs SCR=<StarCraft: Remastered folder>" >&2; exit 2; fi

# The game reads the original files (ADR 0018): the repack; the test scenarios and the terrain checks serve the tests
# and a debug bundle. The icon needs StarCraft.exe, so only from 1.17.
convert: check-source
ifneq ($(strip $(SC)),)
	$(MAKE) verify
endif
	$(MAKE) repack scenarios terrain
ifneq ($(strip $(SC)),)
	$(MAKE) icon
endif

locale-check: check-scr convert-tool
	$(KP_CONVERT) locale-check "$(SCR)" --out "$(OUT)"

verify: check-sc convert-tool
	$(KP_CONVERT) verify "$(SC)"

scenarios: check-source convert-tool
	$(KP_CONVERT) scenarios "$(SOURCE)" --out "$(OUT)"

terrain: check-source convert-tool
	$(KP_CONVERT) terrain "$(SOURCE)" --out "$(OUT)"

# The game's data (ADR 0018): the original files repacked, in groups and in one file (the cinematics, the languages and
# the HD sets apart in both). The HD art's sets (ADR 0019, stage C5) come from Remastered: the groups take the web's
# HD2 and the phones' (its pages in ASTC, or in ETC2 for phones without ASTC), the single file the computers' HD2 and
# HD (kp_convert's default).
REPACK_HD := $(if $(strip $(SC)),,--hd hd2,hd2-astc,hd2-etc2)
repack: check-source convert-tool
	$(KP_CONVERT) repack "$(SOURCE)" --out "$(OUT)/repack" $(REPACK_HD)
	$(KP_CONVERT) repack "$(SOURCE)" --single --out "$(OUT)/repack-single"

icon: check-sc convert-tool
	$(KP_CONVERT) icon "$(SC)" --out "$(OUT)"

# The HD interface's placement table (ADR 0019, stage C6), committed: the runtime and the repack lay the interface's
# frames out by it, and leave in SD the files whose content Remastered changed since
hd-ui-table: check-scr convert-tool
	$(KP_CONVERT) hd-ui-table "$(SCR)"

# kp_convert with the ASTC encoder (the phones' HD sets)
convert-tool: toolchain
	tools/toolchain/fetch.sh astc
	cmake -S . -B $(TOOLS_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_ASTC=ON
	cmake --build $(TOOLS_DIR) --target kp_convert

# --- Builds -------------------------------------------------------------------------------------------------------

toolchain:
	tools/toolchain/fetch.sh

$(PLATFORMS):
	tools/product.sh $@ $(VARIANT)

serve:
	$(PYTHON) tools/web_serve.py --dir dist/product/web --port $(PORT)

install-android:
	adb install -r dist/product/android/KopruluSector.apk

app: toolchain
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_APP=ON -DKP_TOOLS=OFF
	cmake --build $(BUILD_DIR) --target koprulu

# The game reads the repack in <data folder>/generated/repack-single (else generated/repack) and the rest of
# generated/; run from the repository root it finds them by itself (a conversion to another OUT is played from a
# bundle, whose staging puts it under generated/, or with --data naming the repack's folder)
run: app
	$(BUILD_DIR)/koprulu $(ARGS)

test:
	tools/ci.sh

clean:
	rm -rf build dist
