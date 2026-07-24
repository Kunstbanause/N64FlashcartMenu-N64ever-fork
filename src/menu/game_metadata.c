/**
 * @file game_metadata.c
 * @brief Game metadata lookup — custom files, then built-in DB.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "game_metadata.h"
#include "game_metadata_db.h"
#include "ini_parser.h"
#include "utils/fs.h"

/* Static buffers for custom-file results (overwritten on each custom lookup). */
static char cm_title[64];
static char cm_developer[64];
static char cm_release_jp[16];
static char cm_release_us[16];
static char cm_release_eu[16];
static char cm_description[512];

static int db_compare(const void *key, const void *entry) {
    return strncmp((const char *)key, ((const game_db_entry_t *)entry)->base, 3);
}

bool game_metadata_db_lookup(const char *game_code, game_meta_t *out) {
    if (!out || !game_code || game_code[0] == '\0') return false;
    memset(out, 0, sizeof(*out));
    const game_db_entry_t *db_entry = (const game_db_entry_t *)bsearch(
        game_code, game_db, GAME_DB_COUNT, sizeof(game_db[0]), db_compare);
    if (!db_entry) return false;
    out->title       = db_entry->title;
    out->developer   = db_entry->developer;
    out->release_jp  = db_entry->release_jp;
    out->release_us  = db_entry->release_us;
    out->release_eu  = db_entry->release_eu;
    out->description = db_entry->description;
    return true;
}

int game_metadata_db_count(void) {
    return GAME_DB_COUNT;
}

const char *game_metadata_db_base(int index) {
    if (index < 0 || index >= GAME_DB_COUNT) return NULL;
    return game_db[index].base;
}

bool game_metadata_get(const char *storage_prefix, const char *game_code, game_meta_t *out) {
    if (!out || !game_code || game_code[0] == '\0') return false;
    memset(out, 0, sizeof(*out));

    /* ------------------------------------------------------------------ */
    /* 1. User custom file: <prefix>/menu/n64ever/gameconfigs/<GAMECODE>.meta.ini */
    /*    (falls back to the legacy /menu/custom/gameconfigs/ location).      */
    /* ------------------------------------------------------------------ */
    char custom_path[160];
    snprintf(custom_path, sizeof(custom_path),
             "%s/menu/n64ever/gameconfigs/%.4s.meta.ini", storage_prefix, game_code);
    if (!file_exists(custom_path)) {
        snprintf(custom_path, sizeof(custom_path),
                 "%s/menu/custom/gameconfigs/%.4s.meta.ini", storage_prefix, game_code);
    }

    bool found_custom = false;
    if (file_exists(custom_path)) {
        ini_t *ini = ini_try_load(custom_path);
        if (ini) {
            const char *v;
            v = ini_get_string(ini, "meta", "title",       NULL);
            if (v && v[0]) { strncpy(cm_title,       v, sizeof(cm_title)       - 1); out->title       = cm_title;       found_custom = true; }
            v = ini_get_string(ini, "meta", "developer",   NULL);
            if (v && v[0]) { strncpy(cm_developer,   v, sizeof(cm_developer)   - 1); out->developer   = cm_developer;   found_custom = true; }
            v = ini_get_string(ini, "meta", "release_jp",  NULL);
            if (v && v[0]) { strncpy(cm_release_jp,  v, sizeof(cm_release_jp)  - 1); out->release_jp  = cm_release_jp;  }
            v = ini_get_string(ini, "meta", "release_us",  NULL);
            if (v && v[0]) { strncpy(cm_release_us,  v, sizeof(cm_release_us)  - 1); out->release_us  = cm_release_us;  }
            v = ini_get_string(ini, "meta", "release_eu",  NULL);
            if (v && v[0]) { strncpy(cm_release_eu,  v, sizeof(cm_release_eu)  - 1); out->release_eu  = cm_release_eu;  }
            v = ini_get_string(ini, "meta", "description", NULL);
            if (v && v[0]) { strncpy(cm_description, v, sizeof(cm_description) - 1); out->description = cm_description; }
            ini_free(ini);
        }
    }

    /* If custom file filled everything we need, return early. */
    if (found_custom && out->title && out->developer) return true;

    /* ------------------------------------------------------------------ */
    /* 2. Built-in database (binary search on 3-char base code)           */
    /* ------------------------------------------------------------------ */
    const game_db_entry_t *db_entry = (const game_db_entry_t *)bsearch(
        game_code, game_db, GAME_DB_COUNT, sizeof(game_db[0]), db_compare);

    if (db_entry) {
        /* Custom file takes precedence field-by-field; DB fills the rest. */
        if (!out->title)       out->title       = db_entry->title;
        if (!out->developer)   out->developer   = db_entry->developer;
        if (!out->release_jp)  out->release_jp  = db_entry->release_jp;
        if (!out->release_us)  out->release_us  = db_entry->release_us;
        if (!out->release_eu)  out->release_eu  = db_entry->release_eu;
        if (!out->description) out->description = db_entry->description;
        return true;
    }

    return found_custom; /* custom partial match still counts */
}
