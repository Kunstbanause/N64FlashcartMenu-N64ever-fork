#include <errno.h>
#include <fatfs/ff.h>   /* f_rename: libdragon doesn't hook libc rename(), so Move uses fatfs directly */
#include <miniz.h>
#include <miniz_zip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../cart_load.h"
#include "../fonts.h"
#include "../fs_filter.h"
#include "../ini_parser.h"
#include "../library.h"
#include "../rom_info.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"
#include "../sound.h"
#include "../disclink.h"

static const char *archive_extensions[] = { "zip", NULL };
static const char *cheat_extensions[] = {"cht", "cheats", "datel", "gameshark", NULL};
static const char *emulator_extensions[] = { "nes", "sfc", "smc", "gb", "gbc", "sms", "gg", "sg", "chf", NULL };
static const char *image_extensions[] = { "png", NULL };
static const char *music_extensions[] = { "mp3", NULL };
static const char *patch_extensions[] = { "bps", "ips", "aps", "ups", "xdelta", NULL };
static const char *save_extensions[] = { "sav", "eep", "sra", "srm", "fla", NULL };
static const char *text_extensions[] = { "txt", "ini", "yml", "yaml", NULL };
static const char *rom_meta_extensions[] = { "meta", "metadata", NULL };

// static bool file_is_fat_hidden (const char *full_path) {
//     struct stat st;

//     if (stat(full_path, &st) == 0) {
//         return FAT_ATTR_IS_HID(&st);
//     }

//     return false;
// }

static int compare_entry (const void *pa, const void *pb) {
    entry_t *a = (entry_t *) (pa);
    entry_t *b = (entry_t *) (pb);

    if (a->type != b->type) {
        if (a->type == ENTRY_TYPE_DIR) {
            return -1;
        } else if (b->type == ENTRY_TYPE_DIR) {
            return 1;
        } else if (a->type == ENTRY_TYPE_ARCHIVE) {
            return -1;
        } else if (b->type == ENTRY_TYPE_ARCHIVE) {
            return 1;
        } else if (a->type == ENTRY_TYPE_DISK) {
            return -1;
        } else if (b->type == ENTRY_TYPE_DISK) {
            return 1;
        } else if (a->type == ENTRY_TYPE_EMULATOR) {
            return -1;
        } else if (b->type == ENTRY_TYPE_EMULATOR) {
            return 1;
        } else if (a->type == ENTRY_TYPE_IMAGE) {
            return -1;
        } else if (b->type == ENTRY_TYPE_IMAGE) {
            return 1;
        } else if (a->type == ENTRY_TYPE_MUSIC) {
            return -1;
        } else if (b->type == ENTRY_TYPE_MUSIC) {
            return 1;
        } else if (a->type == ENTRY_TYPE_ROM) {
            return -1;
        } else if (b->type == ENTRY_TYPE_ROM) {
            return 1;
        } else if (a->type == ENTRY_TYPE_ROM_CHEAT) {
            return -1;
        } else if (b->type == ENTRY_TYPE_ROM_CHEAT) {
            return 1;
        } else if (a->type == ENTRY_TYPE_ROM_PATCH) {
            return -1;
        } else if (b->type == ENTRY_TYPE_ROM_PATCH) {
            return 1;
        } else if (a->type == ENTRY_TYPE_SAVE) {
            return -1;
        } else if (b->type == ENTRY_TYPE_SAVE) {
            return 1;
        } else if (a->type == ENTRY_TYPE_TEXT) {
            return -1;
        } else if (b->type == ENTRY_TYPE_TEXT) {
            return 1;
        } else if (a->type == ENTRY_TYPE_ROM_META) {
            return -1;
        } else if (b->type == ENTRY_TYPE_ROM_META) {
            return 1;
        }
    }

    return strcasecmp((const char *) (a->name), (const char *) (b->name));
}

static void browser_list_free (menu_t *menu) {
    if (menu->browser.archive) {
        mz_zip_reader_end(&menu->browser.zip);
    }
    menu->browser.archive = false;

    for (int i = menu->browser.entries - 1; i >= 0; i--) {
        free(menu->browser.list[i].name);
    }

    free(menu->browser.list);

    menu->browser.list = NULL;
    menu->browser.entries = 0;
    menu->browser.entry = NULL;
    menu->browser.selected = -1;
}

static bool load_archive (menu_t *menu) {
    browser_list_free(menu);

    mz_zip_zero_struct(&menu->browser.zip);
    if (!mz_zip_reader_init_file(&menu->browser.zip, path_get(menu->browser.directory), 0)) {
        return true;
    }

    menu->browser.archive = true;
    menu->browser.entries = (int32_t)mz_zip_reader_get_num_files(&menu->browser.zip);
    menu->browser.list = malloc(menu->browser.entries * sizeof(entry_t));
    if (!menu->browser.list) {
        browser_list_free(menu);
        return true;
    }

    for (int32_t i = 0; i < menu->browser.entries; i++) {
        entry_t *entry = &menu->browser.list[i];

        mz_zip_archive_file_stat info;
        if (!mz_zip_reader_file_stat(&menu->browser.zip, i, &info)) {
            browser_list_free(menu);
            return true;
        }

        entry->name = strdup(info.m_filename);
        if (!entry->name) {
            browser_list_free(menu);
            return true;
        }

        entry->type = ENTRY_TYPE_ARCHIVED;
        entry->size = info.m_uncomp_size;
        entry->index = i;
        entry->presents_region = 0;
    }

    if (menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    qsort(menu->browser.list, menu->browser.entries, sizeof(entry_t), compare_entry);

    return false;
}

static bool load_directory (menu_t *menu) {
    int result;
    dir_t info;

    browser_list_free(menu);

    path_t *path = path_clone(menu->browser.directory);

    result = dir_findfirst(path_get(path), &info);

    while (result == 0) {
        bool hide = false;

        if (!menu->settings.show_protected_entries) {
            path_push(path, info.d_name);
            hide = path_is_hidden(path);
            path_pop(path);
        }

        if (!menu->settings.show_saves_folder) {
            path_push(path, info.d_name);
            // Skip the "saves" directory if it is hidden (this is case sensitive)
            if (strcmp(info.d_name, SAVE_DIRECTORY_NAME) == 0) {
                hide = true;
            }
            path_pop(path);
        }

        if (!menu->settings.show_save_files) {
            path_push(path, info.d_name);
            // Skip save files if they are hidden (this is case sensitive)
            if (file_has_extensions(info.d_name, save_extensions)) {
                hide = true;
            }
            path_pop(path);
        }

        if (!menu->settings.show_cheat_files) {
            path_push(path, info.d_name);
            // Skip cheat files if they are hidden (this is case sensitive)
            if (file_has_extensions(info.d_name, cheat_extensions)) {
                hide = true;
            }
            path_pop(path);
        }

        if (!hide) {
            menu->browser.list = realloc(menu->browser.list, (menu->browser.entries + 1) * sizeof(entry_t));

            entry_t *entry = &menu->browser.list[menu->browser.entries++];

            entry->name = strdup(info.d_name);
            if (!entry->name) {
                path_free(path);
                browser_list_free(menu);
                return true;
            }

            if (info.d_type == DT_DIR) {
                entry->type = ENTRY_TYPE_DIR;
            } else if (file_has_extensions(entry->name, n64_rom_extensions)) {
                entry->type = ENTRY_TYPE_ROM;
            } else if (file_has_extensions(entry->name, disk_extensions)) {
                entry->type = ENTRY_TYPE_DISK;
            } else if (file_has_extensions(entry->name, patch_extensions)) {
                entry->type = ENTRY_TYPE_ROM_PATCH;
            } else if (file_has_extensions(entry->name, cheat_extensions)) {
                entry->type = ENTRY_TYPE_ROM_CHEAT;
            } else if (file_has_extensions(entry->name, emulator_extensions)) {
                entry->type = ENTRY_TYPE_EMULATOR;
            } else if (file_has_extensions(entry->name, save_extensions)) {
                entry->type = ENTRY_TYPE_SAVE;
            } else if (file_has_extensions(entry->name, image_extensions)) {
                entry->type = ENTRY_TYPE_IMAGE;
            } else if (file_has_extensions(entry->name, text_extensions)) {
                entry->type = ENTRY_TYPE_TEXT;
            } else if (file_has_extensions(entry->name, music_extensions)) {
                entry->type = ENTRY_TYPE_MUSIC;
            } else if (file_has_extensions(entry->name, archive_extensions)) {
                entry->type = ENTRY_TYPE_ARCHIVE;
            } else if (file_has_extensions(entry->name, rom_meta_extensions)) {
                entry->type = ENTRY_TYPE_ROM_META;
            } else {
                entry->type = ENTRY_TYPE_OTHER;
            }

            entry->size = info.d_size;
            entry->index = menu->browser.entries - 1;
        }

        result = dir_findnext(path_get(path), &info);
    }

    path_free(path);

    if (result < -1) {
        browser_list_free(menu);
        return true;
    }

    if (menu->browser.entries > 0) {
        menu->browser.selected = 0;
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    qsort(menu->browser.list, menu->browser.entries, sizeof(entry_t), compare_entry);

    /* Populate presents_region for each entry (0 by default, letter if custom ini overrides it) */
    for (int i = 0; i < menu->browser.entries; i++) {
        menu->browser.list[i].presents_region = 0;
    }
    {
        char custom_dir[128];
        snprintf(custom_dir, sizeof(custom_dir), "%s/menu/n64ever", menu->storage_prefix);
        if (directory_exists(custom_dir)) {
            for (int i = 0; i < menu->browser.entries; i++) {
                entry_t *entry = &menu->browser.list[i];
                if (entry->type != ENTRY_TYPE_ROM) continue;
                char stem[256];
                strncpy(stem, entry->name, sizeof(stem) - 1); stem[sizeof(stem) - 1] = '\0';
                char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
                char ini_path[400];
                snprintf(ini_path, sizeof(ini_path), "%s/menu/n64ever/gameconfigs/%s.ini", menu->storage_prefix, stem);
                ini_t *ini = ini_load(ini_path);
                if (!ini) {
                    snprintf(ini_path, sizeof(ini_path), "%s/menu/custom/gameconfigs/%s.ini", menu->storage_prefix, stem);
                    ini = ini_load(ini_path);
                }
                if (ini) {
                    int pa = ini_get_int(ini, "", "presents_as", 0);
                    ini_free(ini);
                    switch (pa) {
                        case ROM_PRESENTS_AS_NTSC:   entry->presents_region = 'U'; break;
                        case ROM_PRESENTS_AS_PAL:    entry->presents_region = 'E'; break;
                        case ROM_PRESENTS_AS_NTSC_J: entry->presents_region = 'J'; break;
                        default:                      entry->presents_region = 0;   break;
                    }
                }
            }
        }
    }

    return false;
}

static bool reload_directory (menu_t *menu) {
    int selected = menu->browser.selected;

    if (load_directory(menu)) {
        return true;
    }

    menu->browser.selected = selected;
    if (menu->browser.selected >= menu->browser.entries) {
        menu->browser.selected = menu->browser.entries - 1;
    }
    menu->browser.entry = menu->browser.selected >= 0 ? &menu->browser.list[menu->browser.selected] : NULL;

    return false;
}

static bool push_directory (menu_t *menu, char *directory, bool archive) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_push(menu->browser.directory, directory);

    if (archive ? load_archive(menu) : load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    path_free(previous_directory);

    return false;
}

static bool pop_directory (menu_t *menu) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_pop(menu->browser.directory);

    if (load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    for (uint16_t i = 0; i < menu->browser.entries; i++) {
        if (strcmp(menu->browser.list[i].name, path_last_get(previous_directory)) == 0) {
            menu->browser.selected = i;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
            break;
        }
    }

    path_free(previous_directory);

    return false;
}

static bool select_file (menu_t *menu, path_t *file) {
    path_t *previous_directory = path_clone(menu->browser.directory);

    path_free(menu->browser.directory);
    menu->browser.directory = path_clone(file);
    path_pop(menu->browser.directory);

    if (load_directory(menu)) {
        path_free(menu->browser.directory);
        menu->browser.directory = previous_directory;
        return true;
    }

    for (uint16_t i = 0; i < menu->browser.entries; i++) {
        if (strcmp(menu->browser.list[i].name, path_last_get(file)) == 0) {
            menu->browser.selected = i;
            menu->browser.entry = &menu->browser.list[menu->browser.selected];
            break;
        }
    }

    path_free(previous_directory);

    return false;
}

/* When a More-menu item opens another view, we snapshot the open menu chain so
   the exact cursor position (top row + any nested submenu rows, up to 3 deep:
   e.g. top -> Extra -> Hardware) is restored when that view closes. This is what
   makes backing out of a submenu return to the item you entered, not the top. */
static bool reopen_pending = false;
static component_context_menu_t *reopen_top  = NULL;  static int reopen_top_row  = 0;
static component_context_menu_t *reopen_sub1 = NULL;  static int reopen_sub1_row = 0;
static component_context_menu_t *reopen_sub2 = NULL;  static int reopen_sub2_row = 0;

/* Capture the currently-open menu chain (called every frame the menu is active,
   so when an action launches a view the pre-action chain is already recorded). */
static void capture_reopen_chain(component_context_menu_t *top) {
    reopen_top = top;  reopen_top_row = top->row_selected;
    component_context_menu_t *s1 = top->submenu;
    reopen_sub1 = s1;  reopen_sub1_row = s1 ? s1->row_selected : 0;
    component_context_menu_t *s2 = s1 ? s1->submenu : NULL;
    reopen_sub2 = s2;  reopen_sub2_row = s2 ? s2->row_selected : 0;
}

/* When true, the browser renders as a small rainbow popup over the grid background.
   Navigation is simplified: no hold-B favorite, Z/R exits to grid. */
static bool browser_popup_mode = false;
static int  popup_scroll       = 0;
/* Popup hold-B multi-mark: while B is held and the cursor sweeps (incl. C-scroll
   jumps), mark a range visually at once; commit to bookkeeping on release so the
   menu never stutters mid-sweep. */
static bool pop_multi      = false;
static bool pop_multi_add  = false;   /* add vs remove for the whole sweep */
static bool pop_start_fav  = false;   /* fav state of the entry where the hold began */
static int  pop_multi_lo   = 0;
static int  pop_multi_hi   = 0;
/* Horizontal scroll of the selected entry's (long) name — reset when selection moves. */
static int  popup_hscroll     = 0;
static int  popup_hscroll_sel = -1;
/* Set by view_browser_open_popup(); consumed in view_browser_init() to decide the
   mode. We must NOT clear browser_popup_mode on exit (doing so makes draw() render
   the full file list for one frame before the grid takes over — the "files flash").
   Instead the flag is (re)set from this intent on the next browser entry. */
static bool popup_requested = false;
/* Set by view_browser_open_file_management(): open the popup at a specific ROM with
   the canonical File-management submenu already showing. */
static bool fm_requested = false;
/* Set when the popup opens a transient file view (image/text/music/info/extract,
   or load-disk/emulator). Makes the return to MENU_MODE_BROWSER stay in the popup
   instead of dropping to the old full file list. Separate from reopen_pending,
   which also rebuilds the More-menu chain (file views don't use that menu). */
static bool popup_return_pending = false;

/* Fav-cache dirty flag — declared here so toggle_favorite() can reference it */
static bool fav_dirty = true;
/* Favorites changed in memory but not yet written to SD. toggle/range favorite no longer save
   per-toggle (that rewrote the whole ~946-entry favorites.ini each time = multi-second freeze);
   instead we flush ONCE when leaving the browser (view_browser_display, on any next_mode change).
   Also gives the "don't update until you exit" behavior. Caveat: cutting power while still IN the
   browser (without exiting) loses the pending toggles -- the normal R-to-grid exit flushes them. */
static bool fav_unsaved = false;

/* ---- History popup (submenu of the universal More menu) ---- */
#define HISTORY_POPUP_MAX 20
static char hist_popup_labels[HISTORY_POPUP_MAX][64];

static component_context_menu_t history_popup_menu = {
    .row_selected = -1,  /* hidden until rebuild_history_popup initializes it */
    .left_align   = true,
    .list = {
        {0},{0},{0},{0},{0},{0},{0},{0},{0},{0},
        {0},{0},{0},{0},{0},{0},{0},{0},{0},{0},
        COMPONENT_CONTEXT_MENU_LIST_END,  /* permanent terminator at index 20 */
    }
};

static void history_popup_launch(menu_t *menu, void *arg) {
    int i = (int)(intptr_t)arg;
    if (i < 0 || i >= HISTORY_COUNT) return;
    bookkeeping_item_t *item = &menu->bookkeeping.history_items[i];
    if (!item->primary_path) return;
    menu->load.load_history_id  = i;
    menu->load.load_favorite_id = -1;
    menu->load.load_return_mode = MENU_MODE_BROWSER;
    reopen_pending = true;
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

static void rebuild_history_popup(menu_t *menu) {
    int n = 0;
    for (int i = 0; i < HISTORY_COUNT && n < HISTORY_POPUP_MAX; i++) {
        bookkeeping_item_t *item = &menu->bookkeeping.history_items[i];
        if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || !item->primary_path) continue;
        const char *name = path_last_get(item->primary_path);
        if (!name || name[0] == '\0') continue;
        int len = (int)strlen(name);
        /* Strip common ROM extension */
        if (len > 4 && name[len-4] == '.') {
            snprintf(hist_popup_labels[n], sizeof(hist_popup_labels[n]), "%.*s", len - 4, name);
        } else {
            strncpy(hist_popup_labels[n], name, sizeof(hist_popup_labels[n]) - 1);
            hist_popup_labels[n][sizeof(hist_popup_labels[n]) - 1] = '\0';
        }
        history_popup_menu.list[n].text   = hist_popup_labels[n];
        history_popup_menu.list[n].action = history_popup_launch;
        history_popup_menu.list[n].arg    = (void *)(intptr_t)i;
        history_popup_menu.list[n].submenu= NULL;
        n++;
    }
    if (n == 0) {
        strncpy(hist_popup_labels[0], "No history", sizeof(hist_popup_labels[0]) - 1);
        history_popup_menu.list[0].text   = hist_popup_labels[0];
        history_popup_menu.list[0].action = NULL;
        history_popup_menu.list[0].arg    = NULL;
        n = 1;
    }
    history_popup_menu.list[n].text = NULL;  /* dynamic terminator */
    ui_components_context_menu_init(&history_popup_menu);
}

/* ---- Favorite/Unfavorite toggle ---- */
static char fav_toggle_label[24] = "Favorite";

/* ROMs and 64DD disks (.ndd) can both be favourited; a disk favourite stores type DISK
   so the grid routes it to the disk loader and identifies it from disk_info.id / filename. */
static bool entry_is_favoritable(int type) {
    return type == ENTRY_TYPE_ROM || type == ENTRY_TYPE_DISK;
}

static void update_fav_label(menu_t *menu) {
    if (!menu->browser.entry || !entry_is_favoritable(menu->browser.entry->type)) {
        strncpy(fav_toggle_label, "Favorite", sizeof(fav_toggle_label) - 1);
        return;
    }
    path_t *p = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    bool is_fav = false;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(p, f->primary_path)) {
            is_fav = true; break;
        }
    }
    path_free(p);
    strncpy(fav_toggle_label, is_fav ? "Unfavorite" : "Favorite", sizeof(fav_toggle_label) - 1);
}

