.PHONY: upload build clean build-game unbrick upload-game

ENV ?= v2

build:
	pio run -e $(ENV)
	python3 prog_tools/check_boot.py .pio/build/$(ENV)/firmware.bin
	cp .pio/build/$(ENV)/firmware.bin build/bootloader.bin

upload: build
	#pio run -e $(ENV) -t upload
	python3 prog_tools/chip_tool.py flash build/bootloader.bin

# Build a game from src_games/ (e.g. make build-game GAME=OSDemo)
# PLATFORMIO_SRC_DIR overrides the src_dir option for this build only, so
# platformio.ini is left untouched. The game is linked for the app region
# (linker/app_flash.ld, FLASH ORIGIN 0x8000) via PLATFORMIO_BUILD_FLAGS, which
# suppresses the framework's Link.ld (the base builder skips its -T when a
# -Wl,-T flag is already present).
#
# The raw firmware.bin is then wrapped with a 20-byte "PH25" header + CRC-32
# (prog_tools/mkapp.py) so the bootloader can verify it before flashing. The
# wrapped image goes to a stable, game-specific path (build/games/<GAME>.bin)
# so a later bootloader build can't clobber it. Flash the wrapped image onto
# the SD card and use "A:Exec" in the bootloader file browser.
APP_LINKER := $(CURDIR)/linker/app_flash.ld

build-game:
	@[ "${GAME}" ] || (echo "Usage: make build-game GAME=<name>  (e.g. GAME=OSDemo)"; exit 1)
	@[ -d "src_games/$(GAME)" ] || (echo "ERROR: no game directory 'src_games/$(GAME)'"; echo "Available games:"; ls -1 src_games; exit 1)
	PLATFORMIO_BUILD_FLAGS="-Wl,-T,$(APP_LINKER)" PLATFORMIO_SRC_DIR=src_games/$(GAME) pio run -e $(ENV)
	@mkdir -p build/games
	@python3 prog_tools/mkapp.py .pio/build/$(ENV)/firmware.bin build/games/$(GAME).bin
	@echo "Game image: build/games/$(GAME).bin (copy to the SD card)"

clean:
	pio run -t clean

unbrick:
	python3 prog_tools/chip_tool.py wipe

upload-game:
	@[ "${GAME}" ] || (echo "Usage: make upload-game GAME=<name>  (e.g. GAME=OSDemo)"; exit 1)
	python3 prog_tools/chip_tool.py flash-pair build/bootloader.bin build/games/$(GAME).bin
