/**
 * @file disclink.c
 * @brief 64DD disc -> base-ROM link setting (per region, keyed by game code).
 */

#include "disclink.h"

#include <stdio.h>
#include <string.h>

/* Build the region file path: <prefix>menu/n64ever/disclink_{jp,us}.ini.
   Region from the code's 4th char: 'J' -> jp, else -> us. */
static void disclink_path (const char *prefix, const char *code, char *out, size_t n) {
    const char *region = (code[3] == 'J') ? "jp" : "us";
    snprintf(out, n, "%smenu/n64ever/disclink_%s.ini", prefix ? prefix : "sd:/", region);
}

/* True if line `p` (already left-trimmed) is the entry for `code` (exact 4-char key). */
static bool line_is_code (const char *p, const char *code) {
    return strncmp(p, code, 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == '=');
}

bool disclink_lookup (const char *prefix, const char *code, char *out_path, size_t out_sz) {
    if (!code || strlen(code) < 4 || !out_path || out_sz == 0) return false;
    char fn[256];
    disclink_path(prefix, code, fn, sizeof fn);
    FILE *f = fopen(fn, "rb");
    if (!f) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!line_is_code(p, code)) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = '\0';
        if (*v) {
            strncpy(out_path, v, out_sz - 1);
            out_path[out_sz - 1] = '\0';
            found = true;
        }
        break;
    }
    fclose(f);
    return found;
}

bool disclink_has (const char *prefix, const char *code) {
    char tmp[256];
    return disclink_lookup(prefix, code, tmp, sizeof tmp);
}

void disclink_store (const char *prefix, const char *code, const char *base_path) {
    if (!code || strlen(code) < 4 || !base_path || !*base_path) return;
    char fn[256];
    disclink_path(prefix, code, fn, sizeof fn);

    /* Read existing entries (minus the one being replaced) so we update in place.
       static (not stack) to keep the frame small on the N64. */
    static char keep[32][256];
    int nkeep = 0;
    FILE *f = fopen(fn, "rb");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f) && nkeep < 32) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (line_is_code(p, code)) continue;        /* drop the old mapping for this code */
            char *e = line + strlen(line);
            while (e > line && (e[-1] == '\n' || e[-1] == '\r')) e--;
            *e = '\0';
            if (line[0]) {
                strncpy(keep[nkeep], line, 255);
                keep[nkeep][255] = '\0';
                nkeep++;
            }
        }
        fclose(f);
    }

    FILE *w = fopen(fn, "wb");
    if (!w) return;
    for (int i = 0; i < nkeep; i++) fprintf(w, "%s\n", keep[i]);
    char c4[5];
    memcpy(c4, code, 4);
    c4[4] = '\0';
    fprintf(w, "%s = %s\n", c4, base_path);
    fclose(w);
}

void disclink_clear_all (const char *prefix) {
    const char *p = prefix ? prefix : "sd:/";
    char fn[256];
    snprintf(fn, sizeof fn, "%smenu/n64ever/disclink_us.ini", p);
    remove(fn);
    snprintf(fn, sizeof fn, "%smenu/n64ever/disclink_jp.ini", p);
    remove(fn);
}
