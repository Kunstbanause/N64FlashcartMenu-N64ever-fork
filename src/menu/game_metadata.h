/**
 * @file game_metadata.h
 * @brief Built-in game metadata database with user-override support.
 *
 * Priority order for each game (highest wins):
 *  1. User file:  <storage_prefix>/menu/n64ever/gameconfigs/<GAMECODE>.meta.ini
 *  2. Built-in:   game_metadata_db.h (compiled into ROM)
 *  3. Caller falls through to rom_info fields
 *
 * The lookup key is the 3-character base code (game_code[0..2]).
 * Region variants (NSME / NSMJ / NSMP) all resolve to the same entry.
 * Regional release dates are then picked by the caller based on the
 * actual region byte (game_code[3]).
 *
 * Custom file format  (<GAMECODE>.meta.ini, e.g. NSME.meta.ini):
 *   [meta]
 *   title=Super Mario 64
 *   developer=Nintendo
 *   release_jp=1996-06-23
 *   release_us=1996-09-29
 *   release_eu=1997-03-01
 *   description=The landmark 3D platformer that defined an era.
 *
 * All fields are optional; missing fields fall through to the DB or rom_info.
 */

#ifndef GAME_METADATA_H__
#define GAME_METADATA_H__

#include <stdbool.h>

/** @brief Metadata for a single game (all pointer fields may be NULL). */
typedef struct {
    const char *title;        /**< Official title                        */
    const char *developer;    /**< Developer / publisher                 */
    const char *release_jp;   /**< Japan release date  "YYYY-MM-DD"      */
    const char *release_us;   /**< North America release "YYYY-MM-DD"    */
    const char *release_eu;   /**< Europe/PAL release  "YYYY-MM-DD"      */
    const char *description;  /**< Short description (1–3 sentences)     */
} game_meta_t;

/**
 * @brief Look up metadata for a game.
 *
 * Checks the user's custom file first, then the built-in database.
 * The returned pointers are valid until the next call to this function
 * (static internal buffers back custom-file strings; DB strings are
 * in ROM and are permanently valid).
 *
 * @param storage_prefix   SD card prefix (e.g. "sd:/")
 * @param game_code        4-char game code from ROM header (e.g. "NSME")
 * @param out              Filled with found metadata; any field may be NULL
 * @return true if at least title or developer was found; false = no data
 */
bool game_metadata_get(const char *storage_prefix, const char *game_code, game_meta_t *out);

/**
 * @brief Look up metadata in the built-in database ONLY (no SD file I/O).
 *
 * Skips the per-game custom-override file probe that game_metadata_get does, so
 * it's instant. Used when "Use custom files" is off.
 *
 * @param game_code 4-char game code from ROM header
 * @param out       Filled with DB metadata (pointers into ROM; valid forever)
 * @return true if the game was found in the database
 */
bool game_metadata_db_lookup(const char *game_code, game_meta_t *out);

/**
 * @brief Number of entries in the built-in metadata database.
 */
int game_metadata_db_count(void);

/**
 * @brief 3-char base code of database entry @p index (NULL if out of range).
 *        Used to pick random games for the screensaver's cover-art wall.
 */
const char *game_metadata_db_base(int index);

#endif /* GAME_METADATA_H__ */
