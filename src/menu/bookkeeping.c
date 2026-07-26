/**
 * @file bookkeeping.c
 * @brief Bookkeeping functions for history and favorites
 * @ingroup menu
 */

#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include "ini_parser.h"

#include "bookkeeping.h"
#include "utils/fs.h"
#include "path.h"

static char *favorites_path = NULL;
static char *history_path = NULL;
static char *legacy_path = NULL;

/* Set when the corresponding file existed but couldn't be read/parsed at load time. The
   in-RAM list is empty in that case, and writing it back would replace the user's real data
   with nothing -- so every save is refused until a load succeeds. */
static bool favorites_write_blocked = false;
static bool history_write_blocked = false;

/**
 * @brief Initialize the bookkeeping system with the favorites/history paths.
 *
 * @param fav     Path to the favorites file.
 * @param hist    Path to the history file.
 * @param legacy  Path to the pre-split combined file (or NULL).
 */
void bookkeeping_init (const char *fav, const char *hist, const char *legacy) {
    free(favorites_path);
    free(history_path);
    free(legacy_path);
    favorites_path = fav ? strdup(fav) : NULL;
    history_path = hist ? strdup(hist) : NULL;
    legacy_path = legacy ? strdup(legacy) : NULL;
}

/**
 * @brief Load a list of bookkeeping items from an INI file.
 * 
 * @param list Pointer to the list of bookkeeping items.
 * @param count Number of items in the list.
 * @param ini Pointer to the INI file structure.
 * @param group Name of the group in the INI file.
 */
static void bookkeeping_clear_list(bookkeeping_item_t *list, uint16_t count);

static void bookkeeping_ini_load_list(bookkeeping_item_t *list, uint16_t count, ini_t *ini, const char *group) {
    /* Start from a fully-empty list, then fill from the file. */
    bookkeeping_clear_list(list, count);

    /* Iterate the section's pairs ONCE (O(pairs)) instead of probing each of the
       up-to-2048 slots with ini_get_* -- every ini_get_* is a linear find_pair scan,
       so reading the list back that way is O(N^2) (the ~45 s favorites-load boot cost
       that returns at high favorite counts). Keys are "<index>_<field>", where <field>
       may itself contain underscores (e.g. "primary_path"). */
    int n = ini_section_pair_count(ini, group);
    for (int p = 0; p < n; p++) {
        const char *key = NULL, *value = NULL;
        if (!ini_section_get_pair(ini, group, p, &key, &value) || !key || !value) {
            continue;
        }
        char *end = NULL;
        long idx = strtol(key, &end, 10);
        if (end == key || *end != '_' || idx < 0 || idx >= count) {
            continue;   /* not an "<index>_..." key belonging to this list */
        }
        const char *field = end + 1;
        bookkeeping_item_t *it = &list[idx];

        if (strcmp(field, "primary_path") == 0) {
            if (value[0]) { path_free(it->primary_path); it->primary_path = path_create(value); }
        } else if (strcmp(field, "secondary_path") == 0) {
            if (value[0]) { path_free(it->secondary_path); it->secondary_path = path_create(value); }
        } else if (strcmp(field, "type") == 0) {
            it->bookkeeping_type = (bookkeeping_item_types_t)atoi(value);
        } else if (strcmp(field, "game_code") == 0) {
            strncpy(it->game_code, value, sizeof(it->game_code) - 1);
            it->game_code[sizeof(it->game_code) - 1] = '\0';
        } else if (strcmp(field, "presents_as") == 0) {
            it->presents_as = atoi(value);
        } else if (strcmp(field, "sort_initial") == 0) {
            it->sort_initial = value[0];
        } else if (strcmp(field, "sort_name") == 0) {
            strncpy(it->sort_name, value, sizeof(it->sort_name) - 1);
            it->sort_name[sizeof(it->sort_name) - 1] = '\0';
        }
    }
}

/**
 * @brief Fill a list with empty items.
 */
