PROJECT_NAME = N64FlashcartMenu

.DEFAULT_GOAL := all

SOURCE_DIR = src
ASSETS_DIR = assets
FILESYSTEM_DIR = filesystem
BUILD_DIR = build
OUTPUT_DIR = output

MENU_VERSION ?= "300"
MENU_BASE_VERSION ?= "V0.3.2"
BUILD_TIMESTAMP = "$(shell TZ='UTC' date "+%Y-%m-%d %H:%M:%S %:z")"

include $(N64_INST)/include/n64.mk

N64_ROM_SAVETYPE = none
N64_ROM_RTC = 1
N64_ROM_REGIONFREE = 1
N64_ROM_REGION = E

N64_CFLAGS += -iquote $(SOURCE_DIR) -iquote $(ASSETS_DIR) -I $(SOURCE_DIR)/libs -isystem $(SOURCE_DIR)/libs/miniz -flto=auto $(FLAGS)

SRCS = \
	main.c \
	boot/boot.c \
	boot/cheats.c \
	boot/cic.c \
	boot/reboot.S \
	flashcart/64drive/64drive_ll.c \
	flashcart/64drive/64drive.c \
	flashcart/flashcart_utils.c \
	flashcart/ed64/ed64_vseries.c \
	flashcart/ed64/ed64_xseries.c \
	flashcart/ed64/ed64_xseries_ll.c \
	flashcart/flashcart.c \
	flashcart/sc64/sc64_ll.c \
	flashcart/sc64/sc64.c \
	libs/libspng/spng/spng.c \
	libs/miniz/miniz_tdef.c \
	libs/miniz/miniz_tinfl.c \
	libs/miniz/miniz_zip.c \
	libs/miniz/miniz.c \
	menu/ini_parser.c \
	menu/actions.c \
	menu/game_metadata.c \
	menu/game_special.c \
	menu/bookkeeping.c \
	menu/cart_load.c \
	menu/datel_codes.c \
	menu/disclink.c \
	menu/disk_info.c \
	menu/fonts.c \
	menu/hdmi.c \
	menu/menu.c \
	menu/mp3_player.c \
	menu/path.c \
	menu/png_decoder.c \
	menu/rom_custom.c \
	menu/rom_info.c \
	menu/settings.c \
	menu/sound.c \
	menu/ui_components/background.c \
	menu/ui_components/boxart.c \
	menu/ui_components/common.c \
	menu/ui_components/context_menu.c \
	menu/ui_components/file_info.c \
	menu/ui_components/file_list.c \
	menu/ui_components/tabs.c \
	menu/usb_comm.c \
	menu/views/browser.c \
	menu/views/games_grid.c \
	menu/views/credits.c \
	menu/views/datel_code_editor.c \
	menu/views/error.c \
	menu/views/extract_file.c \
	menu/views/fault.c \
	menu/views/file_info.c \
	menu/views/history_favorites.c \
	menu/views/image_viewer.c \
	menu/views/text_viewer.c \
	menu/views/link_disc.c \
	menu/views/load_disk.c \
	menu/views/load_emulator.c \
	menu/views/load_rom.c \
	menu/views/music_player.c \
	menu/views/rom_boot.c \
	menu/views/startup.c \
	menu/views/system_info.c \
	menu/views/settings_editor.c \
	menu/views/rtc.c \
	menu/views/flashcart_info.c \
	menu/views/cpakfs_manager.c \
	menu/views/cpak_dump_info.c \
	menu/views/cpak_note_dump_info.c \
	utils/cpakfs_utils.c \
	utils/fs.c

FONTS = \
	Firple-Bold.ttf \
	PixelMplus12-Bold.ttf

IMAGES = \
	splash.png \
	placeholder-cart.png \
	placeholder-disc.png \
	credits_logo.png

SOUNDS = \
	cursorsound.wav \
	back.wav \
	bgm.wav \
	enter.wav \
	error.wav \
	settings.wav \
	grid_move.wav \
	grid_enter.wav \
	grid_back.wav \
	launch.wav

