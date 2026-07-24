/**
 * @file bookkeeping.h
 * @brief Bookkeeping of loaded ROMs.
 * @ingroup menu 
 */

#ifndef BOOKKEEPING_H__
#define BOOKKEEPING_H__

#include "path.h"

#define FAVORITES_COUNT 2048 /**< Maximum number of favorite items */
#define HISTORY_COUNT 64 /**< Maximum number of history items */

/** @brief Bookkeeping item types enumeration */
typedef enum {
    BOOKKEEPING_TYPE_EMPTY, /**< Empty item */
    BOOKKEEPING_TYPE_ROM, /**< ROM item */
    BOOKKEEPING_TYPE_DISK, /**< Disk item */
} bookkeeping_item_types_t;

/** @brief Bookkeeping item structure */
typedef struct {
    path_t *primary_path; /**< Primary path */
    path_t *secondary_path; /**< Secondary path */
    bookkeeping_item_types_t bookkeeping_type; /**< Bookkeeping item type */
    char game_code[5];      /**< Cached 4-char ROM code (favorites; "" = unknown). Lets the
                                 grid skip the ROM header read on boot. */
    int  presents_as;       /**< Per-game region override (rom_presents_as_t value; favorites).
                                 Stored here so the grid reflects it without reading config. */
    char sort_initial;      /**< Cached A-Z section letter (favorites; 0 = unknown). Set by the
                                 sort so later boots skip re-reading names and C-jump works. */
    char sort_name[21];     /**< Cached sort name for games NOT in the metadata DB (favorites;
                                 "" = none/use DB). Persisted so a re-sort needn't re-read the
                                 ROM header for the title every time. */
} bookkeeping_item_t;

/** @brief ROM bookkeeping structure */
typedef struct {
    bookkeeping_item_t history_items[HISTORY_COUNT]; /**< History items */
    bookkeeping_item_t favorite_items[FAVORITES_COUNT]; /**< Favorite items */
} bookkeeping_t;

/**
 * @brief Initialize ROM bookkeeping paths.
 *
 * Favorites and history are stored in separate files (under /menu/n64ever).
 * If neither new file exists, the (optional) legacy combined file is used to
 * migrate the lists on first load.
 *
 * @param favorites_path        Path to the favorites file (favorites.ini).
 * @param history_path          Path to the history file (history.ini).
 * @param legacy_combined_path  Path to the pre-split combined file, or NULL.
 */
void bookkeeping_init(const char *favorites_path, const char *history_path, const char *legacy_combined_path);

/**
 * @brief Load ROM bookkeeping (favorites + history).
 *
 * @param history Pointer to the bookkeeping structure to load.
 */
void bookkeeping_load(bookkeeping_t *history);

/**
 * @brief Save both favorites and history files.
 *
 * @param history Pointer to the bookkeeping structure to save.
 */
void bookkeeping_save(bookkeeping_t *history);

/**
 * @brief Save only the history file.
 *
 * @param history Pointer to the bookkeeping structure to save.
 */
void bookkeeping_save_history(bookkeeping_t *history);

/**
 * @brief Save only the favorites file.
 *
 * @param history Pointer to the bookkeeping structure to save.
 */
void bookkeeping_save_favorites(bookkeeping_t *history);

/**
 * @brief Add a ROM to the history.
 * 
 * @param bookkeeping The bookkeeping structure.
 * @param primary_path The primary path of the ROM.
 * @param secondary_path The secondary path of the ROM.
 * @param type The type of the bookkeeping item.
 */
void bookkeeping_history_add(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type);

/**
 * @brief Add a ROM to the favorites.
 * 
 * @param bookkeeping The bookkeeping structure.
 * @param primary_path The primary path of the ROM.
 * @param secondary_path The secondary path of the ROM.
 * @param type The type of the bookkeeping item.
 */
void bookkeeping_favorite_add(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type);

/** @brief Add a favorite WITHOUT persisting (bulk ops; caller saves once at the end). */
void bookkeeping_favorite_add_nosave(bookkeeping_t *bookkeeping, path_t *primary_path, path_t *secondary_path, bookkeeping_item_types_t type);

/**
 * @brief Remove a ROM from the favorites.
 *
 * @param bookkeeping The bookkeeping structure.
 * @param selection The index of the favorite item to remove.
 */
void bookkeeping_favorite_remove(bookkeeping_t *bookkeeping, int selection);

/** @brief Remove a favorite WITHOUT persisting (bulk ops; caller saves once at the end). */
void bookkeeping_favorite_remove_nosave(bookkeeping_t *bookkeeping, int selection);

/**
 * @brief Remove all favorites and persist the empty list.
 *
 * @param bookkeeping The bookkeeping structure.
 */
void bookkeeping_favorite_clear_all(bookkeeping_t *bookkeeping);

/**
 * @brief Remove all history entries and persist the empty list.
 *
 * @param bookkeeping The bookkeeping structure.
 */
void bookkeeping_history_clear_all(bookkeeping_t *bookkeeping);

#endif /* BOOKKEEPING_H__ */