static void bookkeeping_clear_list(bookkeeping_item_t *list, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        /* Free before replacing so bookkeeping_load() can be RE-CALLED without leaking the
           existing list (used by the folder-fav abort to discard the in-RAM partial). NULL-safe,
           so the first load on a calloc'd struct is fine. */
        path_free(list[i].primary_path);
        path_free(list[i].secondary_path);
        list[i].primary_path = path_create("");
        list[i].secondary_path = path_create("");
        list[i].bookkeeping_type = BOOKKEEPING_TYPE_EMPTY;
        list[i].game_code[0] = '\0';
        list[i].presents_as = 0;
        list[i].sort_initial = 0;
        list[i].sort_name[0] = '\0';
    }
}

/** @brief Outcome of loading one list -- drives whether it's safe to write the file back. */
typedef enum {
    BK_LOAD_OK,          /**< File read and parsed. */
    BK_LOAD_NO_FILE,     /**< Confirmed no such file; an empty list is the truth. */
    BK_LOAD_UNREADABLE,  /**< File is there but couldn't be read/parsed -- DO NOT overwrite it. */
} bk_load_result_t;

/**
 * @brief Load a single list from a file.
 *
 * The list is always left in a valid (possibly empty) state. The return value tells the
 * caller whether an empty list means "there really is nothing" or "we couldn't read it" --
 * writing the list back in the latter case destroys the user's data.
 */
static bk_load_result_t bookkeeping_load_list_from(bookkeeping_item_t *list, uint16_t count, const char *path, const char *group) {
    if (!path) {
        bookkeeping_clear_list(list, count);
        return BK_LOAD_NO_FILE;
    }

    file_presence_t presence = file_presence((char *)path);
    if (presence != FILE_PRESENCE_PRESENT) {
        bookkeeping_clear_list(list, count);
        if (presence == FILE_PRESENCE_ABSENT) return BK_LOAD_NO_FILE;
        debugf("[BOOKKEEPING] %s unreadable -- keeping the file as-is\n", path);
        return BK_LOAD_UNREADABLE;
    }

    ini_t *ini = ini_try_load(path);
    if (!ini) {
        /* Present but unparseable: a truncated read or a corrupted card. Show an empty list
           this session, but never write that emptiness back over the real file. */
        bookkeeping_clear_list(list, count);
        debugf("[BOOKKEEPING] %s failed to parse -- keeping the file as-is\n", path);
        return BK_LOAD_UNREADABLE;
    }

    bookkeeping_ini_load_list(list, count, ini, group);
    ini_free(ini);
    return BK_LOAD_OK;
}

/**
 * @brief Load the bookkeeping history and favorites from their files.
 *
 * Favorites and history live in separate files. If a new file is missing, the
 * legacy combined file (if present) is used to migrate the list, after which
 * the new split files are written out.
 *
 * @param history Pointer to the bookkeeping structure.
 */
void bookkeeping_load (bookkeeping_t *history) {
    file_presence_t fav_presence = favorites_path ? file_presence(favorites_path) : FILE_PRESENCE_ABSENT;
    file_presence_t hist_presence = history_path ? file_presence(history_path) : FILE_PRESENCE_ABSENT;

    bool have_fav = (fav_presence == FILE_PRESENCE_PRESENT);
    bool have_hist = (hist_presence == FILE_PRESENCE_PRESENT);
    bool have_legacy = legacy_path && file_exists(legacy_path);

    /* Only fall back to the legacy combined file when the split file is CONFIRMED missing.
       If it's merely unreadable, loading legacy data would silently present a stale list. */
    const char *hist_src = have_hist ? history_path
                         : (hist_presence == FILE_PRESENCE_ABSENT && have_legacy ? legacy_path : NULL);
    const char *fav_src = have_fav ? favorites_path
                        : (fav_presence == FILE_PRESENCE_ABSENT && have_legacy ? legacy_path : NULL);

    /* Favorites/history keys are unique by construction -- skip the per-insert
       dedup scan so parsing the large favorites file is O(N), not O(N^2). */
    ini_assume_unique_keys(true);
    bk_load_result_t hist_res = bookkeeping_load_list_from(history->history_items, HISTORY_COUNT, hist_src, "history");
    bk_load_result_t fav_res = bookkeeping_load_list_from(history->favorite_items, FAVORITES_COUNT, fav_src, "favorite");
    ini_assume_unique_keys(false);

    /* An unreadable file must survive the session untouched: block every later write too
       (the browser flushes pending toggles on exit, the grid saves on sort/clear/reorder),
       not just the create-on-first-boot below. Cleared on the next successful load. */
    history_write_blocked = (hist_presence == FILE_PRESENCE_UNKNOWN) || (hist_res == BK_LOAD_UNREADABLE);
    favorites_write_blocked = (fav_presence == FILE_PRESENCE_UNKNOWN) || (fav_res == BK_LOAD_UNREADABLE);

    /* Create the new split files on first boot, or migrate from the legacy file. Guarded by
       the blocks above so a read failure can never be written back as an empty list. */
    if (!have_hist) {
        bookkeeping_save_history(history);
    }
    if (!have_fav) {
        bookkeeping_save_favorites(history);
    }
}

