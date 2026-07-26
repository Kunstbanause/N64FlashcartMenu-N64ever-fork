/**
 * @file library.c
 * @brief Pinned library galleries (folders pinned from the browser as extra grid tabs).
 * @ingroup menu
 */

#include <libdragon.h>
#include <stdlib.h>
#include <string.h>

#include "fs_filter.h"
#include "ini_parser.h"
#include "library.h"
#include "utils/fs.h"

#define LIBRARIES_INI_SECTION "libraries"

static char      *libraries_ini_path          = NULL;
static library_t  libraries[LIBRARIES_MAX];
static int        libraries_count             = 0;

void library_init (const char *ini_path) {
    free(libraries_ini_path);
    libraries_ini_path = ini_path ? strdup(ini_path) : NULL;
}

void library_free_items (library_t *lib) {
    if (!lib) return;
    if (lib->items) {
        for (int i = 0; i < lib->count; i++) {
            path_free(lib->items[i].primary_path);
            path_free(lib->items[i].secondary_path);
        }
        free(lib->items);
        lib->items = NULL;
    }
    lib->count   = 0;
    lib->scanned = false;
}

void library_deinit (void) {
    for (int i = 0; i < libraries_count; i++) {
        library_free_items(&libraries[i]);
        path_free(libraries[i].path);
        libraries[i].path = NULL;
    }
    libraries_count = 0;
    free(libraries_ini_path);
    libraries_ini_path = NULL;
}

void library_load (void) {
    for (int i = 0; i < libraries_count; i++) {
        library_free_items(&libraries[i]);
        path_free(libraries[i].path);
    }
    memset(libraries, 0, sizeof(libraries));
    libraries_count = 0;

    if (!libraries_ini_path || !file_exists(libraries_ini_path)) {
        return;
    }

    ini_t *ini = ini_try_load(libraries_ini_path);
    if (!ini) return;

    char key[24];
    for (int i = 0; i < LIBRARIES_MAX; i++) {
        snprintf(key, sizeof(key), "%d_path", i);
        const char *path_str = ini_get_string(ini, LIBRARIES_INI_SECTION, key, "");
        if (!path_str[0]) break;   /* indices are contiguous by construction */

        snprintf(key, sizeof(key), "%d_name", i);
        const char *name_str = ini_get_string(ini, LIBRARIES_INI_SECTION, key, "");

        library_t *lib = &libraries[libraries_count];
        strncpy(lib->name, name_str, sizeof(lib->name) - 1);
        lib->name[sizeof(lib->name) - 1] = '\0';
        lib->path    = path_create(path_str);
        lib->items   = NULL;
        lib->count   = 0;
        lib->scanned = false;
        libraries_count++;
    }

    ini_free(ini);
}

void library_save (void) {
    if (!libraries_ini_path) return;

    ini_t *ini = ini_create();
    if (!ini) {
        debugf("[LIBRARY] Failed to create INI structure\n");
        return;
    }

    char key[24];
    for (int i = 0; i < libraries_count; i++) {
        snprintf(key, sizeof(key), "%d_name", i);
        ini_set_string(ini, LIBRARIES_INI_SECTION, key, libraries[i].name);
        snprintf(key, sizeof(key), "%d_path", i);
        ini_set_string(ini, LIBRARIES_INI_SECTION, key, path_get(libraries[i].path));
    }

    if (!ini_save(ini, libraries_ini_path)) {
        debugf("[LIBRARY] Failed to save to %s\n", libraries_ini_path);
    }
    ini_free(ini);
}

int library_count (void) {
    return libraries_count;
}

library_t *library_get (int i) {
    if (i < 0 || i >= libraries_count) return NULL;
    return &libraries[i];
}

int library_find_by_path (path_t *folder) {
    if (!folder) return -1;
    for (int i = 0; i < libraries_count; i++) {
        if (path_are_match(libraries[i].path, folder)) return i;
    }
    return -1;
}

bool library_add (const char *name, path_t *folder) {
    if (!folder || libraries_count >= LIBRARIES_MAX) return false;
    if (library_find_by_path(folder) >= 0) return false;

    library_t *lib = &libraries[libraries_count];
    const char *label = (name && name[0]) ? name : path_last_get(folder);
    strncpy(lib->name, label, sizeof(lib->name) - 1);
    lib->name[sizeof(lib->name) - 1] = '\0';
    lib->path    = path_clone(folder);
    lib->items   = NULL;
    lib->count   = 0;
    lib->scanned = false;
    libraries_count++;

    library_save();
    return true;
}

void library_remove (int i) {
    if (i < 0 || i >= libraries_count) return;

    library_free_items(&libraries[i]);
    path_free(libraries[i].path);

    for (int j = i; j < libraries_count - 1; j++) {
        libraries[j] = libraries[j + 1];
    }
    memset(&libraries[libraries_count - 1], 0, sizeof(library_t));
    libraries_count--;

    library_save();
}

/* Recursive walk callback (see libdragon dir_walk): `fn` is the entry's full path,
   `dir->d_name` its basename -- the same fields folder_fav_scan() filters on. */
static int library_scan_cb (const char *fn, dir_t *dir, void *data) {
    library_t *lib = (library_t *)data;
    if (lib->count >= LIBRARY_ITEMS_MAX) return DIR_WALK_ABORT;

    path_t *p = path_create(fn);
    bool hidden = path_is_hidden(p);

    if (dir->d_type == DT_DIR) {
        path_free(p);
        return hidden ? DIR_WALK_SKIPDIR : DIR_WALK_CONTINUE;
    }

    bool is_disk = file_has_extensions(dir->d_name, disk_extensions);
    if (!hidden && (is_disk || file_has_extensions(dir->d_name, n64_rom_extensions))) {
        bookkeeping_item_t *it = &lib->items[lib->count++];
        it->primary_path      = p;
        it->secondary_path    = path_create("");
        it->bookkeeping_type  = is_disk ? BOOKKEEPING_TYPE_DISK : BOOKKEEPING_TYPE_ROM;
        it->game_code[0]      = '\0';
        it->presents_as       = 0;
        it->sort_initial      = 0;
        it->sort_name[0]      = '\0';
    } else {
        path_free(p);
    }
    return DIR_WALK_CONTINUE;
}

void library_scan (library_t *lib) {
    if (!lib || !lib->path) return;

    library_free_items(lib);   /* safe no-op if this is the first scan */
    lib->items = calloc(LIBRARY_ITEMS_MAX, sizeof(bookkeeping_item_t));
    if (!lib->items) {
        debugf("[LIBRARY] Failed to allocate item array for '%s'\n", lib->name);
        lib->scanned = true;
        return;
    }

    dir_walk(path_get(lib->path), library_scan_cb, lib);
    lib->scanned = true;
    debugf("[LIBRARY] scan of '%s' found %d item(s)\n", path_get(lib->path), lib->count);
}
