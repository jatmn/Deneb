/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Minimal JSON writer implementation.
 */

#include "json_writer.h"
#include <stdio.h>
#include <string.h>

void json_init(json_writer_t *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->pos = 0;
    w->cap = cap;
    w->depth = 0;
    w->need_comma = 0;
    if (cap > 0) buf[0] = '\0';
}

static void put(json_writer_t *w, char c)
{
    if (w->pos < w->cap - 1) w->buf[w->pos++] = c;
}

static void puts_n(json_writer_t *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n && w->pos < w->cap - 1; i++)
        w->buf[w->pos++] = s[i];
}

static void puts_s(json_writer_t *w, const char *s)
{
    while (*s && w->pos < w->cap - 1) w->buf[w->pos++] = *s++;
}

static void comma(json_writer_t *w)
{
    if (w->need_comma) put(w, ',');
    w->need_comma = 1;
}

static void write_escaped_str(json_writer_t *w, const char *s)
{
    put(w, '"');
    while (*s) {
        char c = *s++;
        if (c == '"' || c == '\\') { put(w, '\\'); put(w, c); }
        else if (c == '\n') { put(w, '\\'); put(w, 'n'); }
        else if (c == '\r') { put(w, '\\'); put(w, 'r'); }
        else if (c == '\t') { put(w, '\\'); put(w, 't'); }
        else put(w, c);
    }
    put(w, '"');
}

static void write_key(json_writer_t *w, const char *key)
{
    comma(w);
    write_escaped_str(w, key);
    put(w, ':');
}

void json_obj_open(json_writer_t *w)
{
    comma(w);
    put(w, '{');
    w->depth++;
    w->need_comma = 0;
}

void json_obj_close(json_writer_t *w)
{
    put(w, '}');
    w->depth--;
    w->need_comma = 1;
}

void json_arr_open(json_writer_t *w)
{
    comma(w);
    put(w, '[');
    w->depth++;
    w->need_comma = 0;
}

void json_arr_close(json_writer_t *w)
{
    put(w, ']');
    w->depth--;
    w->need_comma = 1;
}

void json_str(json_writer_t *w, const char *key, const char *val)
{
    write_key(w, key);
    write_escaped_str(w, val ? val : "");
}

void json_int(json_writer_t *w, const char *key, long long val)
{
    write_key(w, key);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%lld", val);
    puts_s(w, tmp);
}

void json_float(json_writer_t *w, const char *key, double val)
{
    write_key(w, key);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.1f", val);
    puts_s(w, tmp);
}

void json_bool(json_writer_t *w, const char *key, int val)
{
    write_key(w, key);
    puts_s(w, val ? "true" : "false");
}

void json_null(json_writer_t *w, const char *key)
{
    write_key(w, key);
    puts_s(w, "null");
}

void json_raw(json_writer_t *w, const char *key, const char *raw_json)
{
    write_key(w, key);
    puts_s(w, raw_json);
}

void json_key(json_writer_t *w, const char *key)
{
    comma(w);
    write_escaped_str(w, key);
    put(w, ':');
    w->need_comma = 0;
}

void json_arr_str(json_writer_t *w, const char *val)
{
    comma(w);
    write_escaped_str(w, val ? val : "");
}

void json_arr_int(json_writer_t *w, long long val)
{
    comma(w);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%lld", val);
    puts_s(w, tmp);
}

void json_arr_float(json_writer_t *w, double val)
{
    comma(w);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.1f", val);
    puts_s(w, tmp);
}

void json_bare_str(json_writer_t *w, const char *val)
{
    write_escaped_str(w, val ? val : "");
}

size_t json_len(const json_writer_t *w)
{
    w->buf[w->pos] = '\0';
    return w->pos;
}