static void toggle_favorite(menu_t *menu, void *arg) {
    if (!menu->browser.entry || !entry_is_favoritable(menu->browser.entry->type)) return;
    bool is_disk = (menu->browser.entry->type == ENTRY_TYPE_DISK);
    path_t *p = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    int fi = -1;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(p, f->primary_path)) {
            fi = k; break;
        }
    }
    if (fi >= 0) {
        bookkeeping_favorite_remove_nosave(&menu->bookkeeping, fi);
    } else {
        bookkeeping_favorite_add_nosave(&menu->bookkeeping, p, NULL,
            is_disk ? BOOKKEEPING_TYPE_DISK : BOOKKEEPING_TYPE_ROM);
    }
    path_free(p);
    update_fav_label(menu);
    fav_dirty = true;
    fav_unsaved = true;   /* flushed once on browser exit (no per-toggle full-ini rewrite) */
}

/* ---- Pin/unpin a folder as a library tab ----
   Same folder-resolution convention as Fav/Unfav inside folder: a highlighted subfolder is
   the target; otherwise (browsing inside one, or a non-folder entry highlighted) the CURRENT
   directory is. */
static path_t *pin_target_folder(menu_t *menu) {
    if (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DIR) {
        return path_clone_push(menu->browser.directory, menu->browser.entry->name);
    }
    return path_clone(menu->browser.directory);
}

static char pin_toggle_label[40] = "Pin as Library";

static void update_pin_label(menu_t *menu) {
    path_t *p = pin_target_folder(menu);
    bool pinned = library_find_by_path(p) >= 0;
    path_free(p);
    strncpy(pin_toggle_label, pinned ? "Unpin Library" : "Pin as Library", sizeof(pin_toggle_label) - 1);
    pin_toggle_label[sizeof(pin_toggle_label) - 1] = '\0';
}

static void toggle_pin_folder(menu_t *menu, void *arg) {
    (void)arg;
    path_t *p = pin_target_folder(menu);
    int idx = library_find_by_path(p);
    if (idx >= 0) {
        library_remove(idx);
        sound_play_effect(SFX_EXIT);
        path_free(p);
    } else if (library_add(NULL, p)) {
        sound_play_effect(SFX_SETTING);
        path_free(p);
    } else {
        path_free(p);
        menu_show_error(menu, "Can't pin: already at the library limit,\nor this folder is already pinned.");
    }
    update_pin_label(menu);
}

static void show_properties (menu_t *menu, void *arg) {
    if (!menu->browser.entry) return;   /* empty folder: no entry to inspect */
    reopen_pending = true;
    menu->next_mode = menu->browser.entry->type == ENTRY_TYPE_ARCHIVED ? MENU_MODE_EXTRACT_FILE : MENU_MODE_FILE_INFO;
}

static void delete_entry (menu_t *menu, void *arg) {
    if (!menu->browser.entry) return;   /* empty folder: nothing selected to delete */
    path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);

    if (remove(path_get(path))) {
        menu->browser.valid = false;
        if (menu->browser.entry->type == ENTRY_TYPE_DIR) {
            menu_show_error(menu, "Couldn't delete directory\nDirectory might not be empty");
        } else {
            menu_show_error(menu, "Couldn't delete file");
        }
        path_free(path);
        return;
    }

    path_free(path);

    if (reload_directory(menu)) {
        menu->browser.valid = false;
        menu_show_error(menu, "Couldn't refresh directory contents after delete operation");
    }
}

static void extract_entry (menu_t *menu, void *arg) {
    if (!menu->browser.entry) return;   /* empty archive: nothing selected to extract */
    menu->load_pending.extract_file = true;
    menu->next_mode = MENU_MODE_EXTRACT_FILE;
}

static void set_default_directory (menu_t *menu, void *arg) {
    free(menu->settings.default_directory);
    menu->settings.default_directory = strdup(strip_fs_prefix(path_get(menu->browser.directory)));
    settings_save(&menu->settings);
}

/* Set the selected ROM as the power-on boot ROM and enable ROM boot. The directory is stored the
   same prefix-relative way as the default directory, so startup can rebuild the path. */
static void set_rom_boot (menu_t *menu, void *arg) {
    (void)arg;
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_ROM) return;
    free(menu->settings.rom_boot_path);
    menu->settings.rom_boot_path = strdup(strip_fs_prefix(path_get(menu->browser.directory)));
    free(menu->settings.rom_boot_filename);
    menu->settings.rom_boot_filename = strdup(menu->browser.entry->name);
    menu->settings.rom_boot_enabled = true;
    settings_save(&menu->settings);
    sound_play_effect(SFX_SETTING);
}

/* Toggle ROM boot on/off, keeping whichever ROM was chosen. */
static void toggle_rom_boot (menu_t *menu, void *arg) {
    (void)arg;
    menu->settings.rom_boot_enabled = !menu->settings.rom_boot_enabled;
    settings_save(&menu->settings);
    sound_play_effect(SFX_SETTING);
}

/* Cycle the ROM-boot countdown length: 1,2,3,4,5,10,15 seconds. */
static void cycle_rom_boot_countdown (menu_t *menu, void *arg) {
    (void)arg;
    static const int opts[] = { 1, 2, 3, 4, 5, 10, 15 };
    const int n = (int)(sizeof(opts) / sizeof(opts[0]));
    int idx = 0;
    for (int i = 0; i < n; i++) if (opts[i] == menu->settings.rom_boot_countdown_sec) { idx = i; break; }
    menu->settings.rom_boot_countdown_sec = opts[(idx + 1) % n];
    settings_save(&menu->settings);
    sound_play_effect(SFX_SETTING);
}

/* ===================== On-screen keyboard (Rename / Create folder) =====================
   A small modal text-entry widget driven from File management. While active it owns input and
   the screen: process()/draw() early-return into it. Buttons follow the standard action bar --
   A = type the highlighted key, B = backspace, START = OK, R = cancel; the D-pad/stick move the
   cursor, and the bottom row holds Shift (case) and Space. */
static int  kb_mode = 0;            /* 0 = off, 1 = rename, 2 = new folder */
static char kb_text[256] = "";
static int  kb_len = 0;
static int  kb_sel = 0;             /* index into kb_layout of the highlighted key */
static bool kb_caps = true;

/* QWERTY layout on a 10-column grid (rows 0..4). Letters are stored uppercase; case is applied
   on insert/draw. Rows: 0=numbers, 1=QWERTYUIOP, 2=ASDFGHJKL (9), 3=ZXCVBNM (shifted right one)
   plus '-', 4=function (Shift / Space / '.' / '_'). '-' sits above '_'; '.' is left of '_'. */
enum { KK_CHAR, KK_SHIFT, KK_SPACE };
typedef struct { uint8_t row, col, wcols, kind; char ch; } kb_key_t;
static const kb_key_t kb_layout[] = {
    {0,0,1,KK_CHAR,'1'},{0,1,1,KK_CHAR,'2'},{0,2,1,KK_CHAR,'3'},{0,3,1,KK_CHAR,'4'},{0,4,1,KK_CHAR,'5'},
    {0,5,1,KK_CHAR,'6'},{0,6,1,KK_CHAR,'7'},{0,7,1,KK_CHAR,'8'},{0,8,1,KK_CHAR,'9'},{0,9,1,KK_CHAR,'0'},
    {1,0,1,KK_CHAR,'Q'},{1,1,1,KK_CHAR,'W'},{1,2,1,KK_CHAR,'E'},{1,3,1,KK_CHAR,'R'},{1,4,1,KK_CHAR,'T'},
    {1,5,1,KK_CHAR,'Y'},{1,6,1,KK_CHAR,'U'},{1,7,1,KK_CHAR,'I'},{1,8,1,KK_CHAR,'O'},{1,9,1,KK_CHAR,'P'},
    {2,0,1,KK_CHAR,'A'},{2,1,1,KK_CHAR,'S'},{2,2,1,KK_CHAR,'D'},{2,3,1,KK_CHAR,'F'},{2,4,1,KK_CHAR,'G'},
    {2,5,1,KK_CHAR,'H'},{2,6,1,KK_CHAR,'J'},{2,7,1,KK_CHAR,'K'},{2,8,1,KK_CHAR,'L'},
    {3,1,1,KK_CHAR,'Z'},{3,2,1,KK_CHAR,'X'},{3,3,1,KK_CHAR,'C'},{3,4,1,KK_CHAR,'V'},{3,5,1,KK_CHAR,'B'},
    {3,6,1,KK_CHAR,'N'},{3,7,1,KK_CHAR,'M'},{3,9,1,KK_CHAR,'-'},
    {4,0,2,KK_SHIFT,0},{4,2,6,KK_SPACE,' '},{4,8,1,KK_CHAR,'.'},{4,9,1,KK_CHAR,'_'},
};
#define KB_NKEYS  ((int)(sizeof(kb_layout) / sizeof(kb_layout[0])))
#define KB_COLS   10
#define KB_NROWS  5

static void kb_open (menu_t *menu, int mode) {
    kb_mode = mode;
    kb_sel = 0;
    kb_caps = true;
    if (mode == 1 && menu->browser.entry) {           /* rename: pre-fill the current name */
        strncpy(kb_text, menu->browser.entry->name, sizeof(kb_text) - 1);
        kb_text[sizeof(kb_text) - 1] = '\0';
    } else {
        kb_text[0] = '\0';
    }
    kb_len = (int)strlen(kb_text);
    sound_play_effect(SFX_SETTING);
}

static void kb_insert (char c) {
    if (kb_len < (int)sizeof(kb_text) - 1) {
        kb_text[kb_len++] = c;
        kb_text[kb_len] = '\0';
    }
}

static void kb_commit (menu_t *menu) {
    int mode = kb_mode;
    while (kb_len > 0 && kb_text[kb_len - 1] == ' ') kb_text[--kb_len] = '\0';   /* trim trailing spaces */
    if (kb_len == 0) { kb_mode = 0; sound_play_effect(SFX_EXIT); return; }       /* empty -> cancel */

    path_t *dst = path_clone_push(menu->browser.directory, kb_text);
    bool exists = file_exists(path_get(dst));
    bool ok = false;
    if (mode == 1) {                                   /* rename via fatfs (libc rename() unhooked) */
        if (menu->browser.entry && !exists) {
            path_t *src = path_clone_push(menu->browser.directory, menu->browser.entry->name);
            ok = (f_rename(strip_fs_prefix(path_get(src)), strip_fs_prefix(path_get(dst))) == FR_OK);
            path_free(src);
        }
    } else {                                           /* create folder */
        if (!exists) ok = (f_mkdir(strip_fs_prefix(path_get(dst))) == FR_OK);
    }
    path_free(dst);
    kb_mode = 0;
    if (exists) { menu_show_error(menu, "A file or folder with that name\nalready exists."); return; }
    if (!ok)    { menu_show_error(menu, mode == 1 ? "Rename failed." : "Couldn't create folder."); return; }
    if (reload_directory(menu)) { menu->browser.valid = false; }
    sound_play_effect(SFX_ENTER);
}

/* Visual centre column of a key (handles wide Shift/Space) for nearest-column up/down nav. */
static float kb_center (const kb_key_t *k) { return k->col + (k->wcols - 1) / 2.0f; }

/* Move the cursor. dcol = -1/+1 steps within the row; drow = -1/+1 jumps to the nearest key
   (by visual column) in the adjacent row -- so the staggered rows + punctuation cluster all
   navigate sensibly. */
