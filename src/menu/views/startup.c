#include "utils/fs.h"
#include "views.h"


static void draw (menu_t *menu, surface_t *d) {
    /* Hold the boot splash through the startup frame instead of clearing to black,
       so there's no splash -> black -> grid flash between menu_init's early splash
       and the grid's own splash. */
    rdpq_attach(d, NULL);
    if (menu->settings.splash_enabled) {
        ui_components_background_draw_splash(menu->settings.custom_splash_enabled);
    } else {
        ui_components_background_draw();   /* match menu_init: no flash when Boot Splash is off */
    }
    rdpq_detach_show();
}


void view_startup_init (menu_t *menu) {
    /* ROM boot (userland power-on convenience). Hold START at power-on to DISABLE it -- the
       escape hatch should a chosen ROM ever boot-loop the console (the per-boot B: Cancel in
       the countdown view is the everyday escape; this one persists the disable). */
    if (menu->settings.rom_boot_enabled) {
        joypad_poll();
        bool start_held = false;
        JOYPAD_PORT_FOREACH (port) {
            if (joypad_get_buttons_held(port).start) start_held = true;
        }
        if (start_held) {
            menu->settings.rom_boot_enabled = false;
            settings_save(&menu->settings);
        }
    }

    if (menu->settings.rom_boot_enabled &&
        menu->settings.rom_boot_filename && menu->settings.rom_boot_filename[0]) {
        path_t *dir = path_init(menu->storage_prefix, menu->settings.rom_boot_path);
        path_t *rp  = path_clone_push(dir, menu->settings.rom_boot_filename);
        path_free(dir);
        if (file_exists(path_get(rp))) {
            menu->load.rom_path = rp;                 /* consumed by the ROM-boot view -> LOAD_ROM */
            menu->next_mode = MENU_MODE_ROM_BOOT;
            return;
        }
        path_free(rp);                                /* configured ROM is missing -> fall through */
    }

    if (menu->settings.first_run) {
        menu->settings.first_run = false;
        settings_save(&menu->settings);
        menu->next_mode = MENU_MODE_CREDITS;
    }
    else {
        menu->next_mode = MENU_MODE_GAMES_GRID;
    }
}

void view_startup_display (menu_t *menu, surface_t *display) {
    draw(menu, display);
}
