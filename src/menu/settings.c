#include <libdragon.h>
#include "ini_parser.h"

#include "settings.h"
#include "utils/fs.h"


static char *settings_path = NULL;


static settings_t init = {
    .schema_revision = 1,
    .first_run = true,
    .pal60_enabled = false,
    .force_progressive_scan = false,
    .show_protected_entries = false,
    .default_directory = "/",
    .disc_folder = "",
    .use_saves_folder = true,
    .show_saves_folder = false,
    .show_save_files = false,
    .show_cheat_files = false,
    .show_rom_configuration_files = false,
    .soundfx_enabled = false,
    .bgm_enabled = false,
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    .rom_autoload_enabled = false,
    .rom_autoload_path = "",
    .rom_autoload_filename = "",
    .loading_progress_bar_enabled = true,
#else
    .rom_fast_reboot_enabled = false,
#endif    
    /* Beta feature flags (should always init to default) */
    .show_browser_file_extensions = true,
    .show_browser_rom_tags = true,
    .rumble_enabled = false,
    .grid_square_tiles = false,
    .grid_large_tiles = false,
    .grid_show_captions_favorites = false,
    .splash_enabled = false,
    .background_image_enabled = false,
    .custom_splash_enabled = false,
    .use_custom_files = false,
    .always_sort_az = false,
    .use_legacy_font = false,
    .show_file_size    = false,
    .screensaver_mode  = SCREENSAVER_ON,
    .screensaver_favorites_only = false,
    .screensaver_timeout_sec    = 180,
    .image_view_grid   = GRID_IMAGE_BOX_FRONT,
    .image_view_inspect= GRID_IMAGE_BOX_BACK,
    .image_view_load   = GRID_IMAGE_CART_FRONT,
    .image_region_default = 0,   /* ROM_PRESENTS_AS_AUTO */
    .rom_boot_enabled  = false,
    .rom_boot_path     = "",
    .rom_boot_filename = "",
    .rom_boot_countdown_sec = 5,
};


void settings_init (char *path) {
    if (settings_path) {
        free(settings_path);
    }
    settings_path = strdup(path);
}