static void kb_nav (int drow, int dcol) {
    const kb_key_t *cur = &kb_layout[kb_sel];
    int best = -1;
    if (dcol != 0) {
        int bestd = 9999;
        for (int i = 0; i < KB_NKEYS; i++) {
            if (kb_layout[i].row != cur->row) continue;
            int d = (int)kb_layout[i].col - (int)cur->col;
            if (dcol > 0 && d > 0 && d < bestd)  { bestd = d;  best = i; }
            if (dcol < 0 && d < 0 && -d < bestd) { bestd = -d; best = i; }
        }
    } else if (drow != 0) {
        int trow = (int)cur->row + drow;
        if (trow < 0 || trow >= KB_NROWS) return;
        float cc = kb_center(cur), bestd = 1e9f;
        for (int i = 0; i < KB_NKEYS; i++) {
            if (kb_layout[i].row != trow) continue;
            float d = kb_center(&kb_layout[i]) - cc; if (d < 0) d = -d;
            if (d < bestd) { bestd = d; best = i; }
        }
    }
    if (best >= 0) { kb_sel = best; sound_play_effect(SFX_CURSOR); }
}

static void kb_process (menu_t *menu) {
    if      (menu->actions.go_up)    kb_nav(-1, 0);
    else if (menu->actions.go_down)  kb_nav(+1, 0);
    else if (menu->actions.go_left)  kb_nav(0, -1);
    else if (menu->actions.go_right) kb_nav(0, +1);

    if (menu->actions.enter) {                          /* A: type / activate */
        const kb_key_t *k = &kb_layout[kb_sel];
        if      (k->kind == KK_SHIFT) { kb_caps = !kb_caps; sound_play_effect(SFX_SETTING); }
        else if (k->kind == KK_SPACE) { kb_insert(' ');     sound_play_effect(SFX_CURSOR);  }
        else {
            char c = k->ch;
            if (c >= 'A' && c <= 'Z' && !kb_caps) c = (char)(c - 'A' + 'a');
            kb_insert(c); sound_play_effect(SFX_CURSOR);
        }
    } else if (menu->actions.back) {                    /* B: backspace */
        if (kb_len > 0) { kb_text[--kb_len] = '\0'; sound_play_effect(SFX_CURSOR); }
    } else if (menu->actions.settings) {                /* START: OK */
        kb_commit(menu);
    } else if (menu->actions.options) {                 /* Z: cancel */
        kb_mode = 0; sound_play_effect(SFX_EXIT);
    }
}

static void kb_popup_draw (void) {
    const int KW = 30, KH = 26, GX = 5, GY = 6, PAD = 18;
    int grid_w     = KB_COLS * KW + (KB_COLS - 1) * GX;       /* 345 */
    int grid_total = KB_NROWS * KH + (KB_NROWS - 1) * GY;    /* 5 rows incl. function row */
    int box_w = grid_w + PAD * 2;                            /* 381 */
    int box_h = PAD + 22 + 8 + 28 + 14 + grid_total + PAD;   /* 262 */
    ui_components_dialog_draw(box_w, box_h);

    int bx0 = DISPLAY_CENTER_X - box_w / 2;
    int by0 = DISPLAY_CENTER_Y - box_h / 2;
    int cx0 = bx0 + PAD;
    int interior_w = box_w - PAD * 2;

    rdpq_text_print(&(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = interior_w, .align = ALIGN_CENTER },
                    FNT_DEFAULT, cx0, by0 + PAD + 14, kb_mode == 2 ? "New folder" : "Rename");

    int fy0 = by0 + PAD + 22 + 8;
    int fy1 = fy0 + 28;
    ui_components_border_draw(cx0 - 1, fy0 - 1, cx0 + interior_w + 1, fy1 + 1);
    char disp[260];
    snprintf(disp, sizeof disp, "%s|", kb_text);
    rdpq_text_print(&(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = interior_w - 12, .align = ALIGN_LEFT, .wrap = WRAP_ELLIPSES },
                    FNT_DEFAULT, cx0 + 6, fy0 + 19, disp);

    int gx0 = DISPLAY_CENTER_X - grid_w / 2;
    int gy0 = fy1 + 14;
    for (int i = 0; i < KB_NKEYS; i++) {
        const kb_key_t *k = &kb_layout[i];
        int kx = gx0 + k->col * (KW + GX);
        int kw = k->wcols * KW + (k->wcols - 1) * GX;
        int ky = gy0 + k->row * (KH + GY);
        if (i == kb_sel)
            ui_components_box_draw(kx, ky, kx + kw, ky + KH, CONTEXT_MENU_HIGHLIGHT_COLOR);
        char label[8];
        if      (k->kind == KK_SHIFT) snprintf(label, sizeof label, "Shift");
        else if (k->kind == KK_SPACE) snprintf(label, sizeof label, "Space");
        else {
            char c = k->ch;
            if (c >= 'A' && c <= 'Z' && !kb_caps) c = (char)(c - 'A' + 'a');
            label[0] = c; label[1] = '\0';
        }
        rdpq_text_print(&(rdpq_textparms_t){ .style_id = STL_DEFAULT, .width = kw, .align = ALIGN_CENTER },
                        FNT_DEFAULT, kx, ky + 18, label);
    }
}

static void rename_entry (menu_t *menu, void *arg) {
    (void)arg;
    if (!menu->browser.entry) return;
    kb_open(menu, 1);
}

static void create_folder (menu_t *menu, void *arg) {
    (void)arg;
    kb_open(menu, 2);
}

static void set_menu_next_mode (menu_t *menu, void *arg) {
    /* The snapshot reopen restores the exact chain (incl. Extra -> Hardware), so
       no per-mode bookkeeping is needed here any more. */
    reopen_pending = true;
    menu->next_mode = (menu_mode_t) (arg);
}

/* ---- Link-pick mode --------------------------------------------------------------------
   "Link disc (64DD)" arms this on the selected ROM/disc; the browser then stays put and the
   user navigates the WHOLE filesystem freely. The NEXT ROM (if the source was a disc) or disc
   (if the source was a ROM) opened with A is paired with the source as a combined disk+cart
   favourite (primary=disc, secondary=ROM) and booted via load_disk -- NOT launched standalone.
   The base ROM does NOT have to live in the disc's folder. R cancels. */
static bool    link_pick_active     = false;
static bool    link_pick_src_is_disc = false;
static path_t *link_pick_src        = NULL;
static char    link_pick_name[64]   = "";

/* Grid-origin link: when the disc is already a favorite, persist the chosen base ROM as a
   disclink entry (keyed by disc code) and boot that existing favorite -- no duplicate favorite.
   This is THE disc->ROM link setting, not an override. */
static bool    link_pick_disclink   = false;
static char    link_pick_disc_code[5] = "";
static int     link_pick_disc_fav   = -1;

/* Cross-view hand-off: the grid arms a link-pick before switching to MENU_MODE_BROWSER, but
   view_browser_init() cancels any stale link-pick on entry -- so stash the request here and
   consume it AFTER that cancel. (No path needed: disclink mode boots by favorite id.) */
static bool    lp_pending           = false;
static char    lp_pending_name[64]  = "";
static char    lp_pending_code[5]   = "";
static int     lp_pending_fav       = -1;

static void cancel_link_pick (void) {
    link_pick_active = false;
    if (link_pick_src) { path_free(link_pick_src); link_pick_src = NULL; }
    link_pick_name[0] = '\0';
    link_pick_disclink = false;
    link_pick_disc_code[0] = '\0';
    link_pick_disc_fav = -1;
}

static void start_link_pick (menu_t *menu, void *arg) {
    (void)arg;
    if (!menu->browser.entry || !entry_is_favoritable(menu->browser.entry->type)) return;
    if (link_pick_src) { path_free(link_pick_src); link_pick_src = NULL; }
    link_pick_src = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    link_pick_src_is_disc = (menu->browser.entry->type == ENTRY_TYPE_DISK);
    strncpy(link_pick_name, menu->browser.entry->name, sizeof(link_pick_name) - 1);
    link_pick_name[sizeof(link_pick_name) - 1] = '\0';
    link_pick_active = true;
    /* Stay in the rainbow popup -- the popup process/draw paths now handle link-pick (A links
       the complementary-type file, B goes up, R cancels) and show the LINK banner. The context
       menu closes when this fires. */
}

/* Grid hand-off: arm a disc->base-ROM link-pick that survives the upcoming view_browser_init().
   The browser opens in full-list link-pick mode; the next ROM opened is stored as the disc's
   base (disclink, keyed by disc_code) and the disc favorite (disc_fav_id) boots combined. */
void view_browser_request_link_pick (const char *disc_name, const char *disc_code, int disc_fav_id) {
    strncpy(lp_pending_name, disc_name ? disc_name : "disc", sizeof lp_pending_name - 1);
    lp_pending_name[sizeof lp_pending_name - 1] = '\0';
    strncpy(lp_pending_code, disc_code ? disc_code : "", sizeof lp_pending_code - 1);
    lp_pending_code[sizeof lp_pending_code - 1] = '\0';
    lp_pending_fav = disc_fav_id;
    lp_pending = true;
}

/* Pair the link-pick source with the just-opened target file and boot combined. */
static void finish_link_pick (menu_t *menu) {
    /* Grid-origin disc: store the disc->ROM link (disclink) and boot the existing disc favorite
       combined -- no duplicate favorite, and load_disk resolves the base from disclink. */
    if (link_pick_disclink) {
        path_t *rom = path_clone_push(menu->browser.directory, menu->browser.entry->name);
        disclink_store(menu->storage_prefix, link_pick_disc_code, path_get(rom));
        path_free(rom);
        int fav = link_pick_disc_fav;
        cancel_link_pick();
        menu->load.load_history_id  = -1;
        menu->load.load_favorite_id = fav;
        menu->next_mode = MENU_MODE_LOAD_DISK;   /* combined disk+cart boot via disclink */
        sound_play_effect(SFX_ENTER);
        return;
    }
    path_t *target = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    path_t *disc = link_pick_src_is_disc ? link_pick_src : target;   /* primary = disc */
    path_t *rom  = link_pick_src_is_disc ? target : link_pick_src;   /* secondary = ROM */
    bookkeeping_favorite_add(&menu->bookkeeping, disc, rom, BOOKKEEPING_TYPE_DISK);
    bookkeeping_save(&menu->bookkeeping);
    int fi = -1;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
        if (f->bookkeeping_type == BOOKKEEPING_TYPE_DISK && f->primary_path &&
            path_are_match(disc, f->primary_path)) { fi = k; break; }
    }
    path_free(target);
    cancel_link_pick();
    menu->load.load_history_id  = -1;
    menu->load.load_favorite_id = fi;
    menu->next_mode = MENU_MODE_LOAD_DISK;   /* combined disk+cart boot */
    sound_play_effect(SFX_ENTER);
}

static void set_browser_presents_as(menu_t *menu, void *arg) {
    rom_presents_as_t presents_as = (rom_presents_as_t)(uintptr_t)(arg);
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_ROM) return;
    path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    rom_info_t dummy; memset(&dummy, 0, sizeof(dummy));
    rom_config_override_presents_as(path, &dummy, presents_as);
    path_free(path);
    switch (presents_as) {
        case ROM_PRESENTS_AS_NTSC:   menu->browser.entry->presents_region = 'U'; break;
        case ROM_PRESENTS_AS_PAL:    menu->browser.entry->presents_region = 'E'; break;
        case ROM_PRESENTS_AS_NTSC_J: menu->browser.entry->presents_region = 'J'; break;
        default:                      menu->browser.entry->presents_region = 0;   break;
    }
}

/* (The grid image-view / square-tiles settings live only in the grid's own menu;
   they were removed from the Files menus in build 104, so the cycle actions, their
   labels, and the grid_settings submenu that lived here are gone.) */