# Also build grid SFX to output/menu/sounds/ so users can place their own to override
GRID_SFX_DIR   = $(OUTPUT_DIR)/menu/sounds
GRID_SFX_NAMES = grid_move grid_enter grid_back launch
GRID_SFX_FILES = $(addsuffix .wav64,$(addprefix $(GRID_SFX_DIR)/,$(GRID_SFX_NAMES)))

$(GRID_SFX_DIR)/%.wav64: $(ASSETS_DIR)/sounds/%.wav
	@mkdir -p $(GRID_SFX_DIR)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) --wav-compress 1 -o $(GRID_SFX_DIR) "$<"

OBJS = $(addprefix $(BUILD_DIR)/, $(addsuffix .o,$(basename $(SRCS))))
MINIZ_OBJS = $(filter $(BUILD_DIR)/libs/miniz/%.o,$(OBJS))
SPNG_OBJS = $(filter $(BUILD_DIR)/libs/libspng/%.o,$(OBJS))
DEPS = $(OBJS:.o=.d)

# Boxart art baked into the DFS, keyed by ROM code: assets/images/boxart/<CODE>/<type>.png
# -> filesystem/boxart/<CODE>/<type>.sprite, loaded at runtime as
# rom:/boxart/<CODE>/<type>.sprite. Subdirectory structure is preserved (unlike the
# flat $(notdir ...) image rule), so codes don't collide.
BOXART_PNGS    = $(shell find $(ASSETS_DIR)/images/boxart -name '*.png' 2>/dev/null)
BOXART_SPRITES = $(patsubst $(ASSETS_DIR)/images/%.png,$(FILESYSTEM_DIR)/%.sprite,$(BOXART_PNGS))

FILESYSTEM = \
	$(addprefix $(FILESYSTEM_DIR)/, $(notdir $(FONTS:%.ttf=%.font64))) \
	$(addprefix $(FILESYSTEM_DIR)/, $(notdir $(SOUNDS:%.wav=%.wav64))) \
	$(addprefix $(FILESYSTEM_DIR)/, $(notdir $(IMAGES:%.png=%.sprite))) \
	$(BOXART_SPRITES)

$(MINIZ_OBJS): N64_CFLAGS+=-Wno-unused-function -fcompare-debug-second
$(SPNG_OBJS): N64_CFLAGS+=-DSPNG_USE_MINIZ -fcompare-debug-second
$(FILESYSTEM_DIR)/Firple-Bold.font64: MKFONT_FLAGS+=--compress 1 --outline 1 --size 15 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1
$(FILESYSTEM_DIR)/PixelMplus12-Bold.font64: MKFONT_FLAGS+=--compress 1 --monochrome --outline 1 --size 12 --charset $(ASSETS_DIR)/fonts/charset.txt --ellipsis 2026,1
$(FILESYSTEM_DIR)/placeholder-cart.sprite: MKSPRITE_FLAGS+=--format RGBA32
$(FILESYSTEM_DIR)/placeholder-disc.sprite: MKSPRITE_FLAGS+=--format RGBA32
$(FILESYSTEM_DIR)/credits_logo.sprite: MKSPRITE_FLAGS+=--format RGBA32
$(FILESYSTEM_DIR)/%.wav64: AUDIOCONV_FLAGS=--wav-compress 1

$(@info $(shell mkdir -p ./$(FILESYSTEM_DIR) &> /dev/null))

$(FILESYSTEM_DIR)/%.font64: $(ASSETS_DIR)/fonts/%.ttf
	@echo "    [FONT] $@"
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o $(FILESYSTEM_DIR) "$<"

$(FILESYSTEM_DIR)/%.wav64: $(ASSETS_DIR)/sounds/%.wav
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o $(FILESYSTEM_DIR) "$<"

$(FILESYSTEM_DIR)/%.sprite: $(ASSETS_DIR)/images/%.png
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o $(dir $@) "$<"