/**
 * @brief Save a list of bookkeeping items to an INI file.
 * 
 * @param list Pointer to the list of bookkeeping items.
 * @param count Number of items in the list.
 * @param ini Pointer to the INI file structure.
 * @param group Name of the group in the INI file.
 */
static void bookkeeping_ini_save_list(bookkeeping_item_t *list, uint16_t count, ini_t *ini, const char *group) {
    char buf[64];
    for(uint16_t i = 0; i < count; i++) {
        /* The list is compacted (removals shift down), so the first empty slot is the
           end. Writing only populated slots shrinks the file from ~6k pairs (every slot)
           to a few per favorite -- the file size is what made load O(N^2). */
        if (list[i].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) break;

        snprintf(buf, sizeof(buf), "%d_primary_path", i);
        path_t* path = list[i].primary_path;
        ini_set_string(ini, group, buf, path != NULL ? path_get(path) : "");

        snprintf(buf, sizeof(buf), "%d_secondary_path", i);
        path = list[i].secondary_path;
        ini_set_string(ini, group, buf, path != NULL ? path_get(path) : "");   

        snprintf(buf, sizeof(buf), "%d_type", i);
        ini_set_int(ini, group, buf, list[i].bookkeeping_type);

        /* Only write the cached fields when set, to keep the file (and pair count)
           small for entries that don't use them. */
        if (list[i].game_code[0]) {
            snprintf(buf, sizeof(buf), "%d_game_code", i);
            ini_set_string(ini, group, buf, list[i].game_code);
        }
        if (list[i].presents_as != 0) {
            snprintf(buf, sizeof(buf), "%d_presents_as", i);
            ini_set_int(ini, group, buf, list[i].presents_as);
        }
        if (list[i].sort_initial) {
            char sv[2] = { list[i].sort_initial, '\0' };
            snprintf(buf, sizeof(buf), "%d_sort_initial", i);
            ini_set_string(ini, group, buf, sv);
        }
        if (list[i].sort_name[0]) {
            snprintf(buf, sizeof(buf), "%d_sort_name", i);
            ini_set_string(ini, group, buf, list[i].sort_name);
        }
    }
}

/**
 * @brief Save a single list to its own INI file.
 */
static void bookkeeping_save_list_to(bookkeeping_item_t *list, uint16_t count, const char *path, const char *group) {
    if (!path) {
        return;
    }
    ini_t *bookkeeping_ini = ini_create();
    if (bookkeeping_ini == NULL) {
        debugf("[BOOKKEEPING] Failed to create INI structure\n");
        return;
    }
    /* Keys are generated uniquely (index-prefixed) -- skip the dedup scan so the
       build is O(N). Without this, saving ~947 favorites was a multi-second freeze
       (the History "favorite hangs" bug). */
    ini_assume_unique_keys(true);
    bookkeeping_ini_save_list(list, count, bookkeeping_ini, group);
    ini_assume_unique_keys(false);
    if (!ini_save(bookkeeping_ini, path)) {
        debugf("[BOOKKEEPING] Failed to save to %s\n", path);
    }
    ini_free(bookkeeping_ini);
}

/**
 * @brief Save only the history file.
 *
 * @param history Pointer to the bookkeeping structure.
 */
void bookkeeping_save_history (bookkeeping_t *history) {
    if (history_write_blocked) {
        debugf("[BOOKKEEPING] history.ini unreadable this session -- refusing to overwrite it\n");
        return;
    }
    bookkeeping_save_list_to(history->history_items, HISTORY_COUNT, history_path, "history");
}

