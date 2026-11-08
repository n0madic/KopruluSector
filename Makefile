# Shortcuts for converting the original data and building the game. `make` alone prints the help.
# The real work is done by `python3 -m converter` and tools/product.sh; this file only chains them.
# Works with the GNU make 3.81 that macOS ships.

SHELL := /bin/bash

# --- Settings (override on the command line: make convert SC=/path/to/StarCraft) ---------------------------------
SC       ?=
SCR      ?=
OUT      ?= generated
PYTHON   ?= python3
VARIANT  ?= release
HD       ?= hd2
CINE     ?= hd2
LOCALE   ?= all
SPEECH   ?=
PORT     ?= 8793
ARGS     ?=
BUILD_DIR ?= build/app
TOOLS_DIR ?= build/cmake

CONVERTER := $(PYTHON) -m converter

# The bundles read the converter's output from here (tools/product.sh)
export KOPRULU_GENERATED := $(OUT)

PLATFORMS := macos web android ios ios-sim linux windows

.DEFAULT_GOAL := help
.PHONY: help convert convert-remastered remastered-extras check-source check-sc check-scr verify missions data \
	resources icon hd cinematics locale speech fnt-tool toolchain app run test serve install-android clean \
	$(PLATFORMS)

help:
	@echo "Koprulu Sector: convert the original data, then build the game."
	@echo ""
	@echo "Convert (SC = a StarCraft: Brood War 1.17.0.1 folder, SCR = a StarCraft: Remastered folder):"
	@echo "  make convert SC=DIR          verify, missions, data, resources and icon from 1.17"
	@echo "  make convert SCR=DIR         missions, data and resources from Remastered"
	@echo "  make convert-remastered SCR=DIR"
	@echo "                               all of the above from Remastered plus its HD art, cinematics, languages"
	@echo "                               and SPEECH"
	@echo "  make hd SCR=DIR [HD=hd2|hd|all]"
	@echo "  make cinematics SCR=DIR [CINE=sd|hd2|all]"
	@echo "  make locale SCR=DIR [LOCALE=all|ruRU|deDE|frFR|plPL]"
	@echo "  make speech SCR=DIR SPEECH=\"ruRU deDE\""
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
	@echo "KOPRULU_LOCALES, KOPRULU_SPEECH and KOPRULU_PHONE_BLOCKS narrow it (see README.md)."

# --- Conversion ---------------------------------------------------------------------------------------------------

# The source of missions, data and resources: the 1.17 folder if given, else the Remastered one
ifneq ($(strip $(SC)),)
SOURCE := --data "$(SC)"
else ifneq ($(strip $(SCR)),)
SOURCE := --scr "$(SCR)"
endif

check-source:
	@if [ -z "$(strip $(SC))$(strip $(SCR))" ]; then \
		echo "make: give SC=<StarCraft 1.17.0.1 folder> or SCR=<StarCraft: Remastered folder>" >&2; exit 2; fi

check-sc:
	@if [ -z "$(strip $(SC))" ]; then echo "make: $(MAKECMDGOALS) needs SC=<StarCraft 1.17.0.1 folder>" >&2; exit 2; fi

check-scr:
	@if [ -z "$(strip $(SCR))" ]; then echo "make: $(MAKECMDGOALS) needs SCR=<StarCraft: Remastered folder>" >&2; exit 2; fi

# One after another: resources reads what missions and data wrote. The icon needs StarCraft.exe, so only from 1.17.
convert: check-source
ifneq ($(strip $(SC)),)
	$(MAKE) verify
endif
	$(MAKE) missions
	$(MAKE) data
	$(MAKE) resources
ifneq ($(strip $(SC)),)
	$(MAKE) icon
endif

convert-remastered: check-scr
	$(MAKE) convert SC=
	$(MAKE) remastered-extras

remastered-extras: check-scr
	$(MAKE) hd
	$(MAKE) cinematics
	$(MAKE) locale
ifneq ($(strip $(SPEECH)),)
	$(MAKE) speech
endif

verify: check-sc
	$(CONVERTER) verify --data "$(SC)"

missions data resources: check-source
	$(CONVERTER) $@ $(SOURCE) --out "$(OUT)"

icon: check-sc
	$(CONVERTER) icon --data "$(SC)" --out "$(OUT)"

hd: check-scr
	$(CONVERTER) hd --scr "$(SCR)" --variant $(HD) --out "$(OUT)"

cinematics: check-scr
	$(CONVERTER) cinematics --scr "$(SCR)" --variant $(CINE) --out "$(OUT)"

locale: check-scr fnt-tool
	$(CONVERTER) locale --scr "$(SCR)" --locale $(LOCALE) --out "$(OUT)"

speech: check-scr
	@if [ -z "$(strip $(SPEECH))" ]; then echo "make: speech needs SPEECH=\"ruRU ...\"" >&2; exit 2; fi
	@set -e; for loc in $(SPEECH); do \
		echo "$(CONVERTER) speech --scr \"$(SCR)\" --locale $$loc --out \"$(OUT)\""; \
		$(CONVERTER) speech --scr "$(SCR)" --locale $$loc --out "$(OUT)"; \
	done

# The converter's locale step runs this font tool (converter/emit/locale.py looks for it in build/cmake)
fnt-tool: toolchain
	cmake -S . -B $(TOOLS_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build $(TOOLS_DIR) --target kp_fnt_make

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

# The game reads <data folder>/generated/ and, run from the repository root, finds it by itself (a conversion to
# another OUT is played from a bundle, whose staging puts it under generated/)
run: app
	$(BUILD_DIR)/koprulu $(ARGS)

test:
	tools/ci.sh

clean:
	rm -rf build dist
