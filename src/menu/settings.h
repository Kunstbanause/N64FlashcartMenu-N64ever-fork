/**
 * @file settings.h
 * @brief Menu Settings
 * @ingroup menu 
 */

#ifndef SETTINGS_H__
#define SETTINGS_H__


/**
 * @brief Which image type is shown in Grid, Inspect popup, and Load screen.
 */
typedef enum {
    GRID_IMAGE_BOX_FRONT  = 0, /**< Box-art front cover */
    GRID_IMAGE_BOX_BACK   = 1, /**< Box-art back cover  */
    GRID_IMAGE_BOX_3D     = 2, /**< 3D box render       */
    GRID_IMAGE_CART_FRONT = 3, /**< Cartridge front     */
    GRID_IMAGE_CART_3D    = 4, /**< 3D cartridge render */
    GRID_IMAGE_LOGO       = 5, /**< Game logo           */
    GRID_IMAGE_COUNT      = 6, /**< Number of view types */
} grid_image_view_t;

/** @brief Idle screensaver on the games grid (random-art marquee). */
typedef enum {
    SCREENSAVER_OFF   = 0, /**< Never engage a screensaver */
    SCREENSAVER_ON    = 1, /**< Random cover-art marquee after idle / L+R hold */
    SCREENSAVER_COUNT = 2, /**< Number of screensaver states */
} screensaver_mode_t;

/** @brief Settings Structure */
typedef struct {
    /** @brief Settings version */
    int schema_revision;

    /** @brief First run of the menu */
    bool first_run;

    /** @brief Use 60 Hz refresh rate on a PAL console */
    bool pal60_enabled;

    /** @brief Direct the VI to force progressive scan output at 240p. Meant for TVs and other devices which struggle to display interlaced video. */
    bool force_progressive_scan;

    /** @brief Show files/directories that are filtered in the browser */
    bool show_protected_entries;

    /** @brief Default directory to navigate to when menu loads */
    char *default_directory;

    /** @brief Folder holding 64DD discs, for the grid "Link disc" picker ("" = unset) */
    char *disc_folder;

    /** @brief Put saves into separate directory */
    bool use_saves_folder;

    /** @brief Show saves folder in file browser */ 
    bool show_saves_folder;

    /** @brief Show save files in file browser */
    bool show_save_files;

    /** @brief Show cheat files in file browser */
    bool show_cheat_files;

    /** @brief Show rom configuration files in file browser */
    bool show_rom_configuration_files;

    /** @brief Show rom file extensions in browser */    
    bool show_browser_file_extensions;

    /** @brief Show rom tags in browser */  
    bool show_browser_rom_tags;

    /** @brief Enable Background music */
    bool bgm_enabled;

    /** @brief Enable Sound effects within the menu */
    bool soundfx_enabled;

    /** @brief Enable rumble feedback within the menu */
    bool rumble_enabled;

    /** @brief Show all grid icons as square tiles with letterbox/pillarbox */
    bool grid_square_tiles;

    /** @brief Larger grid tiles (bigger covers, ~one less row). Off = Small (default). */
    bool grid_large_tiles;

    /** @brief Show background image as a splash screen on boot until grid boxart loads */
    bool splash_enabled;

    /** @brief Show the user background image behind the Files browser */
    bool background_image_enabled;

    /** @brief Use the user PNG (menu/n64ever/splash.png) as the boot splash */
    bool custom_splash_enabled;

    /** @brief Honor per-game custom override files in gameconfigs/ (custom art PNGs
     *  and <CODE>.meta.ini metadata). Off (default) = faster: skip the per-game SD
     *  probes and use only baked/DB data. */
    bool use_custom_files;

    /** @brief Keep the favorites grid sorted A-Z automatically (re-sorts on each
     *  grid entry). Recommended only with use_custom_files off (DB-only sort is
     *  instant). */
    bool always_sort_az;

    /** @brief Use the classic Firple-Bold font instead of the modern PixelMplus12
     *  pixel font. Off (default) = modern font; on = classic, for compatibility. */
    bool use_legacy_font;

    /** @brief Show file size in the Files browser */
    bool show_file_size;

    /** @brief Idle screensaver mode on the games grid (Off / Drift / Spotlight). */
    screensaver_mode_t screensaver_mode;

    /** @brief Idle screensaver pulls only from favorited games (not the whole library). */
    bool screensaver_favorites_only;

    /** @brief Seconds of idle before the screensaver engages (30..3600). */
    int screensaver_timeout_sec;

    /** @brief Which image type is shown on Grid tiles */
    grid_image_view_t image_view_grid;
    /** @brief Which image type is shown in the Inspect popup */
    grid_image_view_t image_view_inspect;
    /** @brief Which image type is shown on the Load/boot screen */
    grid_image_view_t image_view_load;

    /** @brief Global default region for box art when a game has no per-game override
     *  (0 = Auto/ROM region; otherwise a rom_presents_as_t value). */
    int image_region_default;

    /** @brief ROM boot: when enabled, power-on shows a brief countdown preview of the chosen
     *  ROM (its Load art or the placeholder) and boots it unless the user presses B to cancel.
     *  Entirely userland -- the menu runs the countdown and then launches like the file browser
     *  would; no firmware autoboot / reset-button boot mode is touched. */
    bool rom_boot_enabled;
    char *rom_boot_path;       /**< directory of the boot ROM (relative to the storage prefix) */
    char *rom_boot_filename;   /**< filename of the boot ROM */
    int rom_boot_countdown_sec; /**< ROM-boot countdown length in seconds (1..15; default 5) */

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on power and reset button */
    bool rom_autoload_enabled;

    /** @brief A path to the autoloaded ROM */
    char *rom_autoload_path;

    /** @brief A filename of the autoloaded ROM */
    char *rom_autoload_filename;
    
    /** @brief Show progress bar when loading a ROM */
    bool loading_progress_bar_enabled;
#else
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on reset button */
    bool rom_fast_reboot_enabled;
#endif

} settings_t;


/** @brief Init settings path */
void settings_init (char *path);
/** @brief The settings to load */
void settings_load (settings_t *settings);
/** @brief The settings to save */
void settings_save (settings_t *settings);
/** @brief Reset settings to defaults */
void settings_reset_to_defaults();

#endif