/**
 * @brief Save only the favorites file.
 *
 * @param history Pointer to the bookkeeping structure.
 */
void bookkeeping_save_favorites (bookkeeping_t *history) {
    if (favorites_write_blocked) {
        debugf("[BOOKKEEPING] favorites.ini unreadable this session -- refusing to overwrite it\n");
        return;
    }
    bookkeeping_save_list_to(history->favorite_items, FAVORITES_COUNT, favorites_path, "favorite");
}

/**
 * @brief Save both the history and favorites files.
 *
 * @param history Pointer to the bookkeeping structure.
 */
void bookkeeping_save (bookkeeping_t *history) {
    bookkeeping_save_history(history);
    bookkeeping_save_favorites(history);
}

/**
 * @brief Check if two bookkeeping items match.
 * 
 * @param left Pointer to the first bookkeeping item.
 * @param right Pointer to the second bookkeeping item.
 * @return true if the items match, false otherwise.
 */
static bool bookkeeping_item_match(bookkeeping_item_t *left, bookkeeping_item_t *right) {
    if(left != NULL && right != NULL) {
        return path_are_match(left->primary_path, right->primary_path) && path_are_match(left->secondary_path, right->secondary_path) && left->bookkeeping_type == right->bookkeeping_type;
    }

    return false;
}

/**
 * @brief Clear a bookkeeping item.
 * 
 * @param item Pointer to the bookkeeping item.
 * @param leave_null Flag indicating whether to leave the paths as NULL.
 */
static void bookkeeping_clear_item(bookkeeping_item_t *item, bool leave_null) {
    if(item->primary_path != NULL){
        path_free(item->primary_path);

        if(leave_null) {
            item->primary_path = NULL;
        } else {
            item->primary_path = path_create("");
        }
    }
    if(item->secondary_path != NULL){
        path_free(item->secondary_path);

        if(leave_null) {
            item->secondary_path = NULL;
        } else {
            item->secondary_path = path_create("");
        }
    }
    item->bookkeeping_type = BOOKKEEPING_TYPE_EMPTY;
    item->game_code[0] = '\0';
    item->presents_as = 0;
    item->sort_initial = 0;
    item->sort_name[0] = '\0';
}

/**
 * @brief Copy a bookkeeping item.
 *
 * @param source Pointer to the source bookkeeping item.
 * @param destination Pointer to the destination bookkeeping item.
 */
static void bookkeeping_copy_item(bookkeeping_item_t *source, bookkeeping_item_t *destination) {
    bookkeeping_clear_item(destination, true);

    destination->primary_path =  source->primary_path != NULL ? path_clone(source->primary_path) : path_create("");
    destination->secondary_path = source->secondary_path != NULL ? path_clone(source->secondary_path) : path_create("");
    destination->bookkeeping_type = source->bookkeeping_type;
    memcpy(destination->game_code, source->game_code, sizeof(destination->game_code));
    destination->presents_as = source->presents_as;
    destination->sort_initial = source->sort_initial;
    memcpy(destination->sort_name, source->sort_name, sizeof(destination->sort_name));
}

/**
 * @brief Move bookkeeping items down in the list.
 * 
 * @param list Pointer to the list of bookkeeping items.
 * @param start Start index.
 * @param end End index.
 */
static void bookkeeping_move_items_down(bookkeeping_item_t *list, int start, int end) {
    int current = end;

    do {
        if(current <= start || current < 0) {
            break;
        }        

        bookkeeping_copy_item(&list[current - 1], &list[current]);
        current--;
    } while(true);
}

/**
 * @brief Move bookkeeping items up in the list.
 * 
 * @param list Pointer to the list of bookkeeping items.
 * @param start Start index.
 * @param end End index.
 */
static void bookkeeping_move_items_up(bookkeeping_item_t *list, int start, int end) {
    int current = start;

    do {
        if(current >= end) {
            break;
        }        

        bookkeeping_copy_item(&list[current + 1], &list[current]);
        current++;
    } while(true);
}

/**
 * @brief Insert a bookkeeping item at the top of the list.
 * 
 * @param list Pointer to the list of bookkeeping items.
 * @param count Number of items in the list.
 * @param new_item Pointer to the new bookkeeping item.
 */
