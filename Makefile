# Shortcuts for converting the original data and building the game. `make` alone prints the help.
# The real work is done by kp_convert (built here), `python3 -m converter` and tools/product.sh; this file only chains
# them.
# Works with the GNU make 3.81 that macOS ships.

SHELL := /bin/bash

# --- Settings (override on the command line: make convert SC=/path/to/StarCraft) ---------------------------------
SC       ?=
SCR      ?=
OUT      ?= generated
PYTHON   ?= python3
VARIANT  ?= release
HD       ?= hd2
PORT     ?= 8793
ARGS     ?=
BUILD_DIR ?= build/app
TOOLS_DIR ?= build/cmake

CONVERTER := $(PYTHON) -m converter
# The C converter (ADR 0019): verify, the repack (the cinematics too), the test scenarios, the terrain checks and the
# icon
KP_CONVERT := $(TOOLS_DIR)/kp_convert

# The bundles read the converter's output from here (tools/product.sh)
export KOPRULU_GENERATED := $(OUT)

PLATFORMS := macos web android ios ios-sim linux windows

.DEFAULT_GOAL := help
.PHONY: help convert convert-remastered remastered-extras check-source check-sc check-scr verify repack console \
	scenarios terrain icon hd locale-check convert-tool toolchain app run test serve \
	install-android clean $(PLATFORMS)

help:
	@echo "Koprulu Sector: convert the original data, then build the game."
	@echo ""
	@echo "Convert (SC = a StarCraft: Brood War 1.17.0.1 folder, SCR = a StarCraft: Remastered folder):"
	@echo "  make convert SC=DIR          verify, the repack, the test scenarios, the terrain checks and the icon"
	@echo "                               from 1.17"
	@echo "  make convert SCR=DIR         the repack, the test scenarios, the terrain checks and the console's"
	@echo "                               pieces from Remastered"
	@echo "  make repack SC=DIR|SCR=DIR   the original files the game reads: OUT/repack (groups: the phones, the"
	@echo "                               web) and OUT/repack-single (one koprulu.mpq: the computers), the"
	@echo "                               cinematics apart in both (1.17's transcoded into WebM, Remastered's"
	@echo "                               SD and HD2 sets as they are); from Remastered a group per language it"
	@echo "                               holds (its texts and speech)"
	@echo "  make console SCR=DIR         the console's pieces OUT/data/console.kpd from Remastered's art"
	@echo "  make scenarios SC=DIR|SCR=DIR  the test scenarios' CHKs OUT/scenarios/ (tests, the oracle's tools)"
	@echo "  make terrain SC=DIR|SCR=DIR    the terrain checks' CRCs OUT/viewer/ (koprulu --autotest)"
	@echo "  make convert-remastered SCR=DIR"
	@echo "                               all of the above from Remastered plus its HD art"
	@echo "  make hd SCR=DIR [HD=hd2|hd|all]"
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

# The game reads the original files (ADR 0018): the repack, and the console's pieces (Remastered's art) for SCR; the
# test scenarios and the terrain checks serve the tests and a debug bundle. The icon needs StarCraft.exe, so only from
# 1.17.
convert: check-source
ifneq ($(strip $(SC)),)
	$(MAKE) verify
endif
	$(MAKE) repack scenarios terrain
ifneq ($(strip $(SCR)),)
	$(MAKE) console
endif
ifneq ($(strip $(SC)),)
	$(MAKE) icon
endif

convert-remastered: check-scr
	$(MAKE) convert SC=
	$(MAKE) remastered-extras

remastered-extras: check-scr
	$(MAKE) hd

locale-check: check-scr convert-tool
	$(KP_CONVERT) locale-check "$(SCR)" --out "$(OUT)"

verify: check-sc convert-tool
	$(KP_CONVERT) verify "$(SC)"

scenarios: check-source convert-tool
	$(KP_CONVERT) scenarios "$(SOURCE)" --out "$(OUT)"

terrain: check-source convert-tool
	$(KP_CONVERT) terrain "$(SOURCE)" --out "$(OUT)"

console: check-scr
	$(CONVERTER) console --scr "$(SCR)" --out "$(OUT)"

# The game's data (ADR 0018): the original files repacked, in groups and in one file (the cinematics apart in both)
repack: check-source convert-tool
	$(KP_CONVERT) repack "$(SOURCE)" --out "$(OUT)/repack"
	$(KP_CONVERT) repack "$(SOURCE)" --single --out "$(OUT)/repack-single"

icon: check-sc convert-tool
	$(KP_CONVERT) icon "$(SC)" --out "$(OUT)"

hd: check-scr
	$(CONVERTER) hd --scr "$(SCR)" --variant $(HD) --out "$(OUT)"

convert-tool: toolchain
	cmake -S . -B $(TOOLS_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release
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
