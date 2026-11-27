/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#define DF_LOCK_DIR "lock"
#define DF_STATUS_FILE "status"
#include "../src/screens/screen_digital_factory.c"

#include <assert.h>
#include <sys/stat.h>

lv_font_t deneb_font_16;
static uint32_t tick;
static int manual_commands;

static uint32_t read_tick(void)
{
    return tick;
}

int __wrap_system(const char *command)
{
    assert(strstr(command, "/usr/bin/deneb-api digital-factory"));
    manual_commands++;
    return 0;
}

/* This executable doubles as the local uci process in the fixture directory. */
static int fake_uci(int argc, char **argv)
{
    assert(argc == 4 && strcmp(argv[0], "uci") == 0);
    assert(strcmp(argv[1], "-q") == 0 && strcmp(argv[2], "get") == 0);
    assert(strcmp(argv[3], "ultimaker.option.cluster_id") == 0);
    FILE *calls = fopen("uci-calls", "a");
    assert(calls && fputc('.', calls) != EOF && fclose(calls) == 0);
    return access("binding", F_OK) == 0 ? 0 : 1;
}

static void write_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(contents, file) >= 0 && fclose(file) == 0);
}

static void expect_status(uint32_t at, bool can_connect, bool can_disconnect,
                          const char *label, off_t lookup_calls)
{
    struct stat calls;
    tick = at;
    refresh_status();
    assert(lv_obj_has_state(connect_button, LV_STATE_DISABLED) == !can_connect);
    assert(lv_obj_has_state(disconnect_button, LV_STATE_DISABLED) == !can_disconnect);
    assert(strcmp(lv_label_get_text(status_label), label) == 0);
    assert(stat("uci-calls", &calls) == 0 && calls.st_size == lookup_calls);
}

int main(int argc, char **argv)
{
    if (argc != 1)
        return fake_uci(argc, argv);

    char directory[] = "./deneb-df-status-XXXXXX";
    int cwd = open(".", O_RDONLY | O_CLOEXEC);
    assert(cwd >= 0 && mkdtemp(directory) && chdir(directory) == 0);
    assert(symlink("/proc/self/exe", "uci") == 0 && setenv("PATH", ".", 1) == 0);

    lv_init();
    lv_tick_set_cb(read_tick);
    deneb_font_16 = lv_font_montserrat_16;
    lv_display_t *display = lv_display_create(320, 240);
    assert(display && locale_init("en") == 0 && df_create());
    expect_status(1000, true, false, "", 1);

    connect_cb(NULL);
    assert(manual_commands == 1);
    assert(strcmp(lv_label_get_text(status_label),
                  locale_get("digital_factory.requesting_pin")) == 0);
    write_file(DF_STATUS_FILE, "state=enter_pin pin=4321\r\nstate=connected\n");
    expect_status(2000, true, true, "PIN: 4321", 1);
    connect_cb(NULL);
    assert(manual_commands == 2);
    assert(strcmp(lv_label_get_text(status_label), "PIN: 4321") == 0);

    write_file("binding", "bound");
    write_file(DF_STATUS_FILE, "state=connected\n");
    expect_status(8000, false, true, locale_get("digital_factory.status_connected"), 1);
    write_file(DF_STATUS_FILE, "state=reconnecting\n");
    expect_status(9000, true, true, locale_get("digital_factory.status_reconnecting"), 1);
    write_file(DF_STATUS_FILE, "state=disconnected\n");
    const char *offline = locale_get("digital_factory.status_disconnected");
    expect_status(10000, true, true, offline, 2);

    /* Keep the offline status unchanged when the binding is removed. */
    assert(unlink("binding") == 0);
    expect_status(14000, true, true, offline, 2);
    expect_status(15000, true, false, offline, 3);
    assert(unlink(DF_STATUS_FILE) == 0);
    write_file("binding", "bound");
    expect_status(19000, true, false, offline, 3);
    expect_status(20000, true, true, offline, 4);

    disconnect_cb(NULL);
    assert(manual_commands == 2);
    disconnect_cb(NULL);
    assert(manual_commands == 3);
    assert(!lv_obj_has_state(connect_button, LV_STATE_DISABLED));
    assert(lv_obj_has_state(disconnect_button, LV_STATE_DISABLED));
    write_file(DF_STATUS_FILE, "state=disconnecting\n");
    expect_status(21000, true, false, locale_get("digital_factory.status_reconnecting"), 4);

    /* Redirecting the command output clears the marker before UCI is updated. */
    assert(mkdir(DF_LOCK_DIR, 0700) == 0);
    write_file(DF_STATUS_FILE, "");
    expect_status(22000, true, false, locale_get("digital_factory.status_reconnecting"), 4);

    assert(unlink("binding") == 0);
    write_file(DF_STATUS_FILE, "state=disconnected\n");
    assert(rmdir(DF_LOCK_DIR) == 0);
    expect_status(23000, true, false, offline, 5);

    df_destroy();
    lv_display_delete(display);
    locale_deinit();
    lv_deinit();
    assert(unlink(DF_STATUS_FILE) == 0 && unlink("uci") == 0 && unlink("uci-calls") == 0);
    assert(fchdir(cwd) == 0 && close(cwd) == 0 && rmdir(directory) == 0);
    return 0;
}
