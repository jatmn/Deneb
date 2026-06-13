/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_API_MULTIPART_H
#define DENEB_API_MULTIPART_H

int extract_multipart_file(const char *boundary, const char *upload_path,
                           char *out_path, int out_sz,
                           char *filename, int fn_sz);
int extract_multipart_field(const char *boundary, const char *upload_path,
                            const char *field_name, char *out, int out_sz);

#endif