void settings_load (settings_t *settings) {
    if (!file_exists(settings_path)) {
        settings_save(&init);
    }

    ini_t *ini = ini_try_load(settings_path);

    settings->schema_revision = ini_get_int(ini, "menu", "schema_revision", init.schema_revision);
    settings->first_run = ini_get_bool(ini, "menu", "first_run", init.first_run);
    settings->pal60_enabled = ini_get_bool(ini, "menu", "pal60", init.pal60_enabled);
    settings->force_progressive_scan = ini_get_bool(ini, "menu", "force_progressive_scan", init.force_progressive_scan);
    settings->show_protected_entries = ini_get_bool(ini, "menu", "show_protected_entries", init.show_protected_entries);
    free(settings->default_directory);
    settings->default_directory = strdup(ini_get_string(ini, "menu", "default_directory", init.default_directory));
    free(settings->disc_folder);
    settings->disc_folder = strdup(ini_get_string(ini, "menu", "disc_folder", init.disc_folder));
    settings->use_saves_folder = ini_get_bool(ini, "menu", "use_saves_folder", init.use_saves_folder);
    settings->show_saves_folder = ini_get_bool(ini, "menu", "show_saves_folder", init.show_saves_folder);
    settings->show_save_files = ini_get_bool(ini, "menu", "show_save_files", init.show_save_files);
    settings->show_cheat_files = ini_get_bool(ini, "menu", "show_cheat_files", init.show_cheat_files);
    settings->show_rom_configuration_files = ini_get_bool(ini, "menu", "show_rom_configuration_files", init.show_rom_configuration_files);
    settings->soundfx_enabled = ini_get_bool(ini, "menu", "soundfx_enabled", init.soundfx_enabled);
    settings->bgm_enabled = ini_get_bool(ini, "menu", "bgm_enabled", init.bgm_enabled);

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    settings->rom_autoload_enabled = ini_get_bool(ini, "menu", "autoload_rom_enabled", init.rom_autoload_enabled);
    free(settings->rom_autoload_path);
    settings->rom_autoload_path = strdup(ini_get_string(ini, "autoload", "rom_path", init.rom_autoload_path));
    free(settings->rom_autoload_filename);
    settings->rom_autoload_filename = strdup(ini_get_string(ini, "autoload", "rom_filename", init.rom_autoload_filename));
    settings->loading_progress_bar_enabled = ini_get_bool(ini, "menu", "loading_progress_bar_enabled", init.loading_progress_bar_enabled);
#else
    settings->rom_fast_reboot_enabled = ini_get_bool(ini, "menu", "reboot_rom_enabled", init.rom_fast_reboot_enabled);
#endif
    /* Beta feature flags, they might not be in the file */
    settings->show_browser_file_extensions = ini_get_bool(ini, "menu", "show_browser_file_extensions", init.show_browser_file_extensions);
    settings->show_browser_rom_tags = ini_get_bool(ini, "menu", "show_browser_rom_tags", init.show_browser_rom_tags);
    settings->rumble_enabled = ini_get_bool(ini, "menu_beta_flag", "rumble_enabled", init.rumble_enabled);
    settings->grid_square_tiles = ini_get_bool(ini, "menu", "grid_square_tiles", init.grid_square_tiles);
    settings->grid_large_tiles  = ini_get_bool(ini, "menu", "grid_large_tiles", init.grid_large_tiles);
    settings->grid_show_captions_favorites = ini_get_bool(ini, "menu", "grid_show_captions_favorites", init.grid_show_captions_favorites);
    settings->splash_enabled    = ini_get_bool(ini, "menu", "splash_enabled", init.splash_enabled);
    settings->background_image_enabled = ini_get_bool(ini, "menu", "background_image_enabled", init.background_image_enabled);
    settings->custom_splash_enabled = ini_get_bool(ini, "menu", "custom_splash_enabled", init.custom_splash_enabled);
    settings->use_custom_files = ini_get_bool(ini, "menu", "use_custom_files", init.use_custom_files);
    settings->always_sort_az = ini_get_bool(ini, "menu", "always_sort_az", init.always_sort_az);
    settings->use_legacy_font = ini_get_bool(ini, "menu", "use_legacy_font", init.use_legacy_font);
    settings->show_file_size    = ini_get_bool(ini, "menu", "show_file_size", init.show_file_size);
    settings->screensaver_mode  = (screensaver_mode_t)ini_get_int(ini, "menu", "screensaver_mode", (int)init.screensaver_mode);
    settings->screensaver_favorites_only = ini_get_bool(ini, "menu", "screensaver_favorites_only", init.screensaver_favorites_only);
    settings->screensaver_timeout_sec    = ini_get_int(ini, "menu", "screensaver_timeout_sec", init.screensaver_timeout_sec);
    settings->image_view_grid   = (grid_image_view_t)ini_get_int(ini, "menu", "image_view_grid",    (int)init.image_view_grid);
    settings->image_view_inspect= (grid_image_view_t)ini_get_int(ini, "menu", "image_view_inspect", (int)init.image_view_inspect);
    settings->image_view_load   = (grid_image_view_t)ini_get_int(ini, "menu", "image_view_load",    (int)init.image_view_load);
    settings->image_region_default = ini_get_int(ini, "menu", "image_region_default", init.image_region_default);
    settings->rom_boot_enabled  = ini_get_bool(ini, "menu", "rom_boot_enabled", init.rom_boot_enabled);
    free(settings->rom_boot_path);
    settings->rom_boot_path     = strdup(ini_get_string(ini, "rom_boot", "path", init.rom_boot_path));
    free(settings->rom_boot_filename);
    settings->rom_boot_filename = strdup(ini_get_string(ini, "rom_boot", "filename", init.rom_boot_filename));
    settings->rom_boot_countdown_sec = ini_get_int(ini, "rom_boot", "countdown_sec", init.rom_boot_countdown_sec);

    ini_free(ini);
}

