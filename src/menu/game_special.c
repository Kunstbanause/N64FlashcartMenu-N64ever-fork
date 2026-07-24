/**
 * @file game_special.c
 * @brief Filename-keyed special-edition overrides (see game_special.h).
 */

#include <stdbool.h>
#include <string.h>
#include "game_special.h"

/* The full original Ocarina of Time blurb, reused as the tail of the Master Quest text. */
#define OOT_FULL_DESC \
    "Journey across Hyrule as the young hero Link, wielding sword, shield and the magical " \
    "Ocarina to travel through time and stop Ganondorf's bid for the Triforce. Master the bow, " \
    "hookshot and bombs, raise stalwart allies and grow from boy to Hero of Time in one of " \
    "gaming's defining adventures."

static const game_special_t specials[] = {
    {   /* The Legend of Zelda: Ocarina of Time Master Quest -- ships under the regular OoT
           codes (CZLE in the US dump, NZLP in the EU dump); only the filename distinguishes it.
           Real product code NUS-ZMQ-USA -> art baked under rom:/boxart/ZMQE/. */
        .tokens     = { "master", "quest", NULL, NULL },
        .art_code   = "ZMQE",
        .title      = "The Legend of Zelda: Ocarina of Time Master Quest",
        .developer  = "Nintendo",
        .release_jp = "2002-11-28",
        .release_us = "2002-11-28",
        .release_eu = "2002-11-28",
        .description = "A new adventure awaits in a mirrored world. " OOT_FULL_DESC,
    },
    {   /* Smash Remix -- a gameplay mod of Super Smash Bros. 64; ships under the base game's
           codes (NALE/NALP/NALJ depending on the source ROM). Matched on the filename so any
           region/version works. Art baked under rom:/boxart/SMRX/. */
        .tokens     = { "smash", "remix", NULL, NULL },
        .art_code   = "SMRX",
        .title      = "Smash Remix",
        .developer  = "JSsixtyfour",
        .release_jp = "2019-05-13",
        .release_us = "2019-05-13",
        .release_eu = "2019-05-13",
        .description = "Smash Remix is a gameplay mod for Super Smash Bros. on the Nintendo 64. "
                       "The goal is to expand Smash 64 with new characters, stages, etc. while "
                       "staying true to its core gameplay. New mechanics from the other Smash Bros. "
                       "games, like wave dashing, are not added. The vision involves having "
                       "tournaments where the new characters are pitched against the old ones, all "
                       "the clones being viable against the originals.",
    },
};

#define N_SPECIAL ((int)(sizeof(specials) / sizeof(specials[0])))

int game_special_match(const char *filename) {
    if (!filename || !filename[0]) return -1;

    /* Lowercase copy of the basename (drop any directory prefix). */
    const char *slash = strrchr(filename, '/');
    const char *base = slash ? slash + 1 : filename;
    char low[256];
    int n = 0;
    for (const char *s = base; *s && n < (int)sizeof(low) - 1; s++) {
        char c = *s;
        low[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    low[n] = '\0';

    for (int i = 0; i < N_SPECIAL; i++) {
        bool all = true;
        for (int t = 0; t < 4 && specials[i].tokens[t]; t++) {
            if (!strstr(low, specials[i].tokens[t])) { all = false; break; }
        }
        if (all) return i;
    }
    return -1;
}

const game_special_t *game_special_get(int index) {
    if (index < 0 || index >= N_SPECIAL) return NULL;
    return &specials[index];
}

game_platform_t game_platform_classify(const char *filename) {
    if (!filename || !filename[0]) return GAME_PLATFORM_N64;
    const char *slash = strrchr(filename, '/');
    const char *base = slash ? slash + 1 : filename;
    char low[256];
    int n = 0;
    for (const char *s = base; *s && n < (int)sizeof(low) - 1; s++) {
        char c = *s;
        low[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    low[n] = '\0';
    if (strstr(low, "aleck"))                          return GAME_PLATFORM_ALECK64;
    if (strstr(low, "ique") || strstr(low, "(china)")) return GAME_PLATFORM_IQUE;
    return GAME_PLATFORM_N64;
}

game_build_t game_build_classify(const char *filename) {
    if (!filename || !filename[0]) return GAME_BUILD_RETAIL;
    const char *slash = strrchr(filename, '/');
    const char *base = slash ? slash + 1 : filename;
    char low[256];
    int n = 0;
    for (const char *s = base; *s && n < (int)sizeof(low) - 1; s++) {
        char c = *s;
        low[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    low[n] = '\0';
    /* Match the No-Intro / GoodN64 dump-tag conventions. The leading '(' avoids false
       hits on real titles ("Demolition Racer" contains "demo"); "kiosk" is distinctive
       enough to match bare. Most specific first. "(proto" also covers "(Prototype)". */
    if (strstr(low, "(proto"))                       return GAME_BUILD_PROTO;
    if (strstr(low, "(beta") || strstr(low, "(alpha")) return GAME_BUILD_BETA;
    if (strstr(low, "(demo") || strstr(low, "(sample") ||
        strstr(low, "(preview") || strstr(low, "kiosk")) return GAME_BUILD_DEMO;
    return GAME_BUILD_RETAIL;
}

const char *game_build_label(game_build_t build) {
    switch (build) {
        case GAME_BUILD_DEMO:  return "Demo";
        case GAME_BUILD_PROTO: return "Prototype";
        case GAME_BUILD_BETA:  return "Beta";
        default:               return NULL;
    }
}