static void bookkeeping_insert_top(bookkeeping_item_t *list, int count, bookkeeping_item_t *new_item) {
    // if it matches the top of the list already then nothing to do
    if(bookkeeping_item_match(&list[0], new_item)) {
        return;
    }

    // if the top isn't empty then we need to move things around
    if(list[0].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
        int found_at = -1;
        for(int i = 1; i < count; i++) {
            if(bookkeeping_item_match(&list[i], new_item)){
                found_at = i;
                break;
            }
        }

        if(found_at == -1) {
            bookkeeping_move_items_down(list, 0, count - 1);
        } else {
            bookkeeping_move_items_down(list, 0, found_at);
        }
    }
    
    bookkeeping_copy_item(new_item, &list[0]);
}

/**
 * @brief Add a new item to the bookkeeping history.
 * 
 * @param bookkeeping Pointer to the bookkeeping structure.
 * @param primary_path Pointer to the primary path.
 * @param secondary_path Pointer to the secondary path.
 * @param type The type of the bookkeeping item.
 */
void bookkeeping_history_add(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type) {
    bookkeeping_item_t new_item = {
        .primary_path = primary_path,
        .secondary_path = secondary_path,
        .bookkeeping_type = type
    };

    bookkeeping_insert_top(bookkeeping->history_items, HISTORY_COUNT, &new_item);
    bookkeeping_save_history(bookkeeping);
}

/**
 * @brief Add a new item to the bookkeeping favorites.
 * 
 * @param bookkeeping Pointer to the bookkeeping structure.
 * @param primary_path Pointer to the primary path.
 * @param secondary_path Pointer to the secondary path.
 * @param type The type of the bookkeeping item.
 */
/* In-RAM add, no SD write. Use for bulk operations (folder-fav) and save ONCE at
   the end — favorites.ini is rewritten whole on each save, so per-item saving makes
   a bulk add O(N^2). */
void bookkeeping_favorite_add_nosave(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type) {
    bookkeeping_item_t new_item = {
        .primary_path = primary_path,
        .secondary_path = secondary_path,
        .bookkeeping_type = type
    };
    bookkeeping_insert_top(bookkeeping->favorite_items, FAVORITES_COUNT, &new_item);
}

void bookkeeping_favorite_add(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type) {
    bookkeeping_favorite_add_nosave(bookkeeping, primary_path, secondary_path, type);
    bookkeeping_save_favorites(bookkeeping);
}

/* In-RAM remove, no SD write (see bookkeeping_favorite_add_nosave). */
void bookkeeping_favorite_remove_nosave(bookkeeping_t *bookkeeping, int selection) {
    if(bookkeeping->favorite_items[selection].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
        bookkeeping_move_items_up(bookkeeping->favorite_items, selection, FAVORITES_COUNT - 1);
        bookkeeping_clear_item(&bookkeeping->favorite_items[FAVORITES_COUNT - 1], false);
    }
}

/**
 * @brief Remove an item from the bookkeeping favorites.
 *
 * @param bookkeeping Pointer to the bookkeeping structure.
 * @param selection Index of the item to remove.
 */
void bookkeeping_favorite_remove(bookkeeping_t *bookkeeping, int selection) {
    if(bookkeeping->favorite_items[selection].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
        bookkeeping_favorite_remove_nosave(bookkeeping, selection);
        bookkeeping_save_favorites(bookkeeping);
    }
}

/**
 * @brief Remove all favorites and persist the empty list.
 *
 * @param bookkeeping Pointer to the bookkeeping structure.
 */
void bookkeeping_favorite_clear_all(bookkeeping_t *bookkeeping) {
    for (int i = 0; i < FAVORITES_COUNT; i++) {
        bookkeeping_clear_item(&bookkeeping->favorite_items[i], false);
    }
    bookkeeping_save_favorites(bookkeeping);
}

/**
 * @brief Remove all history entries and persist the empty list.
 *
 * @param bookkeeping Pointer to the bookkeeping structure.
 */
void bookkeeping_history_clear_all(bookkeeping_t *bookkeeping) {
    for (int i = 0; i < HISTORY_COUNT; i++) {
        bookkeeping_clear_item(&bookkeeping->history_items[i], false);
    }
    bookkeeping_save_history(bookkeeping);
}
