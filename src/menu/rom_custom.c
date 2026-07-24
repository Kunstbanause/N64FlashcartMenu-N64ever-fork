#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rom_custom.h"

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* Parse a JSON string starting at the opening '"'. Returns pointer after closing '"' or NULL. */
static const char *parse_str(const char *p, char *out, size_t n) {
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            char c = *p;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
            if (i + 1 < n) out[i++] = c;
        } else {
            if (i + 1 < n) out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return (*p == '"') ? p + 1 : NULL;
}

/* Skip any JSON value (string, number, bool, null, object, array). */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
        if (*p == '"') p++;
    } else if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
                if (*p == '"') p++;
            } else if (*p == open) { depth++; p++; }
            else if (*p == close) { depth--; p++; }
            else p++;
        }
    } else {
        while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    }
    return p;
}

bool rom_custom_load(const char *storage_prefix, const char *rom_full_path, rom_custom_t *out) {
    memset(out, 0, sizeof(*out));

    const char *slash = strrchr(rom_full_path, '/');
    const char *fname = slash ? slash + 1 : rom_full_path;
    char stem[256];
    strncpy(stem, fname, sizeof(stem) - 1); stem[sizeof(stem) - 1] = '\0';
    char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';

    char json_path[360];
    snprintf(json_path, sizeof(json_path), "%s/menu/n64ever/gameconfigs/%s.json", storage_prefix, stem);

    FILE *f = fopen(json_path, "r");
    if (!f) {
        /* Backward compat: pre-relocation /menu/custom/<stem>.json */
        snprintf(json_path, sizeof(json_path), "%s/menu/custom/gameconfigs/%s.json", storage_prefix, stem);
        f = fopen(json_path, "r");
    }
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 8192) { fclose(f); return false; }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[rd] = '\0';

    const char *p = skip_ws(buf);
    if (*p != '{') { free(buf); return false; }
    p++;

    char key[64];
    while (1) {
        p = skip_ws(p);
        if (!*p || *p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') { p++; continue; }

        const char *after = parse_str(p, key, sizeof(key));
        if (!after) break;
        p = skip_ws(after);
        if (*p != ':') break;
        p++;
        p = skip_ws(p);

        if (strcmp(key, "description") == 0 && *p == '"') {
            after = parse_str(p, out->description, sizeof(out->description));
            if (after) { out->has_description = (out->description[0] != '\0'); p = after; }
            else { p = skip_value(p); }
        } else if (strcmp(key, "fields") == 0 && *p == '[') {
            p++;
            while (*p) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                if (*p != '{') { p = skip_value(p); continue; }
                p++;
                char lbl[64] = "", val[128] = "";
                while (1) {
                    p = skip_ws(p);
                    if (!*p || *p == '}') break;
                    if (*p == ',') { p++; continue; }
                    if (*p != '"') { p++; continue; }
                    char fkey[32];
                    after = parse_str(p, fkey, sizeof(fkey));
                    if (!after) break;
                    p = skip_ws(after);
                    if (*p != ':') break;
                    p++;
                    p = skip_ws(p);
                    if (*p == '"') {
                        if (strcmp(fkey, "label") == 0)
                            after = parse_str(p, lbl, sizeof(lbl));
                        else if (strcmp(fkey, "value") == 0)
                            after = parse_str(p, val, sizeof(val));
                        else
                            after = skip_value(p);
                        if (after) p = after;
                    } else { p = skip_value(p); }
                }
                if (*p == '}') p++;
                if (out->field_count < ROM_CUSTOM_MAX_FIELDS) {
                    strncpy(out->fields[out->field_count].label, lbl, sizeof(lbl) - 1);
                    strncpy(out->fields[out->field_count].value, val, sizeof(val) - 1);
                    out->field_count++;
                }
            }
        } else {
            p = skip_value(p);
        }
    }

    free(buf);
    return out->has_description || out->field_count > 0;
}
