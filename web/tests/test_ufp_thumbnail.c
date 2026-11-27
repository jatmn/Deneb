/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "../../common/print/print_job_file.c"
#include <assert.h>

int main(void)
{
    enum { preview_limit = 2 * 1024 * 1024, compressed_limit = preview_limit + 64 * 1024 };

    assert(ufp_thumbnail_sizes_valid(8, compressed_limit, preview_limit));
    assert(!ufp_thumbnail_sizes_valid(8, compressed_limit + 1, preview_limit));
    assert(!ufp_thumbnail_sizes_valid(8, compressed_limit, preview_limit + 1));
    assert(!ufp_thumbnail_sizes_valid(8, 0, 1));
    assert(!ufp_thumbnail_sizes_valid(8, 1, 0));
    assert(ufp_thumbnail_sizes_valid(0, preview_limit, preview_limit));
    assert(!ufp_thumbnail_sizes_valid(0, preview_limit, preview_limit - 1));
    return 0;
}