# Boxart sprites: RGBA16 keeps photo-like art small. cart3d (grey 3D cartridge renders) used
# to bake RGBA32 to avoid 5-bit grey-plastic banding -- but ORDERED dithering removes that
# banding at half the bytes, so cart3d is now RGBA16 + dither (the big size win for the full
# LaunchBox re-source). Other types: plain RGBA16 (printed art doesn't band).
MKBOXART_FMT = RGBA16
MKBOXART_DITHER =
$(filter %/cart3d.sprite,$(BOXART_SPRITES)): MKBOXART_DITHER := --dither ORDERED
$(FILESYSTEM_DIR)/boxart/%.sprite: $(ASSETS_DIR)/images/boxart/%.png
	@mkdir -p $(dir $@)
	@$(N64_MKSPRITE) --format $(MKBOXART_FMT) $(MKBOXART_DITHER) --compress 3 -o $(dir $@) "$<"

$(BUILD_DIR)/$(PROJECT_NAME).dfs: $(FILESYSTEM)

$(BUILD_DIR)/menu/views/credits.o: .FORCE
$(BUILD_DIR)/menu/views/credits.o: FLAGS+=-DMENU_VERSION=\"$(MENU_VERSION)\" -DBUILD_TIMESTAMP=\"$(BUILD_TIMESTAMP)\" -DLIBDRAGON_VERSION=\"$(LIBDRAGON_VERSION)\" -DMENU_BASE_VERSION=\"$(MENU_BASE_VERSION)\"

$(BUILD_DIR)/$(PROJECT_NAME).elf: $(OBJS)

disassembly: $(BUILD_DIR)/$(PROJECT_NAME).elf
	@$(N64_OBJDUMP) -S $< > $(BUILD_DIR)/$(PROJECT_NAME).lst
.PHONY: disassembly

$(PROJECT_NAME).z64: N64_ROM_TITLE=$(PROJECT_NAME)
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).dfs

$(@info $(shell mkdir -p ./$(OUTPUT_DIR) &> /dev/null))

$(OUTPUT_DIR)/$(PROJECT_NAME).n64: $(PROJECT_NAME).z64
	@mv $< $@

64drive: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/menu.bin
.PHONY: 64drive

ed64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64.v64
.PHONY: ed64

ed64-clone: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/OS64P.v64
.PHONY: ed64-clone

sc64: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
	@cp $< $(OUTPUT_DIR)/sc64menu.n64
.PHONY: sc64

all: $(OUTPUT_DIR)/$(PROJECT_NAME).n64 64drive ed64 ed64-clone sc64 $(GRID_SFX_FILES)
.PHONY: all

clean:
	@rm -f ./$(FILESYSTEM)
	@find ./$(FILESYSTEM_DIR) -type d -empty -delete
	@rm -rf ./$(BUILD_DIR) ./$(OUTPUT_DIR)
.PHONY: clean

format:
	@find ./$(SOURCE_DIR) \
		-path \./$(SOURCE_DIR)/libs -prune \
		-o -iname *.c -print \
		-o -iname *.h -print \
		| xargs clang-format -i

deploy: $(OUTPUT_DIR)/sc64menu.n64
	~/.local/bin/sc64deployer upload $<
.PHONY: deploy

run: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat
else
	./remotedeploy.sh
endif
.PHONY: run

run-debug: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /d
else
	./remotedeploy.sh -d
endif
.PHONY: run-debug

run-debug-reboot: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dr
else
	./remotedeploy.sh -dr
endif
.PHONY: run-debug-reboot

run-debug-upload: $(OUTPUT_DIR)/$(PROJECT_NAME).n64
ifeq ($(OS),Windows_NT)
	./localdeploy.bat /dur
else
	./remotedeploy.sh -dur
endif
.PHONY: run-debug-upload

# test:
#   TODO: run tests

.FORCE:

-include $(DEPS)
