/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <assert.h>

/* Uncompressed ZIP containing just 3D/model.gcode with the text ";TIME:1\n". */
static const unsigned char ufp[] = {
    0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x21, 0x5c, 0x5f, 0xbc, 0x95, 0x41, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x33, 0x44, 0x2f, 0x6d, 0x6f, 0x64,
    0x65, 0x6c, 0x2e, 0x67, 0x63, 0x6f, 0x64, 0x65, 0x3b, 0x54, 0x49, 0x4d,
    0x45, 0x3a, 0x31, 0x0a, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x0a, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x5c, 0x5f, 0xbc, 0x95, 0x41,
    0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb4, 0x81, 0x00, 0x00,
    0x00, 0x00, 0x33, 0x44, 0x2f, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x2e, 0x67,
    0x63, 0x6f, 0x64, 0x65, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0x00, 0x00
};

int main(void)
{
    static const char guid[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
    static const char expected[] =
        "; material_guid=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n"
        "; print_core_id=AA 0.4\n;TIME:1\n";
    char actual[sizeof(expected)];
    struct stat st;
    int archive = open_job_file();
    int model = open_job_file();
    assert(archive >= 0 && model >= 0);
    assert(fstat(model, &st) == 0 && st.st_nlink == 0);
    assert((fcntl(archive, F_GETFD) & FD_CLOEXEC) != 0);
    assert(write(archive, ufp, sizeof(ufp)) == sizeof(ufp));
    off_t prefix_size = write_file_metadata(model, guid, "AA 0.4");
    assert(prefix_size > 0);
    assert(extract_ufp_model_gcode(archive, model, prefix_size) == 0);
    assert((fcntl(archive, F_GETFD) & FD_CLOEXEC) != 0);
    assert(pread(model, actual, sizeof(actual), 0) == sizeof(expected) - 1);
    assert(memcmp(actual, expected, sizeof(expected) - 1) == 0);

    assert(ftruncate(model, prefix_size) == 0);
    assert(lseek(model, prefix_size, SEEK_SET) == prefix_size);
    /* A zip reader can emit bytes before discovering the bad CRC. */
    assert(pwrite(archive, "!", 1, 44) == 1);
    assert(extract_ufp_model_gcode(archive, model, prefix_size) != 0);
    assert(fstat(model, &st) == 0 && st.st_size == prefix_size);

    assert(ftruncate(archive, 0) == 0);
    assert(extract_ufp_model_gcode(archive, model, prefix_size) != 0);

    pid_t writer = start_job_writer(model);
    assert(writer >= 0);
    if (writer == 0) {
        for (;;)
            pause();
    }
    g_stop = 1;
    assert(wait_for_job_writer(writer, model, prefix_size) != 0);
    assert(waitpid(writer, NULL, WNOHANG) < 0 && errno == ECHILD);
    assert(local_api_upload_file(model, "job.gcode", guid, "cloud", actual,
                                 sizeof(actual)) == -1);
    assert(fcntl(model, F_GETFD) < 0 && errno == EBADF);
    assert(close(archive) == 0);
    return 0;
}
