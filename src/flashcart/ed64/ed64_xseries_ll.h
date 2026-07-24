/**
 * @file ed64_xseries_ll.h
 * @brief ed64x flashcart low level access
 * @ingroup flashcart
 */

#ifndef FLASHCART_ED64_XSERIES_LL_H__
#define FLASHCART_ED64_XSERIES_LL_H__


/**
 * @addtogroup ed64_xseries_ll
 * @{
 */

/**
 * @brief Ensure the cart SDRAM is enabled and mapped into the ROM address space.
 *
 * The EverDrive-64 X-series presents its 64 MiB SDRAM at the cartridge ROM
 * address (`0x10000000` / `0xB0000000`). ROM data written by the menu only
 * takes effect (and is only visible to the boot code) while SDRAM is mapped.
 * This is idempotent and safe to call repeatedly.
 */
void ed64x_ll_enable_sdram (void);

/**
 * @brief Assert SDRAM mapping and lock the register window before booting.
 *
 * Mirrors what a known-good EverDrive OS does as the final step before handing
 * control to the ROM. Must only be called once all SD / register access is
 * finished (from the flashcart deinit path, right before boot()).
 */
void ed64x_ll_lock_regs_for_boot (void);

/** @} */ /* ed64_xseries_ll */


#endif
