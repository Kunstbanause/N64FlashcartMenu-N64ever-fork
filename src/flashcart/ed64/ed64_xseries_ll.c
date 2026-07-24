#include <stdint.h>

#include <libdragon.h>

#include "ed64_xseries_ll.h"


/**
 * EverDrive-64 X-series FPGA register map.
 *
 * Addresses and bit definitions are taken from the EverDrive-64 X-series
 * support in libcart / libdragon (`libdragon/src/libcart/cart.c`) and the
 * N64brew wiki. Registers live in the cartridge PI address space and are
 * accessed with the libdragon `io_read()` / `io_write()` helpers (which expect
 * a raw PI physical address). The register window must be unlocked by writing
 * #EDX_KEY to #EDX_KEY_REG before any register access.
 */
#define EDX_BASE_REG        (0x1F800000)

#define EDX_SYS_CFG_REG     (EDX_BASE_REG + 0x8000)
#define EDX_KEY_REG         (EDX_BASE_REG + 0x8004)

/* EDX_SYS_CFG_REG values */
#define EDX_CFG_SDRAM_ON    (0x0000)
#define EDX_CFG_SDRAM_OFF   (0x0001)

/* Register unlock key */
#define EDX_KEY             (0xAA55)


void ed64x_ll_enable_sdram (void) {
    // libdragon's SD driver (libcart) keeps the register window unlocked for
    // the whole session, so we only ever (re)assert the key here, never lock.
    io_write(EDX_KEY_REG, EDX_KEY);
    io_write(EDX_SYS_CFG_REG, EDX_CFG_SDRAM_ON);
}

void ed64x_ll_lock_regs_for_boot (void) {
    // Final step before handing off to the ROM. This must only be called once
    // all SD / register access is finished (i.e. from flashcart deinit, right
    // before boot()), otherwise it would break libcart's SD access.
    //
    // NOTE: switching the FPGA into "game mode" (EDX_BCFG_GAMEMOD/CICLOCK) was
    // tried here to make the cart emulate the loaded game's CIC, but it does
    // not change the boot result: only CIC-6102 titles boot via this software
    // boot path. Presenting other CICs appears to require a hardware boot/reset
    // sequence that is not implemented here, so we keep this minimal.
    io_write(EDX_KEY_REG, EDX_KEY);
    io_write(EDX_SYS_CFG_REG, EDX_CFG_SDRAM_ON);
    io_write(EDX_KEY_REG, 0);
}
