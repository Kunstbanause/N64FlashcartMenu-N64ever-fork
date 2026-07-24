#include <stdbool.h>
#include <stdio.h>
#include <libdragon.h>
#include "../sound.h"
#include "../settings.h"
#include "../ui_components/constants.h"
#include "../fonts.h"
#include "views.h"

/* ---------- helpers ---------- */

static const char *on_off(bool v) { return v ? "On" : "Off"; }

static const char *screensaver_name(screensaver_mode_t m) {
    return (m == SCREENSAVER_OFF) ? "Off" : "On";
}

/* Allowed idle-timeout values for the screensaver (seconds). */
static const int ss_timer_values[] = { 30, 60, 120, 180, 240, 300, 600, 1200, 1800, 3600 };
#define SS_TIMER_COUNT ((int)(sizeof(ss_timer_values) / sizeof(ss_timer_values[0])))

static const char *fmt_ss_timer(int sec) {
    static char buf[8];
    if (sec < 60) snprintf(buf, sizeof(buf), "%ds", sec);
    else          snprintf(buf, sizeof(buf), "%dm", sec / 60);
    return buf;
}

/* ---------- dynamic menu labels ---------- */

static char lbl_hidden[30];
static char lbl_soundfx[24];
static char lbl_bgm[24];
static char lbl_splash[20];
static char lbl_bg_image[26];
static char lbl_custom_files[30];
static char lbl_saves_use[24];
static char lbl_saves_show[26];
static char lbl_save_files[22];
static char lbl_cheat_files[22];
static char lbl_pal60[20];
static char lbl_font[20];
static char lbl_screensaver[28];
static char lbl_ss_fav[36];
static char lbl_ss_timer[30];
static char lbl_boot_opt[26];
#ifdef BETA_SETTINGS
static char lbl_rom_ext[26];
static char lbl_rom_tags[20];
static char lbl_rumble[22];
#endif

static void update_settings_labels(menu_t *menu) {
    snprintf(lbl_hidden,      sizeof(lbl_hidden),      "Show Hidden: %s",      on_off(menu->settings.show_protected_entries));
    snprintf(lbl_soundfx,     sizeof(lbl_soundfx),     "Sound Effects: %s",    on_off(menu->settings.soundfx_enabled));
    snprintf(lbl_bgm,         sizeof(lbl_bgm),         "Background Music: %s", on_off(menu->settings.bgm_enabled));
    snprintf(lbl_splash,      sizeof(lbl_splash),      "Boot Splash: %s",      on_off(menu->settings.splash_enabled));
    snprintf(lbl_bg_image,    sizeof(lbl_bg_image),    "Custom Splash: %s", on_off(menu->settings.custom_splash_enabled));
    snprintf(lbl_custom_files,sizeof(lbl_custom_files), "Use Custom Files: %s", on_off(menu->settings.use_custom_files));
    snprintf(lbl_saves_use,   sizeof(lbl_saves_use),   "Use Saves Folder: %s", on_off(menu->settings.use_saves_folder));
    snprintf(lbl_saves_show,  sizeof(lbl_saves_show),  "Show Saves Folder: %s",on_off(menu->settings.show_saves_folder));
    snprintf(lbl_save_files,  sizeof(lbl_save_files),  "Show Save Files: %s",  on_off(menu->settings.show_save_files));
    snprintf(lbl_cheat_files, sizeof(lbl_cheat_files), "Show Cheat Files: %s", on_off(menu->settings.show_cheat_files));
    snprintf(lbl_pal60,       sizeof(lbl_pal60),       "PAL60 Mode: %s",       on_off(menu->settings.pal60_enabled));
    snprintf(lbl_font,        sizeof(lbl_font),        "Font: %s",            menu->settings.use_legacy_font ? "Classic" : "Pixel");
    snprintf(lbl_screensaver, sizeof(lbl_screensaver), "Screensaver: %s",     screensaver_name(menu->settings.screensaver_mode));
    snprintf(lbl_ss_fav,      sizeof(lbl_ss_fav),      "Favorite Screensaver: %s", on_off(menu->settings.screensaver_favorites_only));
    snprintf(lbl_ss_timer,    sizeof(lbl_ss_timer),    "Screensaver Timer: %s", fmt_ss_timer(menu->settings.screensaver_timeout_sec));
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    snprintf(lbl_boot_opt, sizeof(lbl_boot_opt), "ROM Loading Bar: %s",  on_off(menu->settings.loading_progress_bar_enabled));
#else
    snprintf(lbl_boot_opt, sizeof(lbl_boot_opt), "Fast Reboot ROM: %s",  on_off(menu->settings.rom_fast_reboot_enabled));
#endif
#ifdef BETA_SETTINGS
    snprintf(lbl_rom_ext,  sizeof(lbl_rom_ext),  "Hide ROM Extension: %s", on_off(menu->settings.show_browser_file_extensions));
    snprintf(lbl_rom_tags, sizeof(lbl_rom_tags), "Hide ROM Tags: %s",      on_off(menu->settings.show_browser_rom_tags));
    snprintf(lbl_rumble,   sizeof(lbl_rumble),   "Rumble Feedback: %s",    on_off(menu->settings.rumble_enabled));
#endif
}

