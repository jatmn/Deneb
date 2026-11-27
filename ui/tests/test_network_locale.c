/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "../src/screens/screen_network.c"
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

static pthread_t ui_thread;
static pthread_mutex_t worker_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
static bool worker_waiting;
static bool worker_released;
static net_usb_config_result_t worker_usb_result = NET_USB_CONFIG_FOUND;
static lv_timer_cb_t active_callback;
static int timer_token;
static int label_token;
static int switch_token;
static char shown[128];

static void hold_worker(void)
{
    assert(!pthread_equal(pthread_self(), ui_thread));
    pthread_mutex_lock(&worker_lock);
    worker_waiting = true;
    pthread_cond_signal(&worker_cond);
    while (!worker_released)
        pthread_cond_wait(&worker_cond, &worker_lock);
    pthread_mutex_unlock(&worker_lock);
}

net_usb_config_result_t __wrap_net_find_usb_config(const char *filename,
                                                  char *path, int size)
{
    if (!pthread_equal(pthread_self(), ui_thread)) {
        hold_worker();
        if (worker_usb_result != NET_USB_CONFIG_FOUND)
            return worker_usb_result;
    }
    snprintf(path, size, "%s", filename);
    return NET_USB_CONFIG_FOUND;
}

int __wrap_system(const char *command)
{
    assert(command);
    hold_worker();
    return 0;
}

void __wrap_net_read_command(char *buf, size_t size, const char *command)
{
    snprintf(buf, size, "%s", strstr(command, "wireless.sta.ssid") ? "Fixture Wi-Fi" : "");
}

const char *__real_locale_get(const char *key);
const char *__wrap_locale_get(const char *key)
{
    assert(pthread_equal(pthread_self(), ui_thread));
    return __real_locale_get(key);
}

int __real_locale_format_s(char *out, size_t size, const char *key, const char *arg);
int __wrap_locale_format_s(char *out, size_t size, const char *key, const char *arg)
{
    assert(pthread_equal(pthread_self(), ui_thread));
    return __real_locale_format_s(out, size, key, arg);
}

int __real_locale_format_ss(char *out, size_t size, const char *key,
                            const char *first, const char *second);
int __wrap_locale_format_ss(char *out, size_t size, const char *key,
                            const char *first, const char *second)
{
    assert(pthread_equal(pthread_self(), ui_thread));
    return __real_locale_format_ss(out, size, key, first, second);
}

lv_timer_t *lv_timer_create(lv_timer_cb_t callback, uint32_t period, void *data)
{
    assert(!active_callback && period == 200 && data == NULL);
    active_callback = callback;
    return (lv_timer_t *)&timer_token;
}

void lv_timer_delete(lv_timer_t *timer)
{
    assert(timer == (lv_timer_t *)&timer_token && active_callback);
    active_callback = NULL;
}

void lv_obj_add_flag(lv_obj_t *obj, lv_obj_flag_t flag) { (void)obj; (void)flag; }
void lv_obj_remove_flag(lv_obj_t *obj, lv_obj_flag_t flag) { (void)obj; (void)flag; }
void lv_obj_add_state(lv_obj_t *obj, lv_state_t state) { (void)obj; (void)state; }
void lv_obj_remove_state(lv_obj_t *obj, lv_state_t state) { (void)obj; (void)state; }
bool lv_obj_has_state(const lv_obj_t *obj, lv_state_t state)
{
    assert(obj == wifi_switch && state == LV_STATE_CHECKED);
    return false;
}

void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    assert(obj == status_label);
    snprintf(shown, sizeof(shown), "%s", text);
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static void expect_uci_call(const char *call)
{
    FILE *file = fopen("uci-calls", "r");
    assert(file);
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        if (strcmp(line, call) == 0)
            found = true;
    }
    assert(fclose(file) == 0 && found);
}

static void start(void (*event)(lv_event_t *))
{
    pthread_mutex_lock(&worker_lock);
    worker_waiting = false;
    worker_released = false;
    pthread_mutex_unlock(&worker_lock);

    event(NULL);
    assert(active_callback && network_busy());
    pthread_mutex_lock(&worker_lock);
    while (!worker_waiting)
        pthread_cond_wait(&worker_cond, &worker_lock);
    pthread_mutex_unlock(&worker_lock);
    active_callback((lv_timer_t *)&timer_token);
    assert(active_callback && network_busy());
}

static void finish(const char *expected)
{
    pthread_mutex_lock(&worker_lock);
    worker_released = true;
    pthread_cond_signal(&worker_cond);
    pthread_mutex_unlock(&worker_lock);

    for (int i = 0; i < 2000 && atomic_load(&job_running); i++)
        usleep(1000);
    assert(!atomic_load(&job_running) && network_busy());
    active_callback((lv_timer_t *)&timer_token);
    assert(!active_callback && !network_busy());
    assert(strcmp(shown, expected) == 0);
}

int main(void)
{
    ui_thread = pthread_self();
    char directory[] = "./deneb-network-locale-XXXXXX";
    int original_directory = open(".", O_RDONLY);
    assert(original_directory >= 0 && mkdtemp(directory) && chdir(directory) == 0);
    write_file("uci", "#!/bin/sh\nprintf '%s|%s|%s\\n' \"$#\" \"$1\" \"$2\" >> uci-calls\n");
    assert(chmod("uci", 0700) == 0 && setenv("PATH", ".", 1) == 0);
    write_file("wifi.txt", "ssid=Fixture Network\npassword=Bob's $guest key\ncountry=ca\n");
    write_file("eth.txt", "ip=192.0.2.80\ndns=192.0.2.53, 192.0.2.54\n");
    write_file("fixture.json",
        "{\"network.wifi_configured_fmt\":\"New Wi-Fi %s\","
        "\"network.eth_static_fmt\":\"New Ethernet %s\","
        "\"network.wifi_disabled\":\"New Wi-Fi off\","
        "\"network.no_wifi_txt\":\"New missing Wi-Fi file\"}");
    status_label = (lv_obj_t *)&label_token;

    assert(locale_init("en") == 0);
    start(import_wifi_cb);
    assert(locale_set("fixture") == 0);
    finish("New Wi-Fi Fixture Network");

    start(import_eth_cb);
    finish("New Ethernet 192.0.2.80");
    expect_uci_call("2|set|wireless.radio0.country=CA\n");
    expect_uci_call("2|set|wireless.sta.ssid=Fixture Network\n");
    expect_uci_call("2|set|wireless.sta.key=Bob's $guest key\n");
    expect_uci_call("2|set|network.wan.ipaddr=192.0.2.80\n");
    expect_uci_call("2|add_list|network.wan.dns=192.0.2.53\n");
    expect_uci_call("2|add_list|network.wan.dns=192.0.2.54\n");

    worker_usb_result = NET_USB_CONFIG_NO_FILE;
    start(import_wifi_cb);
    finish("New missing Wi-Fi file");

    assert(locale_set("en") == 0);
    wifi_switch = (lv_obj_t *)&switch_token;
    start(wifi_switch_cb);
    network_destroy();
    locale_deinit();
    assert(locale_init("fixture") == 0);
    status_label = (lv_obj_t *)&label_token;
    finish("New Wi-Fi off");
    locale_deinit();

    assert(unlink("fixture.json") == 0 && unlink("eth.txt") == 0 && unlink("wifi.txt") == 0);
    assert(unlink("uci") == 0 && unlink("uci-calls") == 0);
    assert(fchdir(original_directory) == 0 && close(original_directory) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
