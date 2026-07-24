/**
 * @file game_special.h
 * @brief Filename-keyed "special edition" overrides for ROMs that reuse another
 *        game's NUS code (re-releases, certain romhacks). The built-in metadata DB
 *        is keyed by the 4-char code, so these would otherwise be indistinguishable
 *        from their base game (e.g. Ocarina of Time Master Quest ships under the
 *        regular OoT codes CZLE / NZLP). We disambiguate on the ROM FILENAME: every
 *        entry lists lowercase tokens that must ALL appear in the name.
 *
 * Each entry carries its own metadata AND an art_code: a 4-char DFS key under
 * rom:/boxart/<art_code>/ where this edition's baked cover/cart art lives, separate
 * from the colliding base-game code. (Master Quest's real product code is NUS-ZMQ.)
 */

#ifndef GAME_SPECIAL_H__
#define GAME_SPECIAL_H__

typedef struct {
    const char *tokens[4];   /* lowercase substrings; ALL must appear in the filename (NULL-terminated) */
    const char *art_code;    /* 4-char DFS art key (rom:/boxart/<art_code>/<type>.sprite) */
    const char *title;
    const char *developer;
    const char *release_jp;
    const char *release_us;
    const char *release_eu;
    const char *description;
} game_special_t;

/* Match a ROM filename (basename, any case) against the table. Returns the index, or -1. */
int game_special_match(const char *filename);

/* Hardware variant of a ROM, inferred from its filename. Aleck64 (Seta arcade board) and
   iQue (China-only N64 variant) ROMs are significant hardware alterations that often will
   NOT boot on a stock N64 / SC64 -- the UI flags them so the user is warned. */
typedef enum {
    GAME_PLATFORM_N64 = 0,
    GAME_PLATFORM_ALECK64,
    GAME_PLATFORM_IQUE,
} game_platform_t;

game_platform_t game_platform_classify(const char *filename);

/* Build/release type of a ROM, inferred from conventional dump-tag tokens in the
   FILENAME ("(Demo)", "(Proto)", "(Beta)", "(Sample)", "Kiosk", ...). Demos, protos
   and betas almost never carry their own box/cart art and often use non-retail NUS
   codes (so they can't borrow a region sibling's art either) -- the UI labels them
   so a missing cover reads as "this is a demo", not a bug. */
typedef enum {
    GAME_BUILD_RETAIL = 0,
    GAME_BUILD_DEMO,
    GAME_BUILD_PROTO,
    GAME_BUILD_BETA,
} game_build_t;

game_build_t game_build_classify(const char *filename);

/* Short UI label for a build type ("Demo"/"Prototype"/"Beta"), or NULL for retail. */
const char *game_build_label(game_build_t build);

/* Fetch an entry by index (NULL if out of range). */
const game_special_t *game_special_get(int index);

#endif /* GAME_SPECIAL_H__ */
