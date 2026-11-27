/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <assert.h>

/* Deflated ZIP containing 3D/model.gcode with eight lines of ";TIME:1". */
static const unsigned char ufp[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x21, 0x5c, 0x66, 0x88, 0xc1, 0x95, 0x0d, 0x00, 0x00, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x33, 0x44, 0x2f, 0x6d, 0x6f, 0x64,
    0x65, 0x6c, 0x2e, 0x67, 0x63, 0x6f, 0x64, 0x65, 0xb3, 0x0e, 0xf1, 0xf4,
    0x75, 0xb5, 0x32, 0xe4, 0xb2, 0x26, 0x93, 0x06, 0x00, 0x50, 0x4b, 0x01,
    0x02, 0x1e, 0x03, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21,
    0x5c, 0x66, 0x88, 0xc1, 0x95, 0x0d, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0xb4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x33, 0x44, 0x2f, 0x6d, 0x6f,
    0x64, 0x65, 0x6c, 0x2e, 0x67, 0x63, 0x6f, 0x64, 0x65, 0x50, 0x4b, 0x05,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00,
    0x00, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00
};

int main(void)
{
    static const char guid[] = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
    static const char expected[] =
        "; material_guid=aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n"
        "; print_core_id=AA 0.4\n"
        ";TIME:1\n;TIME:1\n;TIME:1\n;TIME:1\n"
        ";TIME:1\n;TIME:1\n;TIME:1\n;TIME:1\n";
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
    assert(download_file_with_wget("--test-invalid-url", model, prefix_size) == -1);
    assert(fstat(model, &st) == 0 && st.st_size == prefix_size);
    assert(extract_ufp_model_gcode(archive, model, prefix_size) == 0);
    assert((fcntl(archive, F_GETFD) & FD_CLOEXEC) != 0);
    assert(pread(model, actual, sizeof(actual), 0) == sizeof(expected) - 1);
    assert(memcmp(actual, expected, sizeof(expected) - 1) == 0);

    assert(ftruncate(model, prefix_size) == 0);
    assert(lseek(model, prefix_size, SEEK_SET) == prefix_size);
    /* Both unzip variants emit the deflated member before checking its CRC. */
    const unsigned char bad_crc_byte = 0x67;
    assert(pwrite(archive, &bad_crc_byte, 1, 14) == 1); /* Local header CRC. */
    assert(pwrite(archive, &bad_crc_byte, 1, 73) == 1); /* Central entry CRC. */
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