static component_context_menu_t set_presents_as_browser_context_menu = { .list = {
    { .text = "Auto (from ROM)",  .action = set_browser_presents_as, .arg = (void *)(uintptr_t)(ROM_PRESENTS_AS_AUTO) },
    { .text = "NTSC (American)",  .action = set_browser_presents_as, .arg = (void *)(uintptr_t)(ROM_PRESENTS_AS_NTSC) },
    { .text = "PAL (European)",   .action = set_browser_presents_as, .arg = (void *)(uintptr_t)(ROM_PRESENTS_AS_PAL) },
    { .text = "NTSC-J (Japan)",   .action = set_browser_presents_as, .arg = (void *)(uintptr_t)(ROM_PRESENTS_AS_NTSC_J) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* ---- Per-game image-view override (stored in the ROM's gameconfigs .ini) ---- */
static char pa_grid_label[26];
static char pa_inspect_label[26];
static char pa_load_label[26];
static bool presents_action_fired = false;
static int  pa_row_within = 0;

static const char *pa_type_name(int v) {
    static const char *names[] = { "Front", "Back", "3D Box", "Cart", "3D Cart", "Logo" };
    return (v >= 0 && v < GRID_IMAGE_COUNT) ? names[v] : "Default";
}

/* Default-directory line shown at the bottom of the File management submenu
   (full path, not truncated). */
static char fm_dir_label[256] = "Default dir: /";

/* Cut/copy + paste between directories. fop_pending: 0=none, 1=copy, 2=move. The source path
   is captured when Copy/Move is chosen; "Paste here" executes into the current directory. */
static int     fop_pending = 0;
static path_t *fop_src     = NULL;
static char    fop_name[256] = "";
static char    fm_paste_label[300] = "";   /* dynamic "<Move|Copy> here" entry; "" = hidden separator */
static char    fm_rb_set_label[24]    = "";                /* "Set ROM boot" for ROM entries; "" hides it */
static char    fm_rb_toggle_label[28] = "ROM boot: Disabled";
static char    fm_rb_countdown_label[36] = "ROM boot countdown: 5s";
/* Incremental copy: the byte-copy can't run inside the menu action (calling display_get()
   mid-action deadlocks the RSP), and a blocking copy would freeze the UI. Instead process()
   drives it across frames: phase 1 opens + measures (draw paints 0%), phase 2 copies a capped
   budget per frame so the progress bar animates and B: Cancel stays responsive. */
static int     fcopy_phase = 0;            /* 0=idle, 1=open+measure, 2=copying */
static path_t *fcopy_dest  = NULL;

static void update_dynamic_labels(menu_t *menu) {
    int g = -1, i = -1, l = -1;
    if (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) {
        path_t *p = path_clone_push(menu->browser.directory, menu->browser.entry->name);
        g = rom_config_get_image_view(p, 0);
        i = rom_config_get_image_view(p, 1);
        l = rom_config_get_image_view(p, 2);
        path_free(p);
    }
    snprintf(pa_grid_label,    sizeof(pa_grid_label),    "Grid: %s",    pa_type_name(g));
    snprintf(pa_inspect_label, sizeof(pa_inspect_label), "Inspect: %s", pa_type_name(i));
    snprintf(pa_load_label,    sizeof(pa_load_label),    "Load: %s",    pa_type_name(l));
    snprintf(fm_dir_label, sizeof(fm_dir_label), "Default dir: %s",
             menu->settings.default_directory ? menu->settings.default_directory : "/");
    if (fop_pending == 2)      snprintf(fm_paste_label, sizeof(fm_paste_label), "Move \"%s\" here", fop_name);
    else if (fop_pending == 1) snprintf(fm_paste_label, sizeof(fm_paste_label), "Copy \"%s\" here", fop_name);
    else                       fm_paste_label[0] = '\0';   /* nothing pending -> hidden separator */
    snprintf(fm_rb_set_label, sizeof(fm_rb_set_label), "%s",
             (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) ? "Set ROM boot" : "");
    snprintf(fm_rb_toggle_label, sizeof(fm_rb_toggle_label), "ROM boot: %s",
             menu->settings.rom_boot_enabled ? "Enabled" : "Disabled");
    snprintf(fm_rb_countdown_label, sizeof(fm_rb_countdown_label), "ROM boot countdown: %ds",
             menu->settings.rom_boot_countdown_sec);
    update_fav_label(menu);
    update_pin_label(menu);
    rebuild_history_popup(menu);
}

/* Capture the selected entry as the source for a pending Copy (1) or Move (2). */
static void file_op_set(menu_t *menu, int op) {
    if (!menu->browser.entry) return;
    if (fop_src) { path_free(fop_src); fop_src = NULL; }
    fop_src = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    strncpy(fop_name, menu->browser.entry->name, sizeof(fop_name) - 1);
    fop_name[sizeof(fop_name) - 1] = '\0';
    fop_pending = op;
    sound_play_effect(SFX_SETTING);
}
static void file_op_copy(menu_t *menu, void *arg) { (void)arg; file_op_set(menu, 1); }
static void file_op_move(menu_t *menu, void *arg) { (void)arg; file_op_set(menu, 2); }

/* Incremental copy state, driven per-frame from process() so the bar animates and B can cancel. */
static FILE *fcopy_in    = NULL;
static FILE *fcopy_out   = NULL;
static long  fcopy_total = 0;
static long  fcopy_done  = 0;

/* Close handles + release the pending-op state. keep_dest=false removes a partial/cancelled copy
   (the SOURCE is never touched, so cancelling is always safe). */
static void fcopy_cleanup(bool keep_dest) {
    if (fcopy_in)  { fclose(fcopy_in);  fcopy_in  = NULL; }
    if (fcopy_out) { fclose(fcopy_out); fcopy_out = NULL; }
    if (fcopy_dest) {
        if (!keep_dest) remove(path_get(fcopy_dest));
        path_free(fcopy_dest); fcopy_dest = NULL;
    }
    if (fop_src) { path_free(fop_src); fop_src = NULL; }
    fop_pending = 0;
    fcopy_phase = 0;
    fcopy_total = fcopy_done = 0;
}

/* Execute the pending op into the current directory. Move = f_rename (instant; libdragon does not
   hook libc rename(), so use fatfs directly). Copy hands off to process() for an incremental,
   cancellable copy with a progress bar. */
static void file_op_paste(menu_t *menu, void *arg) {
    (void)arg;
    if (!fop_pending || !fop_src || fcopy_phase) return;
    path_t *dest = path_clone_push(menu->browser.directory, fop_name);
    if (file_exists(path_get(dest))) {              /* never silently overwrite */
        menu_show_error(menu, "A file with that name already\nexists in this folder.");
        path_free(dest);
        return;
    }
    if (fop_pending == 1) {
        /* Copy: defer to process() so the draw loop animates the progress bar. */
        if (fcopy_dest) path_free(fcopy_dest);
        fcopy_dest  = dest;
        fcopy_phase = 1;
        return;
    }
    /* Move: instant same-volume rename. f_rename takes fatfs paths (strip the "sd:" prefix). */
    bool ok = (f_rename(strip_fs_prefix(path_get(fop_src)),
                        strip_fs_prefix(path_get(dest))) == FR_OK);
    path_free(dest);
    if (!ok) { menu_show_error(menu, "Move failed."); return; }
    path_free(fop_src); fop_src = NULL;             /* one paste per Copy/Move; re-Copy to paste again */
    fop_pending = 0;
    if (reload_directory(menu)) { menu->browser.valid = false; }
    sound_play_effect(SFX_ENTER);
}

/* Incremental copy, driven from process(). Phase 1: open + measure (draw 0% first). Phase 2: copy
   a capped budget per frame so the bar animates and B: Cancel stays responsive. */
static void file_copy_step(menu_t *menu) {
    if (fcopy_phase == 1) {
        fcopy_in  = fopen(path_get(fop_src), "rb");
        fcopy_out = fcopy_in ? fopen(path_get(fcopy_dest), "wb") : NULL;
        if (!fcopy_in || !fcopy_out) { fcopy_cleanup(false); menu_show_error(menu, "Copy failed (open)."); return; }
        fseek(fcopy_in, 0, SEEK_END); fcopy_total = ftell(fcopy_in); fseek(fcopy_in, 0, SEEK_SET);
        fcopy_done  = 0;
        fcopy_phase = 2;
        return;                                     /* one frame at 0% before the first chunk */
    }
    if (menu->actions.back) {                       /* B: Cancel -- safe, source is untouched */
        fcopy_cleanup(false);
        if (reload_directory(menu)) { menu->browser.valid = false; }
        sound_play_effect(SFX_EXIT);
        return;
    }
    static char buf[32768];
    long budget = 512 * 1024;                       /* per-frame cap keeps the bar/cancel responsive */
    while (budget > 0) {
        size_t n = fread(buf, 1, sizeof(buf), fcopy_in);
        if (n == 0) break;
        if (fwrite(buf, 1, n, fcopy_out) != n) {    /* write error (e.g. card full) */
            fcopy_cleanup(false);
            menu_show_error(menu, "Copy failed (write).");
            if (reload_directory(menu)) { menu->browser.valid = false; }
            return;
        }
        fcopy_done += (long)n; budget -= (long)n;
    }
    if (ferror(fcopy_in)) { fcopy_cleanup(false); menu_show_error(menu, "Copy failed (read)."); return; }
    if (feof(fcopy_in) || fcopy_done >= fcopy_total) {
        fcopy_cleanup(true);                        /* keep the finished copy */
        if (reload_directory(menu)) { menu->browser.valid = false; }
        sound_play_effect(SFX_ENTER);
    }
}

/* Cycle a per-game override through Default -> Front .. Logo -> Default. */
static void cycle_pg_image(menu_t *menu, int context, int row) {
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_ROM) return;
    pa_row_within = row;
    presents_action_fired = true;
    path_t *p = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    int cur = rom_config_get_image_view(p, context);
    int next = cur + 1;
    if (next > GRID_IMAGE_COUNT - 1) next = -1;   /* wrap back to Default */
    rom_config_override_image_view(p, context, next);
    path_free(p);
    update_dynamic_labels(menu);
}
static void cycle_pg_grid(menu_t *menu, void *arg)    { cycle_pg_image(menu, 0, (int)(intptr_t)arg); }
static void cycle_pg_inspect(menu_t *menu, void *arg) { cycle_pg_image(menu, 1, (int)(intptr_t)arg); }
static void cycle_pg_load(menu_t *menu, void *arg)    { cycle_pg_image(menu, 2, (int)(intptr_t)arg); }

/* "Presents As" parent: Region art (front/back) + per-game type overrides. */
static component_context_menu_t presents_as_parent_context_menu = { .list = {
    { .text = "Region art",   .submenu = &set_presents_as_browser_context_menu },
    { .text = pa_grid_label,    .action = cycle_pg_grid,    .arg = (void*)(intptr_t)1 },
    { .text = pa_inspect_label, .action = cycle_pg_inspect, .arg = (void*)(intptr_t)2 },
    { .text = pa_load_label,    .action = cycle_pg_load,    .arg = (void*)(intptr_t)3 },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Fav-flags cache: rebuilt once per directory change or favourite toggle, not every frame */
static bool *fav_cache     = NULL;
static int   fav_cache_n   = -1;
static int   fav_cache_sp  = -1;
/* fav_dirty declared earlier (before toggle_favorite) */

/* Set on first B press at root; second B press navigates to grid */
static bool root_b_primed = false;


/* B-hold state — module-level so view_browser_init can reset them on entry */
static int  b_hold = 0;
static bool b_multi_mode = false;
static bool b_multi_add  = false;
/* Ignore the first B release after entering the browser (or after a context menu
   closes) so B held in another mode/context doesn't ghost-pop the directory. */
static bool b_skip_release = false;
/* Tracks whether the context menu was open last frame to detect close transitions. */
static bool context_menu_was_active = false;

static void update_file_size_label(menu_t *menu); /* forward decl — defined after context menus */

/* Mark (add=true) or unmark every ROM entry in the inclusive index range as a
   favorite. Used by hold-B multi-select so C-button fast-scroll, which jumps the
   selection several rows per frame, still catches every entry it skips over. */
static void mark_range_favorite(menu_t *menu, int lo, int hi, bool add) {
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    if (lo < 0) lo = 0;
    if (hi >= menu->browser.entries) hi = menu->browser.entries - 1;
    for (int i = lo; i <= hi; i++) {
        if (!entry_is_favoritable(menu->browser.list[i].type)) continue;
        bool is_disk = (menu->browser.list[i].type == ENTRY_TYPE_DISK);
        path_t *p = path_clone_push(menu->browser.directory, menu->browser.list[i].name);
        int fi = -1;
        for (int k = 0; k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(p, f->primary_path)) { fi = k; break; }
        }
        if (add && fi < 0) {
            bookkeeping_favorite_add_nosave(&menu->bookkeeping, p, NULL,
                is_disk ? BOOKKEEPING_TYPE_DISK : BOOKKEEPING_TYPE_ROM);
            fav_dirty = true; fav_unsaved = true;
        } else if (!add && fi >= 0) {
            bookkeeping_favorite_remove_nosave(&menu->bookkeeping, fi);
            fav_dirty = true; fav_unsaved = true;
        }
        path_free(p);
    }
}

/* Boot the selected ROM straight away, bypassing the Game settings view. */
static void launch_game(menu_t *menu, void *arg) {
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_ROM) {
        return;
    }
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->load_pending.launch_rom = true;
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

/* Launch the selected 64DD disk. An UNLINKED expansion disk (E-prefix code -- it needs a base
   cartridge, e.g. the F-Zero X Expansion Kit) can't boot standalone, so OFFER TO LINK it (arm the
   link-pick: browse anywhere for the base) instead of black-screening -- mirrors the grid's
   launch_favorite. A disk already linked via a disclink override OR a combined favorite boots
   normally (load_disk resolves the base). */
static void launch_disk(menu_t *menu) {
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_DISK) return;
    /* Disk game code from a "NUD-CCCC-..." filename (real .ndd carry it verbatim). */
    char code[5] = "";
    const char *nud = strstr(menu->browser.entry->name, "NUD-");
    if (nud && strlen(nud) >= 8) { memcpy(code, nud + 4, 4); code[4] = '\0'; }
    if (code[0] == 'E') {                       /* expansion disk -> needs a base cartridge */
        path_t *dp = path_clone_push(menu->browser.directory, menu->browser.entry->name);
        bool linked = disclink_has(menu->storage_prefix, code);
        for (int k = 0; !linked && k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type == BOOKKEEPING_TYPE_DISK && f->primary_path &&
                path_are_match(dp, f->primary_path) && path_has_value(f->secondary_path)) linked = true;
        }
        path_free(dp);
        if (!linked) {                          /* not linked -> offer to link it */
            start_link_pick(menu, NULL);
            sound_play_effect(SFX_ENTER);
            return;
        }
    }
    menu->next_mode = MENU_MODE_LOAD_DISK;
    sound_play_effect(SFX_ENTER);
}

/* Context-menu wrapper: launch the selected 64DD disk (handles the expansion-disc link-pick). */
static void disk_launch_action(menu_t *menu, void *arg) { (void)arg; launch_disk(menu); }

/* Open the Game settings view (the former load-ROM screen) for the selected ROM. */
static void open_game_settings(menu_t *menu, void *arg) {
    if (!menu->browser.entry || menu->browser.entry->type != ENTRY_TYPE_ROM) {
        return;
    }
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    reopen_pending = true;
    menu->next_mode = MENU_MODE_LOAD_ROM;
}

static void toggle_file_size(menu_t *menu, void *arg) {
    menu->settings.show_file_size = !menu->settings.show_file_size;
    settings_save(&menu->settings);
    update_file_size_label(menu);
}

/* Hardware submenu — console/cart facilities. */
static component_context_menu_t hardware_context_menu = { .list = {
    { .text = "Controller Pak manager", .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_CONTROLLER_PAKFS) },
    { .text = "Time (RTC) settings",    .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_RTC) },
    { .text = "Flashcart information",  .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_FLASHCART) },
    { .text = "N64 information",        .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_SYSTEM_INFO) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* Clear all 64DD disc->base links (deletes the disclink files). The expansion-disc picker
   re-links on the next launch, so this is the "start fresh" reset for disc linking. */
static void clear_disc_links (menu_t *menu, void *arg) {
    (void)arg;
    disclink_clear_all(menu->storage_prefix);
    sound_play_effect(SFX_SETTING);
}

/* File management submenu — operations on the selected entry / directory. */
static component_context_menu_t file_management_context_menu = { .list = {
    { .text = "Show entry properties",            .action = show_properties },
    { .text = "Delete selected entry",            .action = delete_entry },
    { .text = "Copy selected entry",              .action = file_op_copy },
    { .text = "Move selected entry",              .action = file_op_move },
    { .text = "Rename selected entry",            .action = rename_entry },
    { .text = "Create folder",                    .action = create_folder },
    { .text = fm_paste_label,                     .action = file_op_paste },  /* "" = hidden until Copy/Move pending */
    { .text = "Clear all disc links (F-Zero X)",  .action = clear_disc_links },
    { .text = "Show file size",                   .action = toggle_file_size },
    { .text = "" },
    { .text = "Set current directory as default", .action = set_default_directory },
    { .text = fm_dir_label },   /* default directory (info only) — sits below the setting */
    { .text = "" },
    { .text = fm_rb_set_label,    .action = set_rom_boot },     /* "" = hidden for non-ROM entries */
    { .text = fm_rb_toggle_label, .action = toggle_rom_boot },
    { .text = fm_rb_countdown_label, .action = cycle_rom_boot_countdown },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t file_management_archive_context_menu = { .list = {
    { .text = "Show entry properties",            .action = show_properties },
    { .text = "Extract selected entry",           .action = extract_entry },
    { .text = "Show file size",                   .action = toggle_file_size },
    { .text = "" },
    { .text = "Set current directory as default", .action = set_default_directory },
    { .text = fm_dir_label },   /* default directory (info only) — sits below the setting */
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* "Extra" submenu — mirrors the grid's Extra: menu-wide settings/info, Hardware,
   and (for ROMs) per-game details. Non-ROM contexts use the variant without
   "Game details". */
static component_context_menu_t extra_context_menu = { .list = {
    { .text = "Menu Settings",    .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_SETTINGS_EDITOR) },
    { .text = "Menu Information", .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_CREDITS) },
    { .text = "Hardware",         .submenu = &hardware_context_menu },
    { .text = "Game details",     .action = open_game_settings },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t extra_norom_context_menu = { .list = {
    { .text = "Menu Settings",    .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_SETTINGS_EDITOR) },
    { .text = "Menu Information", .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_CREDITS) },
    { .text = "Hardware",         .submenu = &hardware_context_menu },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

/* ---- Fav / Unfav every ROM inside the selected folder ----
   Runs incrementally across frames so the "Working" popup can show a real
   progress bar: frame 1 shows the box, frame 2 enumerates the folder, then a
   few ROMs are (un)favourited per frame until done. */
static int      folder_fav_pending = 0;    /* +1 = fav, -1 = unfav, 0 = idle (set by action) */
static bool     folder_fav_working = false;/* operation in progress (drives the popup) */
static bool     folder_fav_scanned = false;/* enumeration complete */
static bool     folder_fav_add     = false;/* direction, latched at scan time */
static path_t  *folder_fav_path    = NULL;
static path_t **folder_fav_roms    = NULL; /* enumerated ROM paths */
static int      folder_fav_total   = 0;
static int      folder_fav_done    = 0;

static void folder_fav_begin(menu_t *menu, int dir) {
    if (folder_fav_path) path_free(folder_fav_path);
    if (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DIR) {
        /* A subfolder is selected: favorite the ROMs inside THAT folder. */
        folder_fav_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    } else {
        /* No folder selected (you're browsing inside one): favorite the CURRENT folder's ROMs. */
        folder_fav_path = path_clone(menu->browser.directory);
    }
    folder_fav_pending = dir;
    folder_fav_working = false;
    folder_fav_scanned = false;
    folder_fav_total   = 0;
    folder_fav_done    = 0;
    debugf("[FOLDERFAV] queued %s of '%s'\n", dir > 0 ? "FAV" : "UNFAV",
           folder_fav_path ? path_get(folder_fav_path) : "(null)");
}
static void fav_inside_folder(menu_t *menu, void *arg)   { (void)arg; folder_fav_begin(menu,  1); }
static void unfav_inside_folder(menu_t *menu, void *arg) { (void)arg; folder_fav_begin(menu, -1); }

/* Phase 1 (one frame): collect every non-hidden ROM path WITHOUT writing the SD.
   Writing favorites.ini mid-enumeration corrupts the FatFs iterator (re-yields
   entries → duplicates), so all paths are gathered first. */
static void folder_fav_scan(void) {
    folder_fav_add = (folder_fav_pending > 0);
    folder_fav_roms = NULL; folder_fav_total = 0; folder_fav_done = 0;
    if (!folder_fav_path) { folder_fav_scanned = true; return; }

    int cap = 0;
    dir_t info;
    int r = dir_findfirst(path_get(folder_fav_path), &info);
    while (r == 0) {
        if (info.d_type != DT_DIR &&
            (file_has_extensions(info.d_name, n64_rom_extensions) ||
             file_has_extensions(info.d_name, disk_extensions))) {     /* include .ndd 64DD disks */
            path_t *rom = path_clone_push(folder_fav_path, info.d_name);
            /* Skip the entries the browser hides — esp. macOS "._<name>.z64"
               AppleDouble sidecars that share the ROM extension. */
            if (path_is_hidden(rom)) {
                path_free(rom);
            } else {
                if (folder_fav_total >= cap) {
                    int nc = cap ? cap * 2 : 32;
                    path_t **g = realloc(folder_fav_roms, nc * sizeof(path_t *));
                    if (g) { folder_fav_roms = g; cap = nc; }
                }
                if (folder_fav_total < cap) folder_fav_roms[folder_fav_total++] = rom;
                else                         path_free(rom);
            }
        }
        r = dir_findnext(path_get(folder_fav_path), &info);
    }
    folder_fav_scanned = true;
    debugf("[FOLDERFAV] scan of '%s' found %d ROM(s)\n", path_get(folder_fav_path), folder_fav_total);
}

/* Phase 2: (un)favourite up to `chunk` enumerated ROMs (advances folder_fav_done). */
static void folder_fav_step(menu_t *menu, int chunk) {
    int end = folder_fav_done + chunk;
    if (end > folder_fav_total) end = folder_fav_total;
    for (; folder_fav_done < end; folder_fav_done++) {
        path_t *rom = folder_fav_roms[folder_fav_done];
        int fi = -1;
        for (int k = 0; k < FAVORITES_COUNT; k++) {
            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && f->primary_path &&
                path_are_match(rom, f->primary_path)) { fi = k; break; }
        }
        /* No-save: folder_fav_finish() persists once at the end. Per-item saving here
           would rewrite the whole favorites.ini each time → O(N^2) for a big folder. */
        if (folder_fav_add && fi < 0) {
            /* Pre-cache the ROM code (behind the progress bar) ONLY when A-Z sorting is on:
               that mode reads every game name up front on boot, so caching here avoids the
               bulk header read next boot (a big folder-fav with no codes is what made the
               first A-Z boot after take ~30 s). With A-Z off the grid resolves names lazily
               while scrolling, so skip it and keep folder-fav fast. insert_top -> index 0. */
            char code[5] = "";
            bool is_disk = file_has_extensions(path_last_get(rom), disk_extensions);
            if (!is_disk && menu->settings.always_sort_az) {   /* disks resolve lazily in the grid */
                rom_info_t qi;
                if (rom_info_load_quick(rom, &qi) == ROM_OK) {
                    memcpy(code, qi.game_code, 4); code[4] = '\0';
                    rom_info_free_meta(&qi);
                }
            }
            bookkeeping_favorite_add_nosave(&menu->bookkeeping, rom, NULL,
                is_disk ? BOOKKEEPING_TYPE_DISK : BOOKKEEPING_TYPE_ROM);
            if (code[0]) {
                memcpy(menu->bookkeeping.favorite_items[0].game_code, code, 4);
                menu->bookkeeping.favorite_items[0].game_code[4] = '\0';
            }
        } else if (!folder_fav_add && fi >= 0) {
            bookkeeping_favorite_remove_nosave(&menu->bookkeeping, fi);
        }
    }
}

/* Phase 3: free the path list, persist, and reset state. */
static void folder_fav_finish(menu_t *menu) {
    for (int j = 0; j < folder_fav_total; j++) path_free(folder_fav_roms[j]);
    free(folder_fav_roms); folder_fav_roms = NULL;
    bookkeeping_save(&menu->bookkeeping);
    if (folder_fav_path) { path_free(folder_fav_path); folder_fav_path = NULL; }
    debugf("[FOLDERFAV] done (%d processed)\n", folder_fav_done);
    folder_fav_total = folder_fav_done = 0;
    folder_fav_pending = 0;
    folder_fav_working = false;
    folder_fav_scanned = false;
    fav_dirty = true;
}

/* Abort (B during the operation): discard the in-RAM partial and reset. step() uses the
   _nosave add/remove, so the SD still holds the pre-operation favorites -- re-reading them
   drops every partial change (full undo, nothing committed). clear_list is now free-safe so
   this reload doesn't leak the current list. */
static void folder_fav_cancel(menu_t *menu) {
    for (int j = 0; j < folder_fav_total; j++) path_free(folder_fav_roms[j]);
    free(folder_fav_roms); folder_fav_roms = NULL;
    bookkeeping_load(&menu->bookkeeping);
    if (folder_fav_path) { path_free(folder_fav_path); folder_fav_path = NULL; }
    folder_fav_total = folder_fav_done = 0;
    folder_fav_pending = 0;
    folder_fav_working = false;
    folder_fav_scanned = false;
    fav_dirty = true;
}

/* Draw the progress popup: a tight "Working" box with the title near the top and
   a slim progress bar below it. Hand-laid out (rather than via messagebox) so the
   title and bar sit with even padding instead of the text floating low. */
static void folder_fav_popup_draw(void) {
    float prog = folder_fav_total > 0 ? (float)folder_fav_done / (float)folder_fav_total : 0.0f;
    char *title = folder_fav_add ? "Favoriting folder\nB: Cancel" : "Unfavoriting folder\nB: Cancel";
    int nbytes = (int)strlen(title);

    rdpq_paragraph_t *p = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width        = MESSAGEBOX_MAX_WIDTH,
        .height       = VISIBLE_AREA_HEIGHT,
        .align        = ALIGN_CENTER,
        .valign       = VALIGN_TOP,
        .wrap         = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, title, &nbytes);

    int txt_h = p->bbox.y1 - p->bbox.y0;
    int box_w = p->bbox.x1 - p->bbox.x0 + MESSAGEBOX_MARGIN;
    if (box_w < 220) box_w = 220;       /* keep room for the bar */

    const int pad = 14, gap = 12, bar_h = 12;
    int box_h   = pad + txt_h + gap + bar_h + pad;
    int box_top = DISPLAY_CENTER_Y - box_h / 2;

    ui_components_dialog_draw(box_w, box_h);
    rdpq_paragraph_render(p, DISPLAY_CENTER_X - MESSAGEBOX_MAX_WIDTH / 2, box_top + pad);
    rdpq_paragraph_free(p);

    int bw = box_w - 40;
    int x0 = DISPLAY_CENTER_X - bw / 2;
    int x1 = DISPLAY_CENTER_X + bw / 2;
    int y0 = box_top + pad + txt_h + gap;
    int y1 = y0 + bar_h;
    ui_components_border_draw(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    ui_components_progressbar_draw_rainbow(x0, y0, x1, y1, prog);
}

/* Progress popup for the incremental file copy (mirrors the folder-fav popup). */
static void fcopy_popup_draw(void) {
    float prog = fcopy_total > 0 ? (float)fcopy_done / (float)fcopy_total : 0.0f;
    char title[300];
    snprintf(title, sizeof(title), "Copying %s\nB: Cancel", fop_name);
    int nbytes = (int)strlen(title);

    rdpq_paragraph_t *p = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width        = MESSAGEBOX_MAX_WIDTH,
        .height       = VISIBLE_AREA_HEIGHT,
        .align        = ALIGN_CENTER,
        .valign       = VALIGN_TOP,
        .wrap         = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, title, &nbytes);

    int txt_h = p->bbox.y1 - p->bbox.y0;
    int box_w = p->bbox.x1 - p->bbox.x0 + MESSAGEBOX_MARGIN;
    if (box_w < 220) box_w = 220;

    const int pad = 14, gap = 12, bar_h = 12;
    int box_h   = pad + txt_h + gap + bar_h + pad;
    int box_top = DISPLAY_CENTER_Y - box_h / 2;

    ui_components_dialog_draw(box_w, box_h);
    rdpq_paragraph_render(p, DISPLAY_CENTER_X - MESSAGEBOX_MAX_WIDTH / 2, box_top + pad);
    rdpq_paragraph_free(p);

    int bw = box_w - 40;
    int x0 = DISPLAY_CENTER_X - bw / 2;
    int x1 = DISPLAY_CENTER_X + bw / 2;
    int y0 = box_top + pad + txt_h + gap;
    int y1 = y0 + bar_h;
    ui_components_border_draw(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    ui_components_progressbar_draw_rainbow(x0, y0, x1, y1, prog);
}

/* ROM-specific "More" context menu — opened when a ROM is selected. Order mirrors
   the grid's universal menu; File management is the Files-only extra (no File
   Browser entry — we're already in the browser). */
static component_context_menu_t more_rom_context_menu = {
    .list = {
        { .text = "Launch",               .action = launch_game },
        { .text = fav_toggle_label,       .action = toggle_favorite },
        { .text = "Fav inside folder",    .action = fav_inside_folder },    /* targets the CURRENT folder (you're inside it) */
        { .text = "Unfav inside folder",  .action = unfav_inside_folder },
        /* "Link disc (64DD)" (the ROM->disc grid picker, link_disc.c DISC mode) is RETIRED for
           now per user: disc linking is done disc-side via the cart-picker instead. The picker
           code is kept in link_disc.c for the future (someone may want it).
        { .text = "Link disc (64DD)",     .action = set_menu_next_mode, .arg = (void *)(MENU_MODE_LINK_DISC) }, */
        { .text = "" },                   /* separator row */
        { .text = "Game Look",            .submenu = &presents_as_parent_context_menu },
        { .text = "" },                   /* separator row */
        { .text = "Extra",                .submenu = &extra_context_menu },
        { .text = "" },                   /* separator row */
        { .text = "History",              .submenu = &history_popup_menu },
        { .text = "File management",      .submenu = &file_management_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

/* "More" menu for a 64DD disk (.ndd): Launch handles the expansion-disc link-pick; a disk is
   favoritable just like a ROM (toggle_favorite stores BOOKKEEPING_TYPE_DISK). */
static component_context_menu_t more_disk_context_menu = {
    .list = {
        { .text = "Launch",               .action = disk_launch_action },
        { .text = fav_toggle_label,       .action = toggle_favorite },
        { .text = "Fav inside folder",    .action = fav_inside_folder },
        { .text = "Unfav inside folder",  .action = unfav_inside_folder },
        { .text = "" },                   /* separator row */
        { .text = "Extra",                .submenu = &extra_context_menu },
        { .text = "" },                   /* separator row */
        { .text = "History",              .submenu = &history_popup_menu },
        { .text = "File management",      .submenu = &file_management_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

/* Generic "More" context menu — for non-ROM entries (dirs, images, etc.).
   The folder-wide favourite actions sit at the top and only act on directories. */
static component_context_menu_t more_context_menu = {
    .list = {
        { .text = "Fav inside folder",    .action = fav_inside_folder },
        { .text = "Unfav inside folder",  .action = unfav_inside_folder },
        { .text = pin_toggle_label,       .action = toggle_pin_folder },
        { .text = "" },                   /* separator row */
        { .text = "Extra",                .submenu = &extra_norom_context_menu },
        { .text = "" },                   /* separator row */
        { .text = "History",              .submenu = &history_popup_menu },
        { .text = "File management",      .submenu = &file_management_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static component_context_menu_t more_archive_context_menu = {
    .list = {
        { .text = "Extra",                .submenu = &extra_norom_context_menu },
        { .text = "" },                   /* separator row */
        { .text = "History",              .submenu = &history_popup_menu },
        { .text = "File management",      .submenu = &file_management_archive_context_menu },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void update_file_size_label(menu_t *menu) {
    const char *label = menu->settings.show_file_size ? "Hide file size" : "Show file size";
    for (int i = 0; file_management_context_menu.list[i].text != NULL; i++) {
        if (file_management_context_menu.list[i].action == toggle_file_size) {
            file_management_context_menu.list[i].text = label; break;
        }
    }
    for (int i = 0; file_management_archive_context_menu.list[i].text != NULL; i++) {
        if (file_management_archive_context_menu.list[i].action == toggle_file_size) {
            file_management_archive_context_menu.list[i].text = label; break;
        }
    }
}

/* True when we're "not in a folder" — i.e. a B press would not navigate any
   further up. Tests the raw buffer string (independent of path->root and
   path_is_root, which were unreliable here): after stripping the "xx:/" storage
   prefix only a leading "/" (or nothing) remains at the top level. */
static bool browser_at_root(menu_t *menu) {
    char *rest = strip_fs_prefix(path_get(menu->browser.directory));
    while (rest && (*rest == '/')) {
        rest++;
    }
    return (rest == NULL) || (*rest == '\0');
}

/* ---- File-popup draw (called when browser_popup_mode). Fixed min height. ---- */
#define POPUP_W         (DISPLAY_WIDTH * 2 / 3)   /* ~2/3 of the screen width */
#define POPUP_LINE_H     22
#define POPUP_MAX_VIS    12   /* fixed visible rows → constant box height */
#define POPUP_PAD_X      16
#define POPUP_PAD_Y      12
#define POPUP_HDR_H      28   /* path breadcrumb header inside the box */

/* Is this entry a ROM currently in the favorites (grid) list? */
static bool popup_entry_is_fav(menu_t *menu, entry_t *e) {
    if (!e || !entry_is_favoritable(e->type)) return false;   /* ROM or .ndd disk */
    path_t *p = path_clone_push(menu->browser.directory, e->name);
    bool f = false;
    for (int k = 0; k < FAVORITES_COUNT; k++) {
        bookkeeping_item_t *fi = &menu->bookkeeping.favorite_items[k];
        if (fi->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(p, fi->primary_path)) {
            f = true; break;
        }
    }
    path_free(p);
    return f;
}

/* Compact file size for the popup's left gutter (e.g. "12M", "640K", "512B"). */
static void fmt_size_compact(int64_t b, char *out, size_t n) {
    if      (b >= 1024LL*1024*1024) snprintf(out, n, "%lldG", (long long)(b / (1024LL*1024*1024)));
    else if (b >= 1024*1024)        snprintf(out, n, "%lldM", (long long)(b / (1024*1024)));
    else if (b >= 1024)             snprintf(out, n, "%lldK", (long long)(b / 1024));
    else                            snprintf(out, n, "%lldB", (long long)b);
}

static void draw_file_popup(menu_t *menu) {
    int n = menu->browser.entries;

    /* Reset the horizontal name-scroll whenever the selection moves to a new file. */
    if (menu->browser.selected != popup_hscroll_sel) {
        popup_hscroll = 0;
        popup_hscroll_sel = menu->browser.selected;
    }

    /* Clamp scroll so selected entry is always visible */
    if (popup_scroll > menu->browser.selected)
        popup_scroll = menu->browser.selected;
    if (popup_scroll < menu->browser.selected - POPUP_MAX_VIS + 1)
        popup_scroll = menu->browser.selected - POPUP_MAX_VIS + 1;
    /* ...and never let the window run PAST the end of the list, or the rows below the last entry
       render blank -- the "only the selected row shows after backing out of a directory" bug:
       popup_scroll was stale-high from the child dir and the "scroll <= selected" clamp above
       pinned the window TOP to the selected row, so a folder near the end showed alone at the top.
       This MUST be unconditional: for a SHORT list (n <= POPUP_MAX_VIS) it goes <= 0 and the floor
       below pins it to 0 (every entry from the top). The earlier `n > POPUP_MAX_VIS` guard is
       exactly what left short lists / the last entry broken -- floor goes LAST. */
    if (popup_scroll > n - POPUP_MAX_VIS)
        popup_scroll = n - POPUP_MAX_VIS;
    if (popup_scroll < 0) popup_scroll = 0;

    /* Fixed box height (constant POPUP_MAX_VIS rows) so the popup never shrinks to
       a tiny box that collides with the grid art behind it. */
    int box_h = POPUP_MAX_VIS * POPUP_LINE_H + POPUP_HDR_H + POPUP_PAD_Y * 2;

    ui_components_dialog_draw(POPUP_W, box_h);

    int x0 = DISPLAY_CENTER_X - POPUP_W / 2;
    int y0 = DISPLAY_CENTER_Y - box_h / 2;
    int rx = x0 + POPUP_PAD_X;
    int rw = POPUP_W - POPUP_PAD_X * 2;

    /* Path breadcrumb header inside the box (trimmed from the left when long), with a
       favourites counter "X/128 *" right-aligned on the same row. */
    {
        int hy = y0 + POPUP_PAD_Y + 14;
        if (link_pick_active) {
            /* Link-pick banner replaces the breadcrumb: what we're pairing + which type to pick. */
            char banner[96];
            snprintf(banner, sizeof banner, "LINK %s -> pick its %s",
                     link_pick_name, link_pick_src_is_disc ? "base ROM" : "disc");
            rdpq_text_print(
                &(rdpq_textparms_t){ .style_id = STL_YELLOW, .width = rw, .align = ALIGN_LEFT, .wrap = WRAP_ELLIPSES },
                FNT_DEFAULT, rx, hy, banner);
        } else {
            char buf[128], disp[64];
            char *rest = strip_fs_prefix(path_get(menu->browser.directory));
            int plen = snprintf(buf, sizeof(buf), "SD:%s", (rest && rest[0]) ? rest : "/");
            if (plen > 52) snprintf(disp, sizeof(disp), "...%s", buf + plen - 49);
            else           memcpy(disp, buf, (size_t)plen + 1);
            rdpq_text_print(
                &(rdpq_textparms_t){ .style_id = STL_GRAY, .width = rw - 90, .align = ALIGN_LEFT, .wrap = WRAP_ELLIPSES },
                FNT_DEFAULT, rx, hy, disp);
            int favn = 0;
            for (int k = 0; k < FAVORITES_COUNT; k++)
                if (menu->bookkeeping.favorite_items[k].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) favn++;
            rdpq_text_printf(
                &(rdpq_textparms_t){ .style_id = STL_YELLOW, .width = rw, .align = ALIGN_RIGHT },
                FNT_DEFAULT, rx, hy, "%d/%d *", favn, FAVORITES_COUNT);
        }
        ui_components_box_draw(rx, y0 + POPUP_PAD_Y + POPUP_HDR_H - 6,
                               x0 + POPUP_W - POPUP_PAD_X, y0 + POPUP_PAD_Y + POPUP_HDR_H - 5,
                               RGBA32(0x44, 0x44, 0x44, 0xFF));
    }

    int list_top = y0 + POPUP_PAD_Y + POPUP_HDR_H;

    if (n <= 0) {
        rdpq_text_print(
            &(rdpq_textparms_t){ .style_id = STL_GRAY, .width = rw, .align = ALIGN_LEFT },
            FNT_DEFAULT, rx, list_top + POPUP_LINE_H - 4, "Empty directory");
    } else {
        /* Selected-row highlight (drawn first, so text sits on top). */
        int sel_row = menu->browser.selected - popup_scroll;
        if (sel_row >= 0 && sel_row < POPUP_MAX_VIS) {
            int hl_y = list_top + sel_row * POPUP_LINE_H;
            ui_components_box_draw(x0 + 1, hl_y, x0 + POPUP_W - 1, hl_y + POPUP_LINE_H,
                                   CONTEXT_MENU_HIGHLIGHT_COLOR);
        }

        /* One text print per row → highlight and rows always line up; favourite
           star is right-aligned in its own column. Names white, dirs yellow. */
        for (int i = 0; i < POPUP_MAX_VIS; i++) {
            int ei = popup_scroll + i;
            if (ei >= n) break;
            entry_t *e = &menu->browser.list[ei];
            int row_top = list_top + i * POPUP_LINE_H;
            bool fav;
            if (pop_multi && ei >= pop_multi_lo && ei <= pop_multi_hi && entry_is_favoritable(e->type)) {
                fav = pop_multi_add;   /* pending sweep state — shown instantly */
            } else {
                fav = (e->type != ENTRY_TYPE_DIR) && popup_entry_is_fav(menu, e);
            }
            menu_font_style_t st = (e->type == ENTRY_TYPE_DIR) ? STL_YELLOW : STL_DEFAULT;
            /* "Show file size" reserves a left gutter; favourited entries show their size there. */
            int sz_gut = menu->settings.show_file_size ? 46 : 0;
            int nx = rx + sz_gut;                                 /* name x (shifted past the gutter) */
            if (ei == menu->browser.selected) {
                /* Selected row: horizontally scrollable (C/D-pad left-right), clipped. */
                int name_w = (int)strlen(e->name) * 9;            /* rough proportional est. */
                int avail  = rw - 16 - sz_gut;
                int maxsc  = name_w > avail ? name_w - avail : 0;
                if (popup_hscroll > maxsc) popup_hscroll = maxsc;
                rdpq_set_scissor(nx, row_top, nx + avail, row_top + POPUP_LINE_H);
                rdpq_text_printf(
                    &(rdpq_textparms_t){ .style_id = st, .height = POPUP_LINE_H,
                                         .align = ALIGN_LEFT, .valign = VALIGN_CENTER },
                    FNT_DEFAULT, nx - popup_hscroll, row_top, "%s", e->name);
                rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            } else {
                rdpq_text_printf(
                    &(rdpq_textparms_t){
                        .style_id = st,
                        .width = rw - 16 - sz_gut, .height = POPUP_LINE_H,
                        .align = ALIGN_LEFT, .valign = VALIGN_CENTER, .wrap = WRAP_ELLIPSES,
                    },
                    FNT_DEFAULT, nx, row_top, "%s", e->name);
            }
            /* File size in the left gutter for EVERY file (not just favourites) so the toggle is a
               true global on/off in all folders. Directories have no meaningful size -> skip. */
            if (sz_gut && e->size > 0 && e->type != ENTRY_TYPE_DIR) {
                char szb[16];
                fmt_size_compact(e->size, szb, sizeof szb);
                rdpq_text_print(
                    &(rdpq_textparms_t){ .style_id = STL_GRAY, .width = sz_gut - 4, .height = POPUP_LINE_H,
                                         .align = ALIGN_LEFT, .valign = VALIGN_CENTER },
                    FNT_DEFAULT, rx, row_top, szb);
            }
            if (fav) {
                rdpq_text_print(
                    &(rdpq_textparms_t){ .style_id = STL_YELLOW, .width = rw, .height = POPUP_LINE_H,
                                         .align = ALIGN_RIGHT, .valign = VALIGN_CENTER },
                    FNT_DEFAULT, rx, row_top, "*");
            }
        }
        /* (Scroll ▲/▼ indicators removed — they overlapped the favorite stars.) */
    }

    /* Toolbar at the bottom action bar, spread across the full width like the grid.
       Right side mirrors the grid's "C: <fn>     Z: Menu" convention (C fast-scrolls
       the list / long names here). "Fav (hold)" only applies to ROMs; else "Up". */
    entry_type_t et = menu->browser.entry ? menu->browser.entry->type : ENTRY_TYPE_OTHER;
    if (link_pick_active) {
        /* Link-pick: A links the complementary-type file (or opens folders), B goes up, Z cancels. */
        const char *a;
        if (et == ENTRY_TYPE_DIR || et == ENTRY_TYPE_ARCHIVE) a = "A: Open";
        else if (link_pick_src_is_disc ? (et == ENTRY_TYPE_ROM) : (et == ENTRY_TYPE_DISK)) a = "A: Link";
        else a = "";
        ui_components_actions_bar_buttons_draw(a, "B: Up", "", "C: Scroll", "Z: Cancel");
        return;
    }
    bool over_rom = (et == ENTRY_TYPE_ROM);
    /* A boots ROMs/disks/emulators ("Launch") but only opens folders and other files. */
    bool launches = over_rom || et == ENTRY_TYPE_DISK || et == ENTRY_TYPE_EMULATOR;
    if (over_rom) {
        ui_components_actions_bar_buttons_draw("A: Launch", "B: Fav (hold)", "S: Grid", "C: Scroll", "Z: Menu");
    } else {
        ui_components_actions_bar_buttons_draw(launches ? "A: Launch" : "A: Open", "B: Up", "S: Grid", "C: Scroll", "Z: Menu");
    }
}

static void process (menu_t *menu) {
    /* On-screen keyboard (Rename / Create folder): when open it owns all input. */
    if (kb_mode) { kb_process(menu); return; }

    /* Deferred file copy: drive it here (not in the menu action) so the blocking copy never
       runs alongside a mid-action display_get(). Holds all input until it finishes. */
    if (fcopy_phase) { file_copy_step(menu); return; }

    /* Folder-wide favourite: incremental so the popup can animate a progress bar.
       Frame 1: show the box. Frame 2: enumerate. Then a few ROMs per frame. */
    if (folder_fav_pending != 0) {
        if (!folder_fav_working) { folder_fav_working = true; return; }
        if (!folder_fav_scanned) { folder_fav_scan(); return; }   /* draw 0% before mutating */
        if (menu->actions.back) { folder_fav_cancel(menu); return; }   /* B = abort (full undo) */
        folder_fav_step(menu, 4);
        if (folder_fav_done >= folder_fav_total) folder_fav_finish(menu);
        return;
    }

    /* === POPUP MODE: file browser rendered as a rainbow popup over the grid ===
       Controls: A launch/open · S More menu · B up (double-B at root → grid) ·
       R/Z → grid · C-buttons fast-scroll. When the More menu is open we fall
       through to the shared context-menu handling below. */
    if (browser_popup_mode) {
        component_context_menu_t *pcm =
            menu->browser.archive ? &more_archive_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) ? &more_rom_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DISK) ? &more_disk_context_menu :
            &more_context_menu;

        if (pcm->row_selected < 0) {
            /* S or Z: back to the grid (R now opens the menu). */
            if (menu->actions.settings || menu->actions.lz_context) {
                popup_scroll = 0;
                menu->next_mode = MENU_MODE_GAMES_GRID;
                sound_play_effect(SFX_EXIT);
                return;
            }

            /* Up/Down (C-buttons fast-scroll via go_fast). */
            if (menu->browser.entries > 1) {
                int speed = menu->actions.go_fast ? 10 : 1;
                bool can_wrap = (b_hold == 0 && !pop_multi);   /* not mid hold-B multi-select */
                if (menu->actions.go_up) {
                    if (can_wrap && menu->browser.selected == 0)
                        menu->browser.selected = menu->browser.entries - 1;
                    else { menu->browser.selected -= speed; if (menu->browser.selected < 0) menu->browser.selected = 0; }
                    sound_play_effect(SFX_CURSOR);
                } else if (menu->actions.go_down) {
                    if (can_wrap && menu->browser.selected == menu->browser.entries - 1)
                        menu->browser.selected = 0;
                    else { menu->browser.selected += speed; if (menu->browser.selected >= menu->browser.entries) menu->browser.selected = menu->browser.entries - 1; }
                    sound_play_effect(SFX_CURSOR);
                }
                menu->browser.entry = &menu->browser.list[menu->browser.selected];
            }

            /* Left/Right (incl. C-left/right): scroll the selected long name. */
            if (menu->actions.go_left)  { popup_hscroll -= 24; if (popup_hscroll < 0) popup_hscroll = 0; }
            if (menu->actions.go_right) { popup_hscroll += 24; }

            /* Link-pick (64DD): navigate freely (handled above); A enters a folder or, on the
               complementary-type file, links + boots; B goes up a directory; R cancels the link.
               Swallows the normal A/S/B/R handlers below for the duration. */
            if (link_pick_active) {
                if (menu->actions.options) { cancel_link_pick(); sound_play_effect(SFX_EXIT); return; }
                if (menu->actions.back) {
                    if (!browser_at_root(menu)) {
                        if (pop_directory(menu)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open last directory"); }
                        else popup_scroll = 0;
                        sound_play_effect(SFX_EXIT);
                    }
                    return;
                }
                if (menu->actions.enter && menu->browser.entry) {
                    int t = menu->browser.entry->type;
                    if (t == ENTRY_TYPE_DIR) {
                        if (push_directory(menu, menu->browser.entry->name, false)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open next directory"); }
                        else popup_scroll = 0;
                        sound_play_effect(SFX_ENTER);
                    } else if (t == ENTRY_TYPE_ARCHIVE) {
                        if (push_directory(menu, menu->browser.entry->name, true)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open file archive"); }
                        sound_play_effect(SFX_ENTER);
                    } else if (link_pick_src_is_disc ? (t == ENTRY_TYPE_ROM) : (t == ENTRY_TYPE_DISK)) {
                        finish_link_pick(menu);
                    } else {
                        sound_play_effect(SFX_ERROR);   /* not the type we're linking to */
                    }
                }
                return;
            }

            /* Z: open the More menu. Works even in an EMPTY folder (no entry) -- pcm falls back to
               more_context_menu (Extra / History / File management), so Paste, Set-default-dir,
               etc. are reachable to drop a copied/moved file into an empty directory. */
            if (menu->actions.options) {
                update_dynamic_labels(menu);
                ui_components_context_menu_show(pcm);
                sound_play_effect(SFX_SETTING);
                return;
            }

            if (!browser_at_root(menu)) root_b_primed = false;

            /* B: hold on a ROM toggles favorite; hold + sweep marks a whole range
               (committed on release so it never stutters); a short tap goes up a
               directory (double-tap at root → grid). Acting on release keeps a hold
               from also popping the directory. */
            {
                bool can_fav = menu->browser.entry && entry_is_favoritable(menu->browser.entry->type);
                joypad_buttons_t bh = {0};
                JOYPAD_PORT_FOREACH(port) { bh = joypad_get_buttons_held(port); if (bh.raw) break; }
                if (bh.b) {
                    if (b_hold == 0) {   /* first frame of the hold — remember the start */
                        pop_start_fav = can_fav && popup_entry_is_fav(menu, menu->browser.entry);
                    }
                    b_hold++;
                    /* Sweep with the D-pad / C-buttons → mark a range, visually only. */
                    if (can_fav && b_hold >= 2 && (menu->actions.go_up || menu->actions.go_down)) {
                        if (!pop_multi) {
                            pop_multi     = true;
                            pop_multi_add = !pop_start_fav;
                            pop_multi_lo  = pop_multi_hi = menu->browser.selected;
                        }
                        if (menu->browser.selected < pop_multi_lo) pop_multi_lo = menu->browser.selected;
                        if (menu->browser.selected > pop_multi_hi) pop_multi_hi = menu->browser.selected;
                    }
                    /* Single toggle on a plain hold (no sweep). */
                    if (can_fav && b_hold == 20 && !pop_multi) {
                        toggle_favorite(menu, NULL);
                        bookkeeping_save(&menu->bookkeeping);
                        sound_play_effect(SFX_ENTER);
                    }
                    return;
                }
                if (pop_multi) {
                    /* Commit the swept range to bookkeeping once, on release. This
                       must take priority over b_skip_release, otherwise the very
                       first sweep after opening the popup would be discarded. */
                    mark_range_favorite(menu, pop_multi_lo, pop_multi_hi, pop_multi_add);
                    bookkeeping_save(&menu->bookkeeping);
                    sound_play_effect(SFX_ENTER);
                    pop_multi = false;
                    b_hold = 0;
                    b_skip_release = false;
                    return;
                }
                if (b_skip_release) {
                    b_skip_release = false;
                } else if (b_hold > 0 && b_hold < 20) {
                    if (browser_at_root(menu)) {
                        if (root_b_primed) {
                            root_b_primed = false; popup_scroll = 0;
                            menu->next_mode = MENU_MODE_GAMES_GRID;
                            sound_play_effect(SFX_CURSOR);
                        } else {
                            root_b_primed = true;
                        }
                    } else {
                        root_b_primed = false;
                        if (pop_directory(menu)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open last directory"); }
                        sound_play_effect(SFX_EXIT);
                    }
                    b_hold = 0;
                    return;
                }
                b_hold = 0;
            }

            /* A: launch/open whatever the cursor is on. */
            if (menu->actions.enter && menu->browser.entry) {
                entry_t *e = menu->browser.entry;
                switch (e->type) {
                    case ENTRY_TYPE_DIR:
                        if (push_directory(menu, e->name, false)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open next directory"); }
                        else popup_scroll = 0;
                        sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_ARCHIVE:
                        if (push_directory(menu, e->name, true)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open file archive"); }
                        sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_ROM:      launch_game(menu, NULL); sound_play_effect(SFX_LAUNCH); break;
                    case ENTRY_TYPE_DISK:     launch_disk(menu); break;
                    case ENTRY_TYPE_EMULATOR: menu->next_mode = MENU_MODE_LOAD_EMULATOR; sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_IMAGE:    menu->next_mode = MENU_MODE_IMAGE_VIEWER;  sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_MUSIC:    menu->next_mode = MENU_MODE_MUSIC_PLAYER;  sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_TEXT:     menu->next_mode = MENU_MODE_TEXT_VIEWER;   sound_play_effect(SFX_ENTER); break;
                    case ENTRY_TYPE_ARCHIVED: menu->next_mode = MENU_MODE_EXTRACT_FILE;  sound_play_effect(SFX_ENTER); break;
                    default:                  menu->next_mode = MENU_MODE_FILE_INFO;     sound_play_effect(SFX_ENTER); break;
                }
            }
            return;
        }
        /* else: More menu open — fall through to shared context-menu handling. */
    }
    /* === END POPUP MODE === */

    component_context_menu_t *active_cm =
        menu->browser.archive ? &more_archive_context_menu :
        (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) ? &more_rom_context_menu :
        (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DISK) ? &more_disk_context_menu :
        &more_context_menu;

    /* Detect context menu close so B held during the close doesn't ghost-pop later */
    bool cm_active_now = (active_cm->row_selected >= 0);
    if (context_menu_was_active && !cm_active_now) {
        b_hold = 0;
        b_skip_release = true;
        if (presents_action_fired) {
            presents_action_fired = false;
            /* Reopen the chain to the Presents As parent submenu at the saved row. */
            int pa_parent_row = 0;
            for (int i = 0; active_cm->list[i].text != NULL; i++) {
                if (active_cm->list[i].submenu == &presents_as_parent_context_menu) {
                    pa_parent_row = i; break;
                }
            }
            ui_components_context_menu_show(active_cm);
            active_cm->row_selected = pa_parent_row;
            active_cm->submenu = &presents_as_parent_context_menu;
            presents_as_parent_context_menu.row_selected = pa_row_within;
            presents_as_parent_context_menu.parent = active_cm;
            cm_active_now = true;
        }
    }
    context_menu_was_active = cm_active_now;

    /* Snapshot the open chain so an action that launches a view (Menu Settings,
       Hardware items, Game details, …) can restore the exact cursor on return. */
    if (cm_active_now) {
        capture_reopen_chain(active_cm);
    }

    if (ui_components_context_menu_process(menu, active_cm)) {
        return;
    }

    /* In popup mode the only full-browser code we share is the context-menu
       handling above; never fall into the file-list navigation / hold-B logic. */
    if (browser_popup_mode) {
        return;
    }

    if (!browser_at_root(menu)) {
        root_b_primed = false;
    }

    /* ===== DOUBLE-B FAILSAFE =====
       Pure edge-based double-tap of B at the top level → Grid. No timeout: once
       primed, the next B (whenever it comes, even after moving around) exits.
       Only leaving the root clears the prime (handled just above). */
    if (!cm_active_now && menu->actions.back && browser_at_root(menu)) {
        if (root_b_primed) {
            root_b_primed = false;
            menu->next_mode = MENU_MODE_GAMES_GRID;
            sound_play_effect(SFX_CURSOR);
            return;
        }
        root_b_primed = true;   /* drives the "B again: Grid" hint */
    }

    /* ===== SINGLE-B BACK-OUT =====
       Any B press in a sub-directory whose selected entry is NOT favouritable pops up one
       level immediately (covers empty folders and folders that only contain sub-folders,
       where hold-to-favorite doesn't apply). FAVOURITABLE entries (ROMs AND .ndd disks) keep
       hold-to-favorite, with a short tap popping (handled in the B block below). Gating this on
       == ENTRY_TYPE_ROM was the real reason .ndd disks couldn't be favourited: B on a disk
       popped the directory here before the hold-to-favorite block downstream ever ran. */
    if (!cm_active_now && menu->actions.back && !browser_at_root(menu) && !b_multi_mode) {
        bool entry_favoritable = menu->browser.entry && entry_is_favoritable(menu->browser.entry->type);
        if (!entry_favoritable) {
            if (pop_directory(menu)) {
                menu->browser.valid = false;
                menu_show_error(menu, "Couldn't open last directory");
            }
            sound_play_effect(SFX_EXIT);
            b_hold = 0;
            return;
        }
    }

    int scroll_speed = menu->actions.go_fast ? 10 : 1;

    /* Remember the selection before navigation so hold-B multi-select can mark
       every entry the cursor travels over this frame (including C-scroll jumps). */
    int sel_before_nav = menu->browser.selected;

    if (menu->browser.entries > 1) {
        /* Wrap top<->bottom on a plain single press at the edge. Suppressed while holding B
           (b_hold/pop_multi) so a multi-select sweep can't mark the whole list at once. */
        bool can_wrap = (b_hold == 0 && !pop_multi);
        if (menu->actions.go_up) {
            if (can_wrap && menu->browser.selected == 0) {
                menu->browser.selected = menu->browser.entries - 1;
            } else {
                menu->browser.selected -= scroll_speed;
                if (menu->browser.selected < 0) menu->browser.selected = 0;
            }
            sound_play_effect(SFX_CURSOR);
        } else if (menu->actions.go_down) {
            if (can_wrap && menu->browser.selected == menu->browser.entries - 1) {
                menu->browser.selected = 0;
            } else {
                menu->browser.selected += scroll_speed;
                if (menu->browser.selected >= menu->browser.entries) menu->browser.selected = menu->browser.entries - 1;
            }
            sound_play_effect(SFX_CURSOR);
        }
        menu->browser.entry = &menu->browser.list[menu->browser.selected];
    }

    if (menu->actions.enter && menu->browser.entry) {
        if (link_pick_active) {
            /* Link-pick mode: navigate freely; A on the complementary-type file links + boots. */
            int t = menu->browser.entry->type;
            if (t == ENTRY_TYPE_DIR) {
                if (push_directory(menu, menu->browser.entry->name, false)) { menu->browser.valid = false; menu_show_error(menu, "Couldn't open next directory"); }
            } else if (t == ENTRY_TYPE_ARCHIVE) {
                if (push_directory(menu, menu->browser.entry->name, true))  { menu->browser.valid = false; menu_show_error(menu, "Couldn't open file archive"); }
            } else if (link_pick_src_is_disc ? (t == ENTRY_TYPE_ROM) : (t == ENTRY_TYPE_DISK)) {
                finish_link_pick(menu);
            } else {
                sound_play_effect(SFX_ERROR);   /* not the type we're linking to */
            }
        } else if (menu->browser.entry->type == ENTRY_TYPE_ROM) {
            /* A on a ROM opens the "More" menu (Launch / Game settings / ...). */
            update_dynamic_labels(menu);   /* per-game labels reflect this ROM */
            ui_components_context_menu_show(menu->browser.archive ? &more_archive_context_menu : &more_rom_context_menu);
            sound_play_effect(SFX_SETTING);
        } else {
            sound_play_effect(SFX_ENTER);
            /* Any file view opened from the popup must return to the popup, not the
               old full list. Default to "returning"; the two in-place dir-navigation
               cases below clear it (they stay in the browser, no round-trip). */
            popup_return_pending = true;
            switch (menu->browser.entry->type) {
                case ENTRY_TYPE_ARCHIVE:
                    popup_return_pending = false;
                    if (push_directory(menu, menu->browser.entry->name, true)) {
                        menu->browser.valid = false;
                        menu_show_error(menu, "Couldn't open file archive");
                    }
                    break;
                case ENTRY_TYPE_ARCHIVED:
                    menu->next_mode = MENU_MODE_EXTRACT_FILE;
                    break;
                case ENTRY_TYPE_DIR:
                    popup_return_pending = false;
                    if (push_directory(menu, menu->browser.entry->name, false)) {
                        menu->browser.valid = false;
                        menu_show_error(menu, "Couldn't open next directory");
                    }
                    break;
                case ENTRY_TYPE_DISK:
                    launch_disk(menu);
                    break;
                case ENTRY_TYPE_EMULATOR:
                    menu->next_mode = MENU_MODE_LOAD_EMULATOR;
                    break;
                case ENTRY_TYPE_IMAGE:
                    menu->next_mode = MENU_MODE_IMAGE_VIEWER;
                    break;
                case ENTRY_TYPE_MUSIC:
                    menu->next_mode = MENU_MODE_MUSIC_PLAYER;
                    break;
                case ENTRY_TYPE_TEXT:
                    menu->next_mode = MENU_MODE_TEXT_VIEWER;
                    break;
                default:
                    menu->next_mode = MENU_MODE_FILE_INFO;
                    break;
            }
        }
    } else if (menu->actions.settings) {
        /* Start: launch the selected ROM directly; otherwise open the More menu. */
        if (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) {
            launch_game(menu, NULL);
            sound_play_effect(SFX_LAUNCH);
        } else {
            update_dynamic_labels(menu);
            ui_components_context_menu_show(menu->browser.archive ? &more_archive_context_menu : &more_context_menu);
            sound_play_effect(SFX_SETTING);
        }
    } else if (menu->actions.options) {
        if (link_pick_active) {
            /* Z cancels an in-progress link instead of leaving to the grid. */
            cancel_link_pick();
            sound_play_effect(SFX_EXIT);
        } else {
            /* Z: back to the grid, on whichever tab it was opened from. */
            menu->next_mode = MENU_MODE_GAMES_GRID;
            sound_play_effect(SFX_CURSOR);
        }
    }

    /* Hold B to Favorite: short press = navigate up, hold = toggle single, hold+direction = multi-select.
       ROMs AND 64DD disks (.ndd) are favoritable -- the backend (toggle_favorite/mark_range) stores
       the right BOOKKEEPING_TYPE per entry, so gate on entry_is_favoritable, not == ENTRY_TYPE_ROM
       (that ROM-only gate was why .ndd disks couldn't be favorited). */
    {
        bool can_fav = menu->browser.entry && entry_is_favoritable(menu->browser.entry->type);

        joypad_buttons_t bh = {0};
        JOYPAD_PORT_FOREACH(port) { bh = joypad_get_buttons_held(port); if (bh.raw) break; }

        /* b_hold / b_multi_mode / b_multi_add are module-level statics reset in view_browser_init */

        if (bh.b) {
            if (can_fav) {
                /* Root double-B → Grid is handled entirely by the failsafe above. */
                /* While B held on a ROM, direction fires mark/unmark all entries passed over */
                if (b_hold >= 3 && (menu->actions.go_up || menu->actions.go_down)) {
                    if (!b_multi_mode) {
                        b_multi_mode = true;
                        /* Direction of the whole sweep is decided by the starting
                           entry: if it wasn't a favorite we add, otherwise remove. */
                        path_t *p = path_clone_push(menu->browser.directory, menu->browser.entry->name);
                        bool is_fav = false;
                        for (int k = 0; k < FAVORITES_COUNT; k++) {
                            bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                            if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(p, f->primary_path)) {
                                is_fav = true; break;
                            }
                        }
                        path_free(p);
                        b_multi_add = !is_fav;
                    }
                    bool was_dirty = fav_dirty;
                    /* Fill the entire span travelled this frame so a C-button
                       fast-scroll marks every skipped entry, not just the endpoint. */
                    mark_range_favorite(menu, sel_before_nav, menu->browser.selected, b_multi_add);
                    if (fav_dirty && !was_dirty) {
                        sound_play_effect(SFX_SETTING);
                    }
                }
                b_hold++;
                if (b_hold == 20 && !b_multi_mode) {
                    path_t *fav_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
                    int fi = -1;
                    for (int k = 0; k < FAVORITES_COUNT; k++) {
                        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY && path_are_match(fav_path, f->primary_path)) {
                            fi = k; break;
                        }
                    }
                    if (fi >= 0) {
                        bookkeeping_favorite_remove(&menu->bookkeeping, fi);
                    } else {
                        /* Store the right type -- a .ndd must be DISK, not ROM, or the grid
                           mis-reads it as a cartridge (garbage name/code, no boot). */
                        bool is_disk = menu->browser.entry->type == ENTRY_TYPE_DISK;
                        bookkeeping_favorite_add(&menu->bookkeeping, fav_path, NULL,
                            is_disk ? BOOKKEEPING_TYPE_DISK : BOOKKEEPING_TYPE_ROM);
                    }
                    path_free(fav_path);
                    fav_dirty = true;
                    sound_play_effect(SFX_ENTER);
                }
            } else {
                /* Non-ROM back-out is handled by the single-B back-out above. */
                b_hold++;
            }
        } else {
            /* B released */
            if (b_skip_release) {
                b_skip_release = false;
            } else if (!b_multi_mode && can_fav && b_hold > 0 && b_hold < 20) {
                /* Short press on ROM — navigate up (root handled on press above) */
                if (!browser_at_root(menu)) {
                    if (pop_directory(menu)) {
                        menu->browser.valid = false;
                        menu_show_error(menu, "Couldn't open last directory");
                    }
                    sound_play_effect(SFX_EXIT);
                }
            }
            b_hold = 0;
            b_multi_mode = false;
        }
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    /* On-screen keyboard overlay: grid backdrop + the keyboard popup + the standard action bar
       (A / B / S / -- / R). Drawn standalone so it sits cleanly over everything. */
    if (kb_mode) {
        view_games_grid_draw_background(menu, d);
        kb_popup_draw();
        ui_components_actions_bar_buttons_draw("A: Select", "B: Delete", "S: OK", "", "Z: Cancel");
        rdpq_detach_show();
        return;
    }

    if (browser_popup_mode) {
        /* Popup mode: grid as the backdrop, file popup on top (the popup is now
           full-width with a fixed height, so the grid only shows top/bottom — no
           more art colliding with the list). The More menu draws over the popup. */
        view_games_grid_draw_background(menu, d);
        draw_file_popup(menu);
        component_context_menu_t *pcm =
            menu->browser.archive ? &more_archive_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) ? &more_rom_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DISK) ? &more_disk_context_menu :
            &more_context_menu;
        ui_components_context_menu_draw(pcm);
        if (folder_fav_working) folder_fav_popup_draw();
        rdpq_detach_show();
        return;
    }

    /* Solid background — the custom user PNG is now used for the boot splash only. */
    ui_components_background_draw();

    /* Frameless: no tab bar, no border — the file list uses the reclaimed space. */

    /* Build the is_fav array for the visible window — cached, rebuilt only when
       the directory changes, the scroll position shifts, or a favourite is toggled. */
    if (menu->browser.entries > 0) {
        int sp = 0;
        if (menu->browser.entries > FILE_LIST_ENTRIES && menu->browser.selected >= FILE_LIST_ENTRIES / 2) {
            sp = menu->browser.selected - FILE_LIST_ENTRIES / 2;
            if (sp > menu->browser.entries - FILE_LIST_ENTRIES) sp = menu->browser.entries - FILE_LIST_ENTRIES;
        }
        if (fav_dirty || fav_cache_n != menu->browser.entries || fav_cache_sp != sp) {
            free(fav_cache);
            fav_cache    = calloc(menu->browser.entries, sizeof(bool));
            fav_cache_n  = menu->browser.entries;
            fav_cache_sp = sp;
            fav_dirty    = false;
            for (int j = sp; j < sp + FILE_LIST_ENTRIES && j < menu->browser.entries; j++) {
                if (menu->browser.list[j].type == ENTRY_TYPE_ROM) {
                    path_t *p = path_clone_push(menu->browser.directory, menu->browser.list[j].name);
                    for (int k = 0; k < FAVORITES_COUNT; k++) {
                        bookkeeping_item_t *f = &menu->bookkeeping.favorite_items[k];
                        if (f->bookkeeping_type != BOOKKEEPING_TYPE_EMPTY
                            && path_are_match(p, f->primary_path)) {
                            fav_cache[j] = true;
                            break;
                        }
                    }
                    path_free(p);
                }
            }
        }
    } else {
        free(fav_cache);
        fav_cache    = NULL;
        fav_cache_n  = -1;
        fav_cache_sp = -1;
    }
    ui_components_file_list_draw(menu->browser.list, menu->browser.entries, menu->browser.selected, fav_cache, menu->settings.show_file_size);

    const char *action = NULL;

    if (menu->browser.entry) {
        switch (menu->browser.entry->type) {
            case ENTRY_TYPE_DIR:    action = "A: Enter"; break;
            case ENTRY_TYPE_ROM:    action = "A: More";  break;
            case ENTRY_TYPE_DISK:   action = "A: Info";  break;
            case ENTRY_TYPE_IMAGE:  action = "A: Show";  break;
            case ENTRY_TYPE_TEXT:   action = "A: View";  break;
            case ENTRY_TYPE_MUSIC:  action = "A: Play";  break;
            case ENTRY_TYPE_ARCHIVE:action = "A: Open";  break;
            default:                action = "A: Info";  break;
        }
    }

    bool can_favorite = menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM;
    const char *act = (menu->browser.entries == 0 || !action) ? "" : action;
    if (link_pick_active && menu->browser.entry) {
        /* In link-pick mode the A hint depends on the highlighted entry. */
        int t = menu->browser.entry->type;
        if (t == ENTRY_TYPE_DIR || t == ENTRY_TYPE_ARCHIVE) act = "A: Enter";
        else if (link_pick_src_is_disc ? (t == ENTRY_TYPE_ROM) : (t == ENTRY_TYPE_DISK)) act = "A: Link";
        else act = "";
    }

    /* Top line: the link-pick banner while linking, else the path breadcrumb. */
    {
        char path_buf[160], display_path[128];
        menu_font_style_t top_style;
        if (link_pick_active) {
            snprintf(display_path, sizeof(display_path),
                     "LINK  %s  ->  open its %s   (A: link & boot,  Z: cancel)",
                     link_pick_name, link_pick_src_is_disc ? "base ROM" : "disc");
            top_style = STL_YELLOW;
        } else {
            char *rest = strip_fs_prefix(path_get(menu->browser.directory));
            int plen = snprintf(path_buf, sizeof(path_buf), "SD: %s", rest[0] ? rest : "/");
            if (plen > 70) snprintf(display_path, sizeof(display_path), "...%s", path_buf + plen - 67);
            else           memcpy(display_path, path_buf, (size_t)plen + 1);
            top_style = STL_GRAY;
        }
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = top_style,
                .width    = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
                .align    = ALIGN_LEFT,
                .wrap     = WRAP_ELLIPSES,
            },
            FNT_DEFAULT,
            VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
            VISIBLE_AREA_Y0 + FILE_LIST_PATH_H - 2,
            display_path
        );
    }

    /* Action bar: 4 equal sections — A | S: action | Z: Grid | C▲▼ */
    {
        int ab_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;
        int ab_y = LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL;
        int ab_h = VISIBLE_AREA_Y1 - LAYOUT_ACTIONS_SEPARATOR_Y - BORDER_THICKNESS - (TEXT_MARGIN_VERTICAL * 2);
        int ab_w = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2);
        int sec  = ab_w / 4;
        rdpq_textparms_t p = {
            .style_id    = STL_DEFAULT,
            .width       = sec,
            .height      = ab_h,
            .valign      = VALIGN_BOTTOM,
            .wrap        = WRAP_ELLIPSES,
            .line_spacing= TEXT_LINE_SPACING_ADJUST,
        };
        p.align = ALIGN_LEFT;
        rdpq_text_print(&p, FNT_DEFAULT, ab_x,           ab_y, act);
        p.align = ALIGN_CENTER;
        rdpq_text_print(&p, FNT_DEFAULT, ab_x + sec,     ab_y,
            link_pick_active ? "" : (can_favorite ? "S: Launch" : "S: More"));
        rdpq_text_print(&p, FNT_DEFAULT, ab_x + 2 * sec, ab_y,
            link_pick_active ? "Z: Cancel" : "Z: Grid");
        p.align = ALIGN_RIGHT;
        rdpq_text_print(&p, FNT_DEFAULT, ab_x + 3 * sec, ab_y, "C \xE2\x96\xB2\xE2\x96\xBC");
    }

    {
        component_context_menu_t *draw_cm =
            menu->browser.archive ? &more_archive_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM) ? &more_rom_context_menu :
            (menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DISK) ? &more_disk_context_menu :
            &more_context_menu;
        ui_components_context_menu_draw(draw_cm);
    }

    if (folder_fav_working) folder_fav_popup_draw();
    if (fcopy_phase) fcopy_popup_draw();

    rdpq_detach_show();
}


void view_browser_init (menu_t *menu) {
    cancel_link_pick();   /* never carry a stale link-pick across a browser (re)entry */
    if (lp_pending) {     /* grid hand-off: arm a disclink-mode link-pick for an expansion disc */
        link_pick_active      = true;
        link_pick_src_is_disc = true;     /* source is the disc; we pick its base ROM */
        link_pick_src         = NULL;     /* disclink mode boots by favorite id, not a stored path */
        strncpy(link_pick_name, lp_pending_name, sizeof link_pick_name - 1);
        link_pick_name[sizeof link_pick_name - 1] = '\0';
        link_pick_disclink    = true;
        strncpy(link_pick_disc_code, lp_pending_code, sizeof link_pick_disc_code - 1);
        link_pick_disc_code[sizeof link_pick_disc_code - 1] = '\0';
        link_pick_disc_fav    = lp_pending_fav;
        lp_pending = false;
        browser_popup_mode    = true;     /* link-pick now uses the rainbow popup, not a full list */
    }
    /* Decide popup vs full-list from the entry intent. Entering via
       view_browser_open_popup() sets popup_requested; any other entry
       (e.g. empty-grid R) starts as the full file list. Returning from a
       More-launched sub-view (reopen_pending set) keeps the current mode, so a
       Game-settings / Hardware round-trip stays inside the popup. */
    if (!reopen_pending) {
        /* The full file list has no intended entry point -- the grid always opens
           the popup, and file views return to it -- so never drop out of popup mode.
           (re)initialise the three "More" menus on a fresh entry so a stale/empty one
           doesn't render as a jittering rainbow box; skip it when returning from a
           file view (the menus were set up on the original open and weren't touched). */
        browser_popup_mode = true;   /* always the popup now -- incl. link-pick */
        if (!popup_return_pending) {
            ui_components_context_menu_init(&more_rom_context_menu);
            ui_components_context_menu_init(&more_disk_context_menu);
            ui_components_context_menu_init(&more_context_menu);
            ui_components_context_menu_init(&more_archive_context_menu);
        }
    }
    popup_requested      = false;
    popup_return_pending = false;
    fav_dirty = true;
    root_b_primed = false;
    b_hold = 0;
    pop_multi = false;
    b_multi_mode = false;
    b_multi_add = false;
    b_skip_release = true;     /* ignore B held from whatever mode we came from */
    context_menu_was_active = false;
    update_file_size_label(menu);
    if (!menu->browser.valid) {
        ui_components_context_menu_init(&more_disk_context_menu);
        ui_components_context_menu_init(&more_context_menu);
        ui_components_context_menu_init(&more_archive_context_menu);
        if (load_directory(menu)) {
            /* The saved/last directory failed to open (deleted, renamed, a stale default, or
               a momentary SD read hiccup after another view touched the card). Fall back to
               the SD root and RETRY before surfacing an error, so the user lands on a working
               file list instead of being stranded on the error box (e.g. backing out of Menu
               Information). Only error if even the root won't open. */
            path_free(menu->browser.directory);
            menu->browser.directory = path_init(menu->storage_prefix, "");
            if (load_directory(menu)) {
                menu_show_error(menu, "Error while opening initial directory");
            } else {
                menu->browser.valid = true;
            }
        } else {
            menu->browser.valid = true;
        }
    }

    if (menu->browser.select_file) {
        if (select_file(menu, menu->browser.select_file)) {
            menu->browser.valid = false;
            menu_show_error(menu, "Error while navigating to file");
        }
        path_free(menu->browser.select_file);
        menu->browser.select_file = NULL;
    }

    if (menu->browser.reload) {
        menu->browser.reload = false;
        if (reload_directory(menu)) {
            menu_show_error(menu, "Error while reloading current directory");
            menu->browser.valid = false;
        }
    }

    /* Returning from a More-launched view: rebuild the exact menu chain we
       snapshotted (top row + up to two nested submenu rows). */
    if (reopen_pending) {
        reopen_pending = false;
        component_context_menu_t *top = reopen_top;
        if (top) {
            ui_components_context_menu_init(top);          /* recomputes row_count */
            int r0 = reopen_top_row;
            if (r0 < 0) r0 = 0; else if (r0 >= top->row_count) r0 = top->row_count - 1;
            top->row_selected = r0;
            top->submenu = NULL;

            if (reopen_sub1) {
                /* The history popup is built dynamically — repopulate before reuse. */
                if (reopen_sub1 == &history_popup_menu) rebuild_history_popup(menu);
                ui_components_context_menu_init(reopen_sub1);
                int r1 = reopen_sub1_row;
                if (r1 < 0) r1 = 0; else if (r1 >= reopen_sub1->row_count) r1 = reopen_sub1->row_count - 1;
                reopen_sub1->row_selected = r1;
                reopen_sub1->parent = top;
                reopen_sub1->submenu = NULL;
                top->submenu = reopen_sub1;

                if (reopen_sub2) {
                    ui_components_context_menu_init(reopen_sub2);
                    int r2 = reopen_sub2_row;
                    if (r2 < 0) r2 = 0; else if (r2 >= reopen_sub2->row_count) r2 = reopen_sub2->row_count - 1;
                    reopen_sub2->row_selected = r2;
                    reopen_sub2->parent = reopen_sub1;
                    reopen_sub2->submenu = NULL;
                    reopen_sub1->submenu = reopen_sub2;
                }
            }
        }
        b_skip_release = true;
    }

    /* Opened via view_browser_open_file_management(): the select_file resolve above
       has put the cursor on the requested ROM — now open the real More menu with the
       canonical File-management submenu showing. */
    if (fm_requested) {
        fm_requested = false;
        bool is_rom = menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_ROM;
        bool is_disk = menu->browser.entry && menu->browser.entry->type == ENTRY_TYPE_DISK;
        component_context_menu_t *top =
            menu->browser.archive ? &more_archive_context_menu :
            is_rom ? &more_rom_context_menu : is_disk ? &more_disk_context_menu : &more_context_menu;
        component_context_menu_t *sub =
            menu->browser.archive ? &file_management_archive_context_menu : &file_management_context_menu;
        ui_components_context_menu_init(top);
        ui_components_context_menu_init(sub);
        ui_components_context_menu_show(top);
        sub->row_selected = 0;
        sub->parent = top;
        top->submenu = sub;
        b_skip_release = true;
    }
}


void view_browser_open_popup(menu_t *menu) {
    popup_requested = true;
    popup_scroll    = 0;
    root_b_primed   = false;
    /* Open at the configured default folder, not wherever we last browsed. */
    const char *dd = menu->settings.default_directory;
    while (dd[0] == '/') dd++;
    path_t *d = path_init(menu->storage_prefix, (char *)dd);
    if (!directory_exists(path_get(d))) { path_free(d); d = path_init(menu->storage_prefix, ""); }
    if (menu->browser.directory) path_free(menu->browser.directory);
    menu->browser.directory = d;
    menu->browser.reload    = true;
    menu->next_mode = MENU_MODE_BROWSER;
}

void view_browser_open_file_management(menu_t *menu, path_t *rom_path) {
    if (menu->browser.select_file) path_free(menu->browser.select_file);
    menu->browser.select_file = path_clone(rom_path);
    popup_requested = true;
    fm_requested    = true;
    popup_scroll    = 0;
    root_b_primed   = false;
    menu->next_mode = MENU_MODE_BROWSER;
}

void view_browser_display (menu_t *menu, surface_t *display) {
    process(menu);

    /* Flush pending favorite toggles ONCE on the way out of the browser (any next_mode change),
       instead of rewriting the whole favorites.ini on every toggle. Covers R-to-grid, launch,
       link, etc. -- the only loss case is cutting power while still inside the browser. */
    if (menu->next_mode != MENU_MODE_BROWSER && fav_unsaved) {
        bookkeeping_save_favorites(&menu->bookkeeping);
        fav_unsaved = false;
    }

    draw(menu, display);
}