void settings_save (settings_t *settings) {
    ini_t *ini = ini_create();

    ini_set_int(ini, "menu", "schema_revision", settings->schema_revision);
    ini_set_bool(ini, "menu", "first_run", settings->first_run);
    ini_set_bool(ini, "menu", "pal60", settings->pal60_enabled);
    ini_set_bool(ini, "menu", "force_progressive_scan", settings->force_progressive_scan);
    ini_set_bool(ini, "menu", "show_protected_entries", settings->show_protected_entries);
    ini_set_string(ini, "menu", "default_directory", settings->default_directory);
    ini_set_string(ini, "menu", "disc_folder", settings->disc_folder);
    ini_set_bool(ini, "menu", "use_saves_folder", settings->use_saves_folder);
    ini_set_bool(ini, "menu", "show_saves_folder", settings->show_saves_folder);
    ini_set_bool(ini, "menu", "show_save_files", settings->show_save_files);
    ini_set_bool(ini, "menu", "show_cheat_files", settings->show_cheat_files);
    ini_set_bool(ini, "menu", "show_rom_configuration_files", settings->show_rom_configuration_files);
    ini_set_bool(ini, "menu", "soundfx_enabled", settings->soundfx_enabled);
    ini_set_bool(ini, "menu", "bgm_enabled", settings->bgm_enabled);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    ini_set_bool(ini, "menu", "autoload_rom_enabled", settings->rom_autoload_enabled);
    ini_set_string(ini, "autoload", "rom_path", settings->rom_autoload_path);
    ini_set_string(ini, "autoload", "rom_filename", settings->rom_autoload_filename);
    ini_set_bool(ini, "menu", "loading_progress_bar_enabled", settings->loading_progress_bar_enabled);
#else
    ini_set_bool(ini, "menu", "reboot_rom_enabled", settings->rom_fast_reboot_enabled);
#endif

    ini_set_bool(ini, "menu", "grid_square_tiles",   settings->grid_square_tiles);
    ini_set_bool(ini, "menu", "grid_large_tiles",    settings->grid_large_tiles);
    ini_set_bool(ini, "menu", "grid_show_captions_favorites", settings->grid_show_captions_favorites);
    ini_set_bool(ini, "menu", "splash_enabled",      settings->splash_enabled);
    ini_set_bool(ini, "menu", "background_image_enabled", settings->background_image_enabled);
    ini_set_bool(ini, "menu", "custom_splash_enabled", settings->custom_splash_enabled);
    ini_set_bool(ini, "menu", "use_custom_files", settings->use_custom_files);
    ini_set_bool(ini, "menu", "always_sort_az", settings->always_sort_az);
    ini_set_bool(ini, "menu", "use_legacy_font", settings->use_legacy_font);
    ini_set_bool(ini, "menu", "show_file_size",      settings->show_file_size);
    ini_set_int(ini,  "menu", "screensaver_mode",    (int)settings->screensaver_mode);
    ini_set_bool(ini, "menu", "screensaver_favorites_only", settings->screensaver_favorites_only);
    ini_set_int(ini,  "menu", "screensaver_timeout_sec",    settings->screensaver_timeout_sec);
    ini_set_int(ini,  "menu", "image_view_grid",     (int)settings->image_view_grid);
    ini_set_int(ini,  "menu", "image_view_inspect",  (int)settings->image_view_inspect);
    ini_set_int(ini,  "menu", "image_view_load",     (int)settings->image_view_load);
    ini_set_int(ini,  "menu", "image_region_default", settings->image_region_default);
    ini_set_bool(ini, "menu", "rom_boot_enabled", settings->rom_boot_enabled);
    ini_set_string(ini, "rom_boot", "path", settings->rom_boot_path);
    ini_set_string(ini, "rom_boot", "filename", settings->rom_boot_filename);
    ini_set_int(ini, "rom_boot", "countdown_sec", settings->rom_boot_countdown_sec);

    /* Beta feature flags, they should not save until production ready! */
    // ini_set_bool(ini, "menu", "show_browser_file_extensions", settings->show_browser_file_extensions);
    // ini_set_bool(ini, "menu", "show_browser_rom_tags", settings->show_browser_rom_tags);
    // ini_set_bool(ini, "menu_beta_flag", "rumble_enabled", settings->rumble_enabled);

    if (!ini_save(ini, settings_path)) {
        debugf("[SETTINGS] Failed to save settings to %s\n", settings_path);
    }

    ini_free(ini);
}

void settings_reset_to_defaults() {
    remove(settings_path);
}