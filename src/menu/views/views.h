/**
 * @file views.h
 * @brief Declarations for all menu view modules and their display/init functions.
 * @ingroup menu
 *
 * This header provides prototypes for all view initialization and display routines used in the menu system.
 */

#ifndef VIEWS_H__
#define VIEWS_H__

#include "../ui_components.h"
#include "../menu_state.h"

/**
 * @addtogroup view
 * @{
 * @brief Menu view modules and their interface functions.
 */

/**
 * @brief Initialize the startup view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_startup_init(menu_t *menu);

/**
 * @brief Display the startup view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_startup_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the browser view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_browser_init(menu_t *menu);

/**
 * @brief Display the browser view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_browser_display(menu_t *menu, surface_t *display);

/**
 * @brief Navigate the browser directly into popup (shrink-wrapped) mode.
 *        The browser renders as a small rainbow popup over the grid background.
 *        Called from games_grid.c when "File Browser" is selected in the More menu.
 *
 * @param menu Pointer to the menu structure.
 */
void view_browser_open_popup(menu_t *menu);

/** @brief Arm a disc->base-ROM link-pick from the grid: opens the browser in link-pick mode,
 *         and the next ROM opened is stored as the disc's base (disclink) so the disc favorite
 *         (disc_fav_id) boots combined. */
void view_browser_request_link_pick(const char *disc_name, const char *disc_code, int disc_fav_id);

/**
 * @brief Open the file browser (popup) focused on a specific ROM with the
 *        canonical File-management submenu already showing.
 *
 * @param menu     Pointer to the menu structure.
 * @param rom_path Path of the ROM to operate on.
 */
void view_browser_open_file_management(menu_t *menu, path_t *rom_path);

/**
 * @brief Draw the games grid (background + tiles) without any overlay.
 *        Called by other views (load_rom, browser popup) that want the grid
 *        as their background instead of the full-screen dark gradient.
 *        Assumes rdpq_attach has already been called by the caller.
 *
 * @param menu    Pointer to the menu structure.
 * @param display Pointer to the display surface (unused but kept for symmetry).
 */
void view_games_grid_draw_background(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the file info view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_file_info_init(menu_t *menu);

/**
 * @brief Display the file info view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_file_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the system info view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_system_info_init(menu_t *menu);

/**
 * @brief Display the system info view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_system_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the image viewer view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_image_viewer_init(menu_t *menu);

/**
 * @brief Display the image viewer view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_image_viewer_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the text viewer view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_text_viewer_init(menu_t *menu);

/**
 * @brief Display the text viewer view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_text_viewer_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the music player view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_music_player_init(menu_t *menu);

/**
 * @brief Display the music player view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_music_player_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the credits view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_credits_init(menu_t *menu);

/**
 * @brief Display the credits view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_credits_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the settings view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_settings_init(menu_t *menu);

/**
 * @brief Display the settings view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_settings_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the RTC view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_rtc_init(menu_t *menu);

/**
 * @brief Display the RTC view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_rtc_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the Controller Pak FS manager view.
 * @param menu Pointer to the menu structure.
 */
void view_controller_pakfs_init(menu_t *menu);
/**
 * @brief Display the Controller Pak FS manager view.
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_controller_pakfs_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the Controller Pak dump info view.
 * @param menu Pointer to the menu structure.
 */
void view_controller_pak_dump_info_init(menu_t *menu);
/**
 * @brief Display the Controller Pak dump info view.
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_controller_pak_dump_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the Controller Pak note dump info view.
 * @param menu Pointer to the menu structure.
 */
void view_controller_pak_note_dump_info_init(menu_t *menu);
/**
 * @brief Display the Controller Pak note dump info view.
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_controller_pak_note_dump_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the flashcart info view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_flashcart_info_init(menu_t *menu);

/**
 * @brief Display the flashcart info view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_flashcart_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load ROM view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_rom_init(menu_t *menu);

/**
 * @brief Display the load ROM view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_rom_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load disk view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_disk_init(menu_t *menu);

/**
 * @brief Display the load disk view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_disk_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load emulator view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_emulator_init(menu_t *menu);

/**
 * @brief Display the load emulator view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_emulator_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the error view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_error_init(menu_t *menu);

/**
 * @brief Display the error view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_error_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the fault view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_fault_init(menu_t *menu);

/**
 * @brief Display the fault view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_fault_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the history view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_history_init(menu_t *menu);

/**
 * @brief Display the history view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_history_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the cheats editor view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_datel_code_editor_init(menu_t *menu);

/**
 * @brief Display the cheats editor view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_datel_code_editor_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the archive browser view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_extract_file_init(menu_t *menu);

/**
 * @brief Display the archive browser view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_extract_file_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the games grid view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_games_grid_init(menu_t *menu);

/**
 * @brief Display the games grid view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_games_grid_display(menu_t *menu, surface_t *display);

/**
 * @brief Drive the games-grid screensaver marquee as an animated background for another view.
 *        begin (lay out + reclaim RAM), frame (step + draw to the attached surface), end
 *        (restore). The caller MUST call end on exit. Used by the credits popup.
 */
void view_grid_screensaver_begin(menu_t *menu);
void view_grid_screensaver_frame(menu_t *menu);
void view_grid_screensaver_end(menu_t *menu);

/** @brief Free all grid cover art (reclaim a large contiguous block, e.g. before a font reload). */
void view_grid_release_boxart(void);

/**
 * @brief Initialize the link-disc picker (pick a 64DD disc to pair with a ROM).
 *
 * @param menu Pointer to the menu structure.
 */
void view_link_disc_init(menu_t *menu);

/**
 * @brief Put the link picker into CART mode for an unlinked expansion disk, then switch to
 *        MENU_MODE_LINK_DISC. Lists favourite cartridges of the same game family to pair.
 *
 * @param menu Pointer to the menu structure.
 * @param fav_i Favourite index of the expansion disk needing a cartridge.
 */
void view_link_disc_pick_cart(menu_t *menu, int fav_i);

/**
 * @brief Display the link-disc picker view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_link_disc_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the ROM-boot countdown view (userland power-on preview of the chosen ROM).
 *        Expects menu->load.rom_path already set (by startup) to the boot ROM.
 *
 * @param menu Pointer to the menu structure.
 */
void view_rom_boot_init(menu_t *menu);

/**
 * @brief Display the ROM-boot countdown view: shows the ROM's Load art (or the cart placeholder)
 *        with a countdown. B cancels to the grid; START boots now; on timeout it launches exactly
 *        as the file browser would (hands off to MENU_MODE_LOAD_ROM).
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_rom_boot_display(menu_t *menu, surface_t *display);

/**
 * @brief Show an error message in the menu.
 *
 * @param menu Pointer to the menu structure.
 * @param error_message Error message to be displayed.
 */
void menu_show_error(menu_t *menu, char *error_message);

/** @} */ /* view */

#endif // VIEWS_H__
