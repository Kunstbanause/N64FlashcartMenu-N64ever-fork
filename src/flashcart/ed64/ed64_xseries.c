#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fatfs/ff.h>
#include <libdragon.h>

#include "utils/fs.h"
#include "utils/utils.h"

#include "../flashcart_utils.h"
#include "ed64_xseries_ll.h"
#include "ed64_xseries.h"

typedef enum {
    // potentially handle if the firmware supports it...
    ED64_X5_0 = 550,
    ED64_X7_0 = 570,
    ED64_UKNOWN = 0,
} ed64_xseries_device_variant_t;

/* ED64 save location base address  */
//#define SRAM_ADDRESS (0xA8000000)

/* Physical (PI) base of the cartridge ROM / SDRAM, for DMA access. */
#define ROM_PI_ADDRESS  (0x10000000)

// ROM byte-swap flag set by flashcart_load_rom() (libcart global).
extern char cart_card_byteswap;

// The ROM is staged through an RDRAM bounce buffer and written to the cartridge
// SDRAM with a PI DMA, rather than using libdragon's SD->SDRAM direct-DMA path.
// That direct path wraps its SDRAM address on large ROMs (the tail of the image
// aliases back over the start), corrupting anything bigger than a few MiB. This
// mirrors how the official EverDrive-64 OS loads ROMs (BiCartRomWr -> sysPI_wr).
#define LOAD_CHUNK_SIZE  KiB(128)
static uint8_t __attribute__((aligned(16))) load_bounce[LOAD_CHUNK_SIZE];

static flashcart_firmware_version_t ed64_xseries_get_firmware_version (void) {
    flashcart_firmware_version_t version_info;
    // FIXME: get version from ll
    version_info.major = 1;
    version_info.minor = 1;
    version_info.revision = 0;

    //ed64_ll_get_version(&version_info.major, &version_info.minor, &version_info.revision);

    return version_info;
}

static flashcart_err_t ed64_xseries_init (void) {

    // Make sure the SDRAM is mapped into the ROM address space so that ROMs
    // written by the menu land in (and boot from) SDRAM.
    ed64x_ll_enable_sdram();

    return FLASHCART_OK;
}

static flashcart_err_t ed64_xseries_deinit (void) {

    // Final step before the menu hands control to the ROM (matches a known-good
    // EverDrive OS): ensure SDRAM is mapped as the cartridge ROM and lock the
    // register window. Safe here because all SD access is already finished.
    ed64x_ll_lock_regs_for_boot();

    return FLASHCART_OK;
}

static ed64_xseries_device_variant_t get_cart_model() {
    ed64_xseries_device_variant_t variant = ED64_X7_0; // FIXME: check cart model from ll for better feature handling.
    return variant;
}

static bool ed64_xseries_has_feature (flashcart_features_t feature) {
    bool is_model_x7 = (get_cart_model() == ED64_X7_0); 
    switch (feature) {
        case FLASHCART_FEATURE_RTC: return is_model_x7 ? true : false;
        case FLASHCART_FEATURE_USB: return is_model_x7 ? true : false;
        case FLASHCART_FEATURE_64DD: return false;
        case FLASHCART_FEATURE_AUTO_CIC: return true;
        case FLASHCART_FEATURE_AUTO_REGION: return true;
        default: return false;
    }
}

static flashcart_err_t ed64_xseries_load_rom (char *rom_path, flashcart_progress_callback_t *progress) {
    FIL fil;
    UINT br;

    // Ensure SDRAM is mapped before writing the ROM image into it.
    ed64x_ll_enable_sdram();

    if (f_open(&fil, strip_fs_prefix(rom_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    fatfs_fix_file_size(&fil);

    size_t rom_size = f_size(&fil);

    if (rom_size > MiB(64)) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    size_t sdram_size = rom_size; // (MiB(64) - KiB(128));

    for (uint32_t offset = 0; offset < sdram_size; offset += LOAD_CHUNK_SIZE) {
        size_t block_size = MIN(sdram_size - offset, LOAD_CHUNK_SIZE);

        // SD -> RDRAM bounce buffer.
        if (f_read(&fil, load_bounce, block_size, &br) != FR_OK) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }
        if (br != block_size) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }

        // Software byte-swap for swapped ROM formats (what the HW path did).
        if (cart_card_byteswap) {
            for (size_t i = 0; (i + 1) < block_size; i += 2) {
                uint8_t tmp = load_bounce[i];
                load_bounce[i] = load_bounce[i + 1];
                load_bounce[i + 1] = tmp;
            }
        }

        // RDRAM -> cartridge SDRAM via PI DMA (addresses the full SDRAM without
        // the wrap seen on the SD->SDRAM direct-DMA path).
        uint32_t dma_len = (block_size + 1) & ~1u; // PI DMA requires even length
        data_cache_hit_writeback_invalidate(load_bounce, dma_len);
        dma_write(load_bounce, ROM_PI_ADDRESS + offset, dma_len);
        dma_wait();

        if (progress) {
            progress(f_tell(&fil) / (float) (f_size(&fil)));
        }
    }

    if (f_tell(&fil) != sdram_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

static flashcart_err_t ed64_xseries_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset) {
    FIL fil;
    UINT br;

    if (f_open(&fil, strip_fs_prefix(file_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    fatfs_fix_file_size(&fil);

    size_t file_size = f_size(&fil) - file_offset;

    if (file_size > (MiB(64) - rom_offset)) {
        f_close(&fil);
        return FLASHCART_ERR_ARGS;
    }

    if (f_lseek(&fil, file_offset) != FR_OK) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    // Same SD -> RDRAM -> SDRAM (PI DMA) staging as load_rom, to avoid the
    // SD->SDRAM direct-DMA address wrap on large transfers.
    for (uint32_t offset = 0; offset < file_size; offset += LOAD_CHUNK_SIZE) {
        size_t block_size = MIN(file_size - offset, LOAD_CHUNK_SIZE);

        if (f_read(&fil, load_bounce, block_size, &br) != FR_OK) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }
        if (br != block_size) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }

        if (cart_card_byteswap) {
            for (size_t i = 0; (i + 1) < block_size; i += 2) {
                uint8_t tmp = load_bounce[i];
                load_bounce[i] = load_bounce[i + 1];
                load_bounce[i + 1] = tmp;
            }
        }

        uint32_t dma_len = (block_size + 1) & ~1u;
        data_cache_hit_writeback_invalidate(load_bounce, dma_len);
        dma_write(load_bounce, ROM_PI_ADDRESS + rom_offset + offset, dma_len);
        dma_wait();
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

static flashcart_err_t ed64_xseries_load_save (char *save_path) {
    // FIXME: the savetype will be none.
    return FLASHCART_OK;
}

static flashcart_err_t ed64_xseries_set_save_type (flashcart_save_type_t save_type) {
    // FIXME: the savetype will be none.
    return FLASHCART_OK;
}

static flashcart_t flashcart_ed64_xseries = {
    .init = ed64_xseries_init,
    .deinit = ed64_xseries_deinit,
    .has_feature = ed64_xseries_has_feature,
    .get_firmware_version = ed64_xseries_get_firmware_version,
    .load_rom = ed64_xseries_load_rom,
    .load_file = ed64_xseries_load_file,
    .load_save = ed64_xseries_load_save,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .set_save_type = ed64_xseries_set_save_type,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
};


flashcart_t *ed64_xseries_get_flashcart (void) {
    return &flashcart_ed64_xseries;
}
