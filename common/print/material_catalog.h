/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_MATERIAL_CATALOG_H
#define DENEB_MATERIAL_CATALOG_H

#include <stddef.h>

#define DENEB_MATERIAL_CATALOG_DIR "/home/3D/deneb-materials"
#define DENEB_MATERIAL_IMPORT_USB_ROOT "/mnt/sda1"
#define DENEB_MATERIAL_IMPORT_MAX_DEPTH 4

int deneb_material_catalog_file_is_candidate(const char *name);
int deneb_material_catalog_copy_tag_value(const char *xml, const char *tag,
                                          char *out, size_t out_sz);
int deneb_material_catalog_guid_is_safe(const char *guid);
int deneb_material_catalog_parse_file(const char *path,
                                      char *guid, size_t guid_sz,
                                      int *version);
int deneb_material_catalog_store_file(const char *path,
                                      const char *catalog_dir,
                                      char *guid, size_t guid_sz,
                                      int *version);
int deneb_material_catalog_import_tree(const char *root,
                                       const char *catalog_dir,
                                       int max_depth,
                                       int *imported);
int deneb_material_catalog_build_response(const char *stock_json,
                                          const char *catalog_dir,
                                          char **out,
                                          size_t *out_len);

#endif
