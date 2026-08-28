-include .env

CC = cc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ZLIB_LIBS  := $(shell pkg-config --libs zlib 2>/dev/null)
ifeq ($(ZLIB_LIBS),)
ZLIB_LIBS  := -lz
endif
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE -I. $(SDL_CFLAGS)
LDLIBS = $(SDL_LIBS) $(ZLIB_LIBS) -lpthread

OBJS = common.o \
       i8051/cpu.o i8051/op.o i8051/peripheral.o \
       machine/generic.o machine/duart.o machine/vt420.o machine/video.o machine/vt5xx.o \
       lk201/lk201.o lk201/keys.o \
       ssu/chan.o ssu/session.o ssu/xonoff.o ssu/config.o \
       host/comm.o host/logging.o host/unicode.o host/headless.o \
       host/fb_render.o host/sdl.o host/text.o host/termkey.o \
       host/ctl.o host/mcp.o host/json.o host/png.o host/gif.o

HDRS = common.h i8051/i8051.h machine/machine.h lk201/lk201.h ssu/ssu.h host/host.h \
       host/ctl.h host/mcp.h host/json.h host/png.h host/gif.h

all: ezalb

ezalb: $(OBJS) main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

boot_test: $(OBJS) tests/boot_test.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

comm_test: $(OBJS) tests/comm_test.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: boot_test comm_test
	./boot_test roms/vt420/23-068E9-00.bin
	./comm_test

mcp_test: ezalb
	tests/mcp_test.sh ./ezalb roms/vt420/23-068E9-00.bin

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) main.o tests/boot_test.o tests/comm_test.o ezalb boot_test comm_test
	rm -rf $(BUILD)

# ---- macOS app bundle / dmg / notarized release ----

BUILD   = build
APP     = $(BUILD)/Ezalb.app
DMG     = $(BUILD)/Ezalb.dmg
STAGING = $(BUILD)/dmg_staging
ROM     = roms/vt420/23-068E9-00.bin

icon: $(BUILD)/AppIcon.icns

$(BUILD)/AppIcon.icns: macos/gen_icon.swift
	@mkdir -p $(BUILD)
	swift macos/gen_icon.swift $(BUILD)

# Bundles the SDL2 dylib (and SDL3, which sdl2-compat dlopens via
# @loader_path/libSDL3.dylib) so the app runs without Homebrew.
app: ezalb $(BUILD)/AppIcon.icns
	@rm -rf $(APP)
	@mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Frameworks $(APP)/Contents/Resources/roms/vt420
	cp ezalb $(APP)/Contents/MacOS/
	cp macos/Info.plist $(APP)/Contents/
	cp $(BUILD)/AppIcon.icns $(APP)/Contents/Resources/
	cp $(ROM) $(APP)/Contents/Resources/roms/vt420/
	SDL2=$$(otool -L ezalb | awk '/libSDL2/{print $$1; exit}'); \
	cp "$$SDL2" $(APP)/Contents/Frameworks/libSDL2-2.0.0.dylib; \
	chmod 644 $(APP)/Contents/Frameworks/libSDL2-2.0.0.dylib; \
	install_name_tool -id @executable_path/../Frameworks/libSDL2-2.0.0.dylib \
		$(APP)/Contents/Frameworks/libSDL2-2.0.0.dylib; \
	install_name_tool -change "$$SDL2" @executable_path/../Frameworks/libSDL2-2.0.0.dylib \
		$(APP)/Contents/MacOS/ezalb; \
	case "$$SDL2" in *sdl2-compat*) \
		cp "$$(brew --prefix sdl3)/lib/libSDL3.0.dylib" $(APP)/Contents/Frameworks/libSDL3.dylib; \
		chmod 644 $(APP)/Contents/Frameworks/libSDL3.dylib; \
		install_name_tool -id @loader_path/libSDL3.dylib $(APP)/Contents/Frameworks/libSDL3.dylib;; \
	esac
	codesign --force --deep --sign - $(APP)
	@echo "Run with: open $(APP)"

define build_dmg
	@rm -rf $(STAGING) $(1)
	@mkdir -p $(STAGING)
	cp -R $(APP) $(STAGING)/
	ln -s /Applications $(STAGING)/Applications
	hdiutil create -volname Ezalb -srcfolder $(STAGING) -ov -format UDZO $(1)
	@rm -rf $(STAGING)
endef

dmg: app
	$(call build_dmg,$(DMG))

release: app
	@test -n "$(DEV_ID)" || { echo "DEV_ID not set — copy .env.example to .env and fill in"; exit 1; }
	@test -n "$(NOTARY_PROFILE)" || { echo "NOTARY_PROFILE not set — copy .env.example to .env and fill in"; exit 1; }
	codesign --force --options runtime --timestamp --sign "$(DEV_ID)" $(APP)/Contents/Frameworks/*.dylib
	codesign --force --options runtime --timestamp --sign "$(DEV_ID)" $(APP)
	# staple the .app too, so it still validates once dragged out of the dmg
	ditto -c -k --keepParent $(APP) $(BUILD)/Ezalb-app.zip
	xcrun notarytool submit $(BUILD)/Ezalb-app.zip --keychain-profile "$(NOTARY_PROFILE)" --wait
	xcrun stapler staple $(APP)
	@rm -f $(BUILD)/Ezalb-app.zip
	$(call build_dmg,$(DMG))
	codesign --force --timestamp --sign "$(DEV_ID)" $(DMG)
	xcrun notarytool submit $(DMG) --keychain-profile "$(NOTARY_PROFILE)" --wait
	xcrun stapler staple $(DMG)
	@echo "Signed + notarized: $(DMG)"

# ---- Linux packages (built via docker, output to dist/) ----

DOCKER  ?= docker
VERSION ?= 1.0
HOSTARCH := $(shell uname -m | sed -e 's/x86_64/amd64/' -e 's/aarch64/arm64/')

deb: deb-$(HOSTARCH)
deb-all: deb-amd64 deb-arm64
deb-%:
	$(DOCKER) build --platform=linux/$* -t ezalb-deb-builder-$* packaging/deb
	$(DOCKER) run --rm --platform=linux/$* -e VERSION=$(VERSION) \
		-v $(CURDIR):/work -w /work ezalb-deb-builder-$* packaging/deb/build.sh

rpm: rpm-$(HOSTARCH)
rpm-all: rpm-amd64 rpm-arm64
rpm-%:
	$(DOCKER) build --platform=linux/$* -t ezalb-rpm-builder-$* packaging/rpm
	$(DOCKER) run --rm --platform=linux/$* -e VERSION=$(VERSION) \
		-v $(CURDIR):/work -w /work ezalb-rpm-builder-$* packaging/rpm/build.sh

linux-packages: deb rpm
linux-packages-all: deb-all rpm-all

.PHONY: all test mcp_test clean icon app dmg release \
	deb deb-all rpm rpm-all linux-packages linux-packages-all