/* ---------- toggle actions ---------- */

static bool settings_action_fired = false;
static bool settings_was_open     = false;
static int  settings_saved_row    = 0;

static void toggle_hidden(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_protected_entries = !menu->settings.show_protected_entries;
    settings_save(&menu->settings);
    menu->browser.reload = true;
    update_settings_labels(menu);
}
static void toggle_soundfx(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.soundfx_enabled = !menu->settings.soundfx_enabled;
    sound_use_sfx(menu->settings.soundfx_enabled);
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_bgm(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.bgm_enabled = !menu->settings.bgm_enabled;
    sound_set_grid_sfx_enabled(menu->settings.bgm_enabled);
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_splash(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.splash_enabled = !menu->settings.splash_enabled;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_bg_image(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.custom_splash_enabled = !menu->settings.custom_splash_enabled;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_custom_files(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.use_custom_files = !menu->settings.use_custom_files;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_font(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.use_legacy_font = !menu->settings.use_legacy_font;
    settings_save(&menu->settings);
    /* Free the grid cover cache first: the Firple font is a 486KB contiguous alloc that a
       full+fragmented cache (cap 80) can't make room for -> asset_load asserts ("Out of memory"
       toggling the font). The grid isn't visible from Settings and reloads covers on return. */
    view_grid_release_boxart();
    fonts_reload(menu->settings.use_legacy_font);   /* apply live */
    update_settings_labels(menu);
}
static void cycle_screensaver(menu_t *menu, void *arg) {
    (void)arg;
    settings_action_fired = true;
    int m = (int)menu->settings.screensaver_mode + 1;
    if (m >= SCREENSAVER_COUNT) m = SCREENSAVER_OFF;
    menu->settings.screensaver_mode = (screensaver_mode_t)m;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_ss_fav(menu_t *menu, void *arg) {
    (void)arg;
    settings_action_fired = true;
    menu->settings.screensaver_favorites_only = !menu->settings.screensaver_favorites_only;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void cycle_ss_timer(menu_t *menu, void *arg) {
    (void)arg;
    settings_action_fired = true;
    int idx = 0;
    for (int i = 0; i < SS_TIMER_COUNT; i++) {
        if (ss_timer_values[i] == menu->settings.screensaver_timeout_sec) { idx = i; break; }
    }
    idx = (idx + 1) % SS_TIMER_COUNT;
    menu->settings.screensaver_timeout_sec = ss_timer_values[idx];
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_saves_use(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.use_saves_folder = !menu->settings.use_saves_folder;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_saves_show(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_saves_folder = !menu->settings.show_saves_folder;
    settings_save(&menu->settings);
    menu->browser.reload = true;
    update_settings_labels(menu);
}
static void toggle_save_files(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_save_files = !menu->settings.show_save_files;
    settings_save(&menu->settings);
    menu->browser.reload = true;
    update_settings_labels(menu);
}
static void toggle_cheat_files(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_cheat_files = !menu->settings.show_cheat_files;
    settings_save(&menu->settings);
    menu->browser.reload = true;
    update_settings_labels(menu);
}

/* PAL60 — handled separately: confirm countdown popup */
static bool     pal60_confirm_active = false;
static uint32_t pal60_deadline_ms    = 0;   /* wall-clock ms when the auto-revert fires (fps-independent) */
static bool     pal60_prev_value     = false;
static int  pal60_choice         = 0;     /* 0 = Keep, 1 = Revert */

static void pal60_apply(bool enable) {
    if (get_tv_type() == TV_PAL) {
        vi_set_timing_preset(enable ? &VI_TIMING_PAL60 : &VI_TIMING_PAL);
    }
}

static void toggle_pal60(menu_t *menu, void *arg) {
    bool next = !menu->settings.pal60_enabled;
    if (get_tv_type() != TV_PAL) {
        /* Non-PAL console: cycle the value but no timing change; reopen normally. */
        settings_action_fired = true;
        menu->settings.pal60_enabled = next;
        settings_save(&menu->settings);
        update_settings_labels(menu);
        return;
    }
    pal60_prev_value = menu->settings.pal60_enabled;
    pal60_apply(next);
    menu->settings.pal60_enabled = next;
    settings_save(&menu->settings);
    update_settings_labels(menu);
    pal60_confirm_active = true;
    pal60_deadline_ms    = (uint32_t)get_ticks_ms() + 10000;   /* 10 s to confirm, wall-clock */
    pal60_choice         = 0;
    /* Do NOT set settings_action_fired — the confirm popup reopens the menu. */
}

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void toggle_loading_bar(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.loading_progress_bar_enabled = !menu->settings.loading_progress_bar_enabled;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
#else
static void toggle_fast_reboot(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.rom_fast_reboot_enabled = !menu->settings.rom_fast_reboot_enabled;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
#endif

#ifdef BETA_SETTINGS
static void toggle_rom_ext(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_browser_file_extensions = !menu->settings.show_browser_file_extensions;
    settings_save(&menu->settings);
    menu->browser.reload = true;
    update_settings_labels(menu);
}
static void toggle_rom_tags(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.show_browser_rom_tags = !menu->settings.show_browser_rom_tags;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
static void toggle_rumble(menu_t *menu, void *arg) {
    settings_action_fired = true;
    menu->settings.rumble_enabled = !menu->settings.rumble_enabled;
    settings_save(&menu->settings);
    update_settings_labels(menu);
}
#endif

/* ---------- context menu ---------- */

static bool show_message_reset_settings = false;
static bool show_message_clear_favorites = false;
static bool show_message_clear_history = false;

static void gs_reset (menu_t *menu, void *arg) {
    (void)menu; (void)arg;
    show_message_reset_settings = true;
}

static void gs_clear_favorites (menu_t *menu, void *arg) {
    (void)menu; (void)arg;
    show_message_clear_favorites = true;
}

static void gs_clear_history (menu_t *menu, void *arg) {
    (void)menu; (void)arg;
    show_message_clear_history = true;
}

static component_context_menu_t options_context_menu = { .list = {
    { .text = lbl_hidden,      .action = toggle_hidden      },
    { .text = lbl_soundfx,     .action = toggle_soundfx     },
    { .text = lbl_bgm,         .action = toggle_bgm         },
    { .text = lbl_splash,      .action = toggle_splash      },
    { .text = lbl_bg_image,    .action = toggle_bg_image    },
    { .text = lbl_custom_files, .action = toggle_custom_files },
    { .text = lbl_saves_use,   .action = toggle_saves_use   },
    { .text = lbl_saves_show,  .action = toggle_saves_show  },
    { .text = lbl_save_files,  .action = toggle_save_files  },
    { .text = lbl_cheat_files, .action = toggle_cheat_files },
    { .text = lbl_pal60,       .action = toggle_pal60       },
    { .text = lbl_font,        .action = toggle_font        },
    { .text = lbl_screensaver, .action = cycle_screensaver  },
    { .text = lbl_ss_fav,      .action = toggle_ss_fav      },
    { .text = lbl_ss_timer,    .action = cycle_ss_timer     },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = lbl_boot_opt,    .action = toggle_loading_bar },
#else
    { .text = lbl_boot_opt,    .action = toggle_fast_reboot },
#endif
#ifdef BETA_SETTINGS
    { .text = lbl_rom_ext,     .action = toggle_rom_ext     },
    { .text = lbl_rom_tags,    .action = toggle_rom_tags    },
    { .text = lbl_rumble,      .action = toggle_rumble      },
#endif
    { .text = "" },
    { .text = "Clear all favorites", .action = gs_clear_favorites },
    { .text = "Clear all history",   .action = gs_clear_history   },
    { .text = "Reset settings",      .action = gs_reset           },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* ---------- process ---------- */

static void process(menu_t *menu) {
    /* Track the open row every frame so we can restore it after a toggle. */
    if (options_context_menu.row_selected >= 0) {
        settings_saved_row = options_context_menu.row_selected;
    }

    /* PAL60 countdown confirm — takes over all input. */
    if (pal60_confirm_active) {
        bool revert = false;
        if ((int32_t)((uint32_t)get_ticks_ms() - pal60_deadline_ms) >= 0 || menu->actions.back) {
            revert = true;
        } else if (menu->actions.go_up || menu->actions.go_down) {
            pal60_choice = !pal60_choice;
            sound_play_effect(SFX_CURSOR);
        } else if (menu->actions.enter) {
            if (pal60_choice == 1) {
                revert = true;
            } else {
                /* Keep — just close the confirm and reopen the settings menu */
                pal60_confirm_active = false;
                sound_play_effect(SFX_ENTER);
                ui_components_context_menu_show(&options_context_menu);
                options_context_menu.row_selected = settings_saved_row;
            }
        }
        if (revert) {
            menu->settings.pal60_enabled = pal60_prev_value;
            settings_save(&menu->settings);
            pal60_apply(pal60_prev_value);
            update_settings_labels(menu);
            pal60_confirm_active = false;
            sound_play_effect(SFX_EXIT);
            ui_components_context_menu_show(&options_context_menu);
            options_context_menu.row_selected = settings_saved_row;
        }
        return;
    }

    /* Reset-settings confirmation takes over input. */
    if (show_message_reset_settings) {
        if (menu->actions.enter) {
            settings_reset_to_defaults();
            menu_show_error(menu, "Reboot N64 to take effect!");
            show_message_reset_settings = false;
            sound_play_effect(SFX_SETTING);
        } else if (menu->actions.back) {
            show_message_reset_settings = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* Clear-all-favorites confirmation takes over input. */
    if (show_message_clear_favorites) {
        if (menu->actions.enter) {
            bookkeeping_favorite_clear_all(&menu->bookkeeping);
            show_message_clear_favorites = false;
            sound_play_effect(SFX_SETTING);
        } else if (menu->actions.back) {
            show_message_clear_favorites = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* Clear-all-history confirmation takes over input. */
    if (show_message_clear_history) {
        if (menu->actions.enter) {
            bookkeeping_history_clear_all(&menu->bookkeeping);
            show_message_clear_history = false;
            sound_play_effect(SFX_SETTING);
        } else if (menu->actions.back) {
            show_message_clear_history = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    /* The settings popup is always open and directly editable. B leaves the editor.
       (Intercepted before the context menu so B doesn't merely close the list.) */
    if (menu->actions.back) {
        menu->next_mode = menu->load.load_return_mode ? menu->load.load_return_mode : MENU_MODE_BROWSER;
        menu->load.load_return_mode = 0;
        sound_play_effect(SFX_EXIT);
        return;
    }

    /* Up/Down/A drive the toggles (and the Reset settings item). */
    ui_components_context_menu_process(menu, &options_context_menu);

    /* Keep the popup open: a toggle action auto-closes the list one frame later;
       reopen it at the same row. Don't reopen while a confirm popup is up. */
    bool is_open = (options_context_menu.row_selected >= 0);
    if (settings_was_open && !is_open && !pal60_confirm_active && !show_message_reset_settings) {
        settings_action_fired = false;
        ui_components_context_menu_show(&options_context_menu);
        options_context_menu.row_selected = settings_saved_row;
        is_open = true;
    }
    settings_was_open = is_open;
}

/* ---------- draw ---------- */

static void draw(menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    /* Standard: draw over the grid. */
    view_games_grid_draw_background(menu, d);

    /* The editable settings popup (always open) — it IS the editor. The default
       directory shows as the last row inside the box. */
    ui_components_context_menu_draw(&options_context_menu);

    if (show_message_reset_settings) {
        ui_components_messagebox_draw(
            "Reset ALL settings?\n"
            "This cannot be undone.\n\n"
            "A: Yes, B: Back"
        );
    }

    if (show_message_clear_favorites) {
        ui_components_messagebox_draw(
            "Clear ALL favorites?\n"
            "This cannot be undone.\n\n"
            "A: Yes, B: Back"
        );
    }

    if (show_message_clear_history) {
        ui_components_messagebox_draw(
            "Clear ALL history?\n"
            "This cannot be undone.\n\n"
            "A: Yes, B: Back"
        );
    }

    if (pal60_confirm_active) {
        uint32_t now = (uint32_t)get_ticks_ms();
        int secs = (pal60_deadline_ms > now) ? (int)((pal60_deadline_ms - now + 999) / 1000) : 0;
        ui_components_messagebox_draw(
            "PAL60 Mode changed to %s\n"
            "Is your display OK?\n\n"
            "%s Keep setting\n"
            "%s Revert setting\n\n"
            "Auto-reverting in %d s...",
            menu->settings.pal60_enabled ? "ON" : "OFF",
            pal60_choice == 0 ? ">" : " ",
            pal60_choice == 1 ? ">" : " ",
            secs
        );
    }

    rdpq_detach_show();
}


void view_settings_init(menu_t *menu) {
    update_settings_labels(menu);
    settings_action_fired = false;
    pal60_confirm_active  = false;
    show_message_reset_settings = false;
    ui_components_context_menu_init(&options_context_menu);
    /* Open the settings list immediately — it IS the editor (no separate box). */
    ui_components_context_menu_show(&options_context_menu);
    settings_saved_row = 0;
    settings_was_open  = true;
}

void view_settings_display(menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
