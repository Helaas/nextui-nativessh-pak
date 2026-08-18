# ──────────────────────────────────────────────────────────────
# Native SSH Pak — Build System
# ──────────────────────────────────────────────────────────────

SHELL := /bin/bash

APP_NAME := nativessh
PAK_NAME := NativeSSH
APOSTROPHE_DIR := third_party/apostrophe
BUILD_DIR := build
DIST_DIR := $(BUILD_DIR)/release
RELEASE_FILENAME := NativeSSH.pak.zip
SRC_FILES := $(shell find src -name '*.c' -print | sort)

TG5040_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain:latest
TG5050_TOOLCHAIN := ghcr.io/loveretro/tg5050-toolchain:latest
MY355_TOOLCHAIN  := ghcr.io/loveretro/my355-toolchain:latest
UNIVERSAL_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab
ADB ?= adb

COMMON_INCLUDES := -I$(APOSTROPHE_DIR)/include

.PHONY: all native mac run-mac run-native universal tg5040 tg5050 my355 \
	package package-universal package-matrix package-tg5040 package-tg5050 package-my355 do-package \
	deploy deploy-platform clean help

# ── Default target ──────────────────────────────────────────

native: mac
run-native: run-mac
all: universal

# ── Native macOS build ──────────────────────────────────────

mac: $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g \
		-DPLATFORM_MAC \
		$(COMMON_INCLUDES) \
		$(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) \
		$(SRC_FILES) \
		$(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image) \
		-lm -lpthread

run-mac: mac
	./$(BUILD_DIR)/mac/$(APP_NAME)

# ── Docker cross-compilation ────────────────────────────────

universal: $(APOSTROPHE_DIR)/include/apostrophe.h
	@mkdir -p $(BUILD_DIR)/universal
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(UNIVERSAL_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile \
			PLATFORM_DEFINE=PLATFORM_NEXTUI \
			BUILD_DIR=/workspace/$(BUILD_DIR)/universal

tg5040:
	@mkdir -p $(BUILD_DIR)/tg5040
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(TG5040_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/tg5040

tg5050:
	@mkdir -p $(BUILD_DIR)/tg5050
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(TG5050_TOOLCHAIN) \
		make -C /workspace -f ports/tg5050/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/tg5050

my355:
	@mkdir -p $(BUILD_DIR)/my355
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(MY355_TOOLCHAIN) \
		make -C /workspace -f ports/my355/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/my355

# ── Packaging ───────────────────────────────────────────────

package-tg5040: tg5040
	@$(MAKE) do-package PLATFORM=tg5040 BIN_SRC=$(BUILD_DIR)/tg5040/$(APP_NAME)

package-tg5050: tg5050
	@$(MAKE) do-package PLATFORM=tg5050 BIN_SRC=$(BUILD_DIR)/tg5050/$(APP_NAME)

package-my355: my355
	@$(MAKE) do-package PLATFORM=my355 BIN_SRC=$(BUILD_DIR)/my355/$(APP_NAME)

package-universal: universal
	@$(MAKE) do-package PLATFORM=universal BIN_SRC=$(BUILD_DIR)/universal/$(APP_NAME)
	@cmp -s "$(BUILD_DIR)/universal/$(APP_NAME)" \
		"$(BUILD_DIR)/universal/$(PAK_NAME).pak/$(APP_NAME)"
	@echo "Verified the packaged universal device binary."

do-package:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(BIN_SRC)" ]; then \
		echo "Error: do-package requires PLATFORM and BIN_SRC."; \
		exit 1; \
	fi
	@rm -rf $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak
	@mkdir -p $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak
	@cp $(BIN_SRC) $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/
	@cp launch.sh pak.json LICENSE $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/
	@if [ -d "$(BUILD_DIR)/$(PLATFORM)/lib" ]; then \
		mkdir -p "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/lib"; \
		cp -a "$(BUILD_DIR)/$(PLATFORM)/lib/." "$(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak/lib/"; \
	fi
	@mkdir -p $(DIST_DIR)/$(PLATFORM)
	@rm -f $(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip
	@cd $(BUILD_DIR)/$(PLATFORM)/$(PAK_NAME).pak && zip -r "$(CURDIR)/$(DIST_DIR)/$(PLATFORM)/$(PAK_NAME).pak.zip" . -x '.*'

package: package-universal
	@mkdir -p $(DIST_DIR)/all
	@rm -f $(DIST_DIR)/all/$(RELEASE_FILENAME) $(DIST_DIR)/all/$(PAK_NAME).pakz
	@cp $(DIST_DIR)/universal/$(PAK_NAME).pak.zip $(DIST_DIR)/all/$(RELEASE_FILENAME)
	@unzip -Z1 $(DIST_DIR)/all/$(RELEASE_FILENAME) | grep -qx "$(APP_NAME)"

package-matrix: package-tg5040 package-tg5050 package-my355

# ── ADB deploy ──────────────────────────────────────────────

deploy:
	@echo "Detecting platform..."
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	ADB_CMD="$(ADB) -s $$SERIAL"; \
	FINGERPRINT=$$($$ADB_CMD shell ' \
		cat /proc/device-tree/compatible 2>/dev/null; \
		echo; \
		cat /proc/device-tree/model 2>/dev/null; \
		echo; \
		uname -a 2>/dev/null' 2>/dev/null | tr '\000' '\n' | tr -d '\r'); \
	case "$$FINGERPRINT" in \
		*sun50iw9*|*H700*|*h700*) PLATFORM=h700 ;; \
		*rk3566*|*miyoo-355*) PLATFORM=my355 ;; \
		*allwinner,a523*|*sun55iw3*) PLATFORM=tg5050 ;; \
		*allwinner,a133*|*sun50iw*) PLATFORM=tg5040 ;; \
		*allwinner*) \
			if printf '%s' "$$FINGERPRINT" | grep -qi 'a523'; then \
				PLATFORM=tg5050; \
			else \
				PLATFORM=tg5040; \
			fi \
			;; \
		*) \
			echo "Error: Could not detect a supported platform from adb fingerprint."; \
			echo "  Serial: $$SERIAL"; \
			echo "  Fingerprint snippet: $$(printf '%s' "$$FINGERPRINT" | head -c 240)"; \
			exit 1; \
			;; \
	esac; \
	echo "Detected adb serial: $$SERIAL"; \
	echo "Detected platform: $$PLATFORM"; \
	$(MAKE) deploy-platform PLATFORM=$$PLATFORM SERIAL=$$SERIAL

