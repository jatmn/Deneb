/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Minimal JSON writer. Builds JSON strings without a library dependency.
 * All functions write into a caller-provided buffer.
 */

#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include <stddef.h>

/* Writer context */
typedef struct {
    char *buf;
    size_t pos;
    size_t cap;
    int depth;       /* nesting depth for comma tracking */
    int need_comma;  /* flag: need comma before next key */
} json_writer_t;

/* Initialize writer with a buffer. */
void json_init(json_writer_t *w, char *buf, size_t cap);

/* Object/array open/close. */
void json_obj_open(json_writer_t *w);
void json_obj_close(json_writer_t *w);
void json_arr_open(json_writer_t *w);
void json_arr_close(json_writer_t *w);

/* Write key-value pairs. Strings are escaped automatically. */
void json_str(json_writer_t *w, const char *key, const char *val);
void json_int(json_writer_t *w, const char *key, long long val);
void json_float(json_writer_t *w, const char *key, double val);
void json_bool(json_writer_t *w, const char *key, int val);
void json_null(json_writer_t *w, const char *key);
void json_raw(json_writer_t *w, const char *key, const char *raw_json);

/* Write key only (for nested objects/arrays: "key":{...} or "key":[...]) */
void json_key(json_writer_t *w, const char *key);

/* Array elements (no key). */
void json_arr_str(json_writer_t *w, const char *val);
void json_arr_int(json_writer_t *w, long long val);
void json_arr_float(json_writer_t *w, double val);

/* Write a bare escaped JSON string (no key, for use outside objects/arrays). */
void json_bare_str(json_writer_t *w, const char *val);

/* Get the current string length. */
size_t json_len(const json_writer_t *w);

#endif /* JSON_WRITER_H */