deploy-platform:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(SERIAL)" ]; then \
		echo "Error: deploy-platform requires PLATFORM and SERIAL."; \
		exit 1; \
	fi
	@$(MAKE) package-universal
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	PAK_ROOT="/mnt/SDCARD/Tools/$(PLATFORM)"; \
	PAK_DIR="$$PAK_ROOT/$(PAK_NAME).pak"; \
	echo "Deploying $(PAK_NAME).pak to $$PAK_DIR..."; \
	$$ADB_CMD shell "rm -rf '$$PAK_DIR' && mkdir -p '$$PAK_ROOT'"; \
	$$ADB_CMD push "$(BUILD_DIR)/universal/$(PAK_NAME).pak" "$$PAK_ROOT/"; \
	echo "Deploy complete."

# ── Cleanup ─────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

# ── Help ────────────────────────────────────────────────────

help:
	@echo "Targets:"
	@echo "  native        Build the mac development binary"
	@echo "  run-native    Build and run the mac binary (set AP_WINDOW_WIDTH/AP_WINDOW_HEIGHT to test sizes)"
	@echo "  all           Build one universal NextUI device binary"
	@echo "  mac           Build for macOS (native)"
	@echo "  run-mac       Build and run for macOS"
	@echo "  tg5040        Build for TG5040 (Docker cross-compile)"
	@echo "  tg5050        Build for TG5050 (Docker cross-compile)"
	@echo "  my355         Build for Miyoo Flip (Docker cross-compile)"
	@echo "  universal     Build once for tg5040, tg5050, my355, and h700"
	@echo "  package       Build the platform-neutral Pak Store archive"
	@echo "  package-matrix  Build the legacy three-toolchain regression matrix"
	@echo "  deploy        Detect adb platform, package, and push"
	@echo "  clean         Remove build artifacts"
