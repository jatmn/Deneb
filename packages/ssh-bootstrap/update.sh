#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

DENEB_HASH='$1$deneb$wCyvDhYQ1xixNsW2sCDBW0'
DENEB_HOME="/home/deneb"
DENEB_BACKUP_DIR="${DENEB_HOME}/backups/get-started"
DENEB_REBOOT_SCHEDULED=0

log() {
    logger -t deneb-get-started "$*"
    echo "deneb-get-started: $*"
}

reboot_now() {
    sync
    ubus call system reboot >/dev/null 2>&1 && exit 0
    /sbin/reboot >/dev/null 2>&1 && exit 0
    /bin/busybox reboot -f >/dev/null 2>&1 && exit 0
    echo b > /proc/sysrq-trigger
}

schedule_reboot() {
    if [ "${DENEB_REBOOT_SCHEDULED}" = "1" ]; then
        return
    fi

    DENEB_REBOOT_SCHEDULED=1
    log "scheduling reboot watchdog"
    (
        sleep 8
        reboot_now
    ) >/dev/null 2>&1 &
}

set_shadow_hash() {
    user="$1"
    hash="$2"

    if grep -q "^${user}:" /etc/shadow; then
        sed -i "s|^${user}:[^:]*:|${user}:${hash}:|" /etc/shadow
    else
        echo "${user}:${hash}:0:0:99999:7:::" >> /etc/shadow
    fi
}

set_passwd_placeholder() {
    user="$1"

    if grep -q "^${user}:" /etc/passwd; then
        sed -i "s|^${user}:[^:]*:|${user}:x:|" /etc/passwd
    fi
}

ensure_login_shell() {
    user="$1"

    if grep -q "^${user}:" /etc/passwd; then
        sed -i "s|^\(${user}:[^:]*:[^:]*:[^:]*:[^:]*:[^:]*:\)[^:]*$|\1/bin/ash|" /etc/passwd
    fi
}

ensure_dropbear_config() {
    if ! uci show dropbear.@dropbear[0] >/dev/null 2>&1; then
        uci add dropbear dropbear >/dev/null
    fi

    uci set dropbear.@dropbear[0].enable='1'
    uci set dropbear.@dropbear[0].PasswordAuth='on'
    uci set dropbear.@dropbear[0].RootPasswordAuth='on'
    uci set dropbear.@dropbear[0].RootLogin='on'
    uci set dropbear.@dropbear[0].Port='22'
    uci commit dropbear
}

ensure_dropbear_enabled_at_boot() {
    /etc/init.d/dropbear enable

    if /etc/init.d/dropbear enabled >/dev/null 2>&1; then
        log "dropbear init service is enabled at boot"
        return
    fi

    if ls /etc/rc.d/S*dropbear >/dev/null 2>&1; then
        log "dropbear startup symlink exists"
        return
    fi

    log "dropbear boot enable verification failed"
    exit 1
}

install_deneb_update_lane() {
    mkdir -p "${DENEB_BACKUP_DIR}"

    if [ ! -f /tmp/update/deneb-boot-320x240.png ]; then
        log "missing Deneb touchscreen splash asset; aborting"
        exit 1
    fi

    if [ ! -f /tmp/update/deneb-splash-128x102.jpg ]; then
        log "missing Deneb nodogsplash asset; aborting"
        exit 1
    fi

    if [ ! -f "${DENEB_BACKUP_DIR}/nodogsplash-splash.jpg.orig" ] && [ -f /etc/nodogsplash/htdocs/images/splash.jpg ]; then
        cp /etc/nodogsplash/htdocs/images/splash.jpg "${DENEB_BACKUP_DIR}/nodogsplash-splash.jpg.orig"
    fi

    cp /tmp/update/deneb-boot-320x240.png /home/cygnus/menu/img/deneb_boot.png
    cp /tmp/update/deneb-splash-128x102.jpg /etc/nodogsplash/htdocs/images/splash.jpg
    chmod 0644 /home/cygnus/menu/img/deneb_boot.png /etc/nodogsplash/htdocs/images/splash.jpg

    python3 - <<'PY'
from pathlib import Path
import shutil

backup_dir = Path("/home/deneb/backups/get-started")
backup_dir.mkdir(parents=True, exist_ok=True)

files = {
    "navigator": Path("/home/cygnus/menu/navigator/settings/update_firmware_navigator.py"),
    "browse": Path("/home/cygnus/menu/screens/update_firmware_browse_page.py"),
    "version_page": Path("/home/cygnus/menu/screens/update_firmware_version_page.py"),
    "main_menu": Path("/home/cygnus/menu/screens/main_menu_page.py"),
    "root_navigator": Path("/home/cygnus/menu/navigator/root_navigator.py"),
    "welcome": Path("/home/cygnus/menu/screens/show_welcome_hello.py"),
    "welcome_link": Path("/home/cygnus/menu/screens/show_welcome_link.py"),
    "images": Path("/home/cygnus/menu/img/images.py"),
    "handler": Path("/home/cygnus/coordinator/handlers/firmwareupdatehandling.py"),
}

def backup_once(path):
    backup = backup_dir / (path.name + ".orig")
    if not backup.exists():
        shutil.copy2(str(path), str(backup))

def write_if_changed(path, content):
    if path.read_text() != content:
        backup_once(path)
        path.write_text(content)

nav = files["navigator"]
text = nav.read_text()
old = "    found_imgs = list(USB_PATH.glob('*.img'))\n\n    if len(list(found_imgs)) == 1:\n        return found_imgs[0]\n"
new = """    update_files = []\n    for pattern in ('*.img', '*.IMG', '*.deneb', '*.DENEB'):\n        update_files.extend(USB_PATH.glob(pattern))\n\n    unique_files = sorted(set(update_files))\n    if len(unique_files) == 1:\n        return unique_files[0]\n"""
if old in text:
    text = text.replace(old, new)
if "*.deneb" not in text:
    raise RuntimeError("Could not patch Deneb auto-selection support in {}".format(nav))
write_if_changed(nav, text)

browse = files["browse"]
text = browse.read_text()
old = '                if item_path.suffix.upper() == ".IMG":\n'
new = '                if item_path.suffix.upper() in (".IMG", ".DENEB"):\n'
if old in text:
    text = text.replace(old, new)
text = text.replace("Explore the USB drive for IMG files", "Explore the USB drive for IMG and DENEB files")
text = text.replace("Scan the USB's current directory for IMG files", "Scan the USB's current directory for IMG and DENEB files")
if ".DENEB" not in text:
    raise RuntimeError("Could not patch Deneb browse support in {}".format(browse))
write_if_changed(browse, text)

handler = files["handler"]
text = handler.read_text()
old = '''        # Check signature
        logger.info("Starting to verify signature")
        try:
            # Use get_process because we want the return code
            verify_P = subprocess.run_subprocess(["/home/scripts/verify_image.sh",
                                                  str(menu_settings.FW_IMG_EXTRACT_DIR)],
                                                 get_process=True)

            # Wait until verify is done
            while verify_P.poll() is None:
                yield Duration(100)

            output, _ = verify_P.communicate()

            if verify_P.returncode != 0:
                raise Exception(f"Signature failure {output} {verify_P.returncode}")
        except Exception:
            logger.exception(f"Signature check failed!")
            """ Activate this section again when we make the signature check blocking
            self.drop(FaultBreadcrumb.id, FaultInstance.create(Fault.UPDATE_VERIFICATION_ERROR))

            shutil.rmtree(menu_settings.FW_IMG_EXTRACT_DIR)

            # Undo process killing
            subprocess.run_subprocess(["/etc/init.d/digitalfactory", "start"])
            subprocess.run_subprocess(["/etc/init.d/printserver", "start"])

            return
            """
'''
new = '''        # Check signature. Deneb packages use the same tar/update.sh transport,
        # but are intentionally project-local packages rather than UltiMaker firmware images.
        update_source = str(self._shared.meta['src'])
        update_source_name = os.path.basename(update_source).lower()
        is_deneb_package = update_source.lower().endswith(".deneb") or update_source_name == "deneb_get_started.img"
        if is_deneb_package:
            logger.info("Skipping UltiMaker signature verification for Deneb package")
        else:
            logger.info("Starting to verify signature")
            try:
                # Use get_process because we want the return code
                verify_P = subprocess.run_subprocess(["/home/scripts/verify_image.sh",
                                                      str(menu_settings.FW_IMG_EXTRACT_DIR)],
                                                     get_process=True)

                # Wait until verify is done
                while verify_P.poll() is None:
                    yield Duration(100)

                output, _ = verify_P.communicate()

                if verify_P.returncode != 0:
                    raise Exception(f"Signature failure {output} {verify_P.returncode}")
            except Exception:
                logger.exception(f"Signature check failed!")
                """ Activate this section again when we make the signature check blocking
                self.drop(FaultBreadcrumb.id, FaultInstance.create(Fault.UPDATE_VERIFICATION_ERROR))

                shutil.rmtree(menu_settings.FW_IMG_EXTRACT_DIR)

                # Undo process killing
                subprocess.run_subprocess(["/etc/init.d/digitalfactory", "start"])
                subprocess.run_subprocess(["/etc/init.d/printserver", "start"])

                return
                """
'''
if old in text:
    text = text.replace(old, new)
old = '''        # Check signature. Deneb packages use the same tar/update.sh transport,
        # but are intentionally project-local packages rather than UltiMaker firmware images.
        if str(self._shared.meta['src']).lower().endswith(".deneb"):
            logger.info("Skipping UltiMaker signature verification for Deneb package")
        else:
            logger.info("Starting to verify signature")
            try:
                # Use get_process because we want the return code
                verify_P = subprocess.run_subprocess(["/home/scripts/verify_image.sh",
                                                      str(menu_settings.FW_IMG_EXTRACT_DIR)],
                                                     get_process=True)

                # Wait until verify is done
                while verify_P.poll() is None:
                    yield Duration(100)

                output, _ = verify_P.communicate()

                if verify_P.returncode != 0:
                    raise Exception(f"Signature failure {output} {verify_P.returncode}")
            except Exception:
                logger.exception(f"Signature check failed!")
                """ Activate this section again when we make the signature check blocking
                self.drop(FaultBreadcrumb.id, FaultInstance.create(Fault.UPDATE_VERIFICATION_ERROR))

                shutil.rmtree(menu_settings.FW_IMG_EXTRACT_DIR)

                # Undo process killing
                subprocess.run_subprocess(["/etc/init.d/digitalfactory", "start"])
                subprocess.run_subprocess(["/etc/init.d/printserver", "start"])

                return
                """
'''
if old in text:
    text = text.replace(old, new)
if "deneb_get_started.img" not in text or "Skipping UltiMaker signature verification for Deneb package" not in text:
    raise RuntimeError("Could not patch Deneb signature bypass in {}".format(handler))
write_if_changed(handler, text)

version_page = files["version_page"]
text = version_page.read_text()
old = '''        install_from_usb_button = ("Install firmware via USB", self._on_update_from_usb, True)
        install_via_internet_button = ("Update firmware", self._on_update_from_internet, True)

        if latest_version != "-" and latest_version != current_version and is_online():
            buttons = [install_via_internet_button]
        else:
            buttons = [install_from_usb_button]
'''
new = '''        install_from_usb_button = ("Install firmware via USB", self._on_update_from_usb, True)

        # Deneb handles project updates through USB .deneb packages for now.
        buttons = [install_from_usb_button]
'''
if old in text:
    text = text.replace(old, new)
old = '''        if latest_version == "-" or not is_online():
            icon_src = ImageSource.usb
            message = "No internet connection, printer could not check for updates"
        elif current_version == latest_version:
            icon_src = ImageSource.ok_white
            message = "Your firmware is up to date"
        else:
            icon_src = ImageSource.update
            message = "There is an update available"
'''
new = '''        icon_src = ImageSource.usb
        message = "Install firmware from USB"
'''
if old in text:
    text = text.replace(old, new)
if "buttons = [install_from_usb_button]" not in text or "Install firmware from USB" not in text:
    raise RuntimeError("Could not patch Deneb USB-first update page in {}".format(version_page))
write_if_changed(version_page, text)

main_menu = files["main_menu"]
text = main_menu.read_text()
old = '''        if is_time_for_version_check() and is_online():
            # Update last check moment in UCI
            configuration.set_version("last_update_check", str(datetime.datetime.now().timestamp()))

            yield from self._async_update_uci_latest_firmware()

            # Compare current to latest version
            current_version = configuration.get_version("nr")
            latest_version = configuration.get_version("latest")
            is_version_mismatch = (current_version != latest_version and latest_version is not None)

            # Check whether the last popup that happened, was related to the latest available version
            popup_version_shown = configuration.get_version("update_popup_shown")
            is_latest_popup_shown = popup_version_shown == latest_version

            if is_version_mismatch and not is_latest_popup_shown:
                self.get_controller().goto(UpdateFirmwareNotificationPage)
'''
new = '''        logger.info("Deneb disables stock internet firmware update checks")
        return
'''
if old in text:
    text = text.replace(old, new)
if "Deneb disables stock internet firmware update checks" not in text:
    raise RuntimeError("Could not disable stock internet update checks in {}".format(main_menu))
write_if_changed(main_menu, text)

root_navigator = files["root_navigator"]
text = root_navigator.read_text()
old = '''        if configuration.get_version("welcome", "true") == "true":
            # If we installed the firmware for the first time show welcome screen once
            self.goto(ShowWelcomeHello)
        else:
            # Otherwise just to goto main menu directly
            self.goto(MenuNavigator)
'''
new = '''        # Deneb uses the first welcome page as an always-on boot splash.
        self.goto(ShowWelcomeHello)
'''
if old in text:
    text = text.replace(old, new)
if "Deneb uses the first welcome page as an always-on boot splash." not in text:
    raise RuntimeError("Could not patch Deneb boot splash navigation in {}".format(root_navigator))
write_if_changed(root_navigator, text)

images = files["images"]
text = images.read_text()
if "\tdeneb_boot = auto()\n" not in text:
    text = text.replace("class ImageSource(Enum):\n", "class ImageSource(Enum):\n\tdeneb_boot = auto()\n")
if "\tdeneb_boot = auto()\n" not in text:
    raise RuntimeError("Could not register Deneb splash image source in {}".format(images))
write_if_changed(images, text)

welcome = files["welcome"]
text = '''from cygnus.marshal.types.gui_status import GUIStatusState
from cygnus.menu import style
from cygnus.menu.img.images import ImageSource
from cygnus.menu.pylvgl import LV_ALIGN_IN_TOP_LEFT
from cygnus.menu.screen import Screen
from cygnus.menu.ui_elements.image import Image
from gershwin.duration import Duration


class ShowWelcomeHello(Screen):
    gui_status = GUIStatusState.MAINTENANCE

    def __init__(self):
        super().__init__()

        self.splash = Image(self.background,
                            path=ImageSource.deneb_boot,
                            size=(style.DISPLAY_WIDTH, style.DISPLAY_HEIGHT),
                            align=(self.background, LV_ALIGN_IN_TOP_LEFT, 0, 0))

    def on_activate(self) -> None:
        self.async_task(self._async_continue)

    def _async_continue(self):
        yield Duration(1000)
        self.get_controller().on_start_at_main_menu()
'''
write_if_changed(welcome, text)

welcome_link = files["welcome_link"]
text = welcome_link.read_text()
text = text.replace('''        welcome_message = "Register your printer at:\\n" \\
                          "ultimaker.com/register-your-printer\\n" \\
                          "and follow the free Ultimaker 2+ Connect product course to get started"
''', '''        welcome_message = "Deneb get-started is installed.\\n" \\
                          "SSH is enabled and future Deneb packages can be installed from USB."
''')
if "Deneb get-started is installed." not in text:
    raise RuntimeError("Could not patch Deneb welcome link in {}".format(welcome_link))
write_if_changed(welcome_link, text)
PY

    uci -q delete ultimaker.version.latest || true
    uci -q delete ultimaker.version.update_popup_shown || true
    uci -q delete ultimaker.version.last_update_check || true
    uci set ultimaker.version.new='false'
    uci commit ultimaker
}

log "starting"
trap schedule_reboot EXIT

if [ ! -x /etc/init.d/dropbear ]; then
    log "dropbear init script missing; aborting"
    exit 1
fi

if [ ! -x /usr/sbin/dropbear ]; then
    log "dropbear binary missing; aborting"
    exit 1
fi

set_shadow_hash root "${DENEB_HASH}"
set_passwd_placeholder root

if grep -q '^ultimaker:' /etc/passwd; then
    set_shadow_hash ultimaker "${DENEB_HASH}"
    set_passwd_placeholder ultimaker
    ensure_login_shell ultimaker
    log "configured existing ultimaker login user"
else
    log "no ultimaker login user found; configured root only"
fi

ensure_dropbear_config

ensure_dropbear_enabled_at_boot

install_deneb_update_lane

log "finished; ssh should be available after reboot on port 22 with root password deneb"
schedule_reboot
sleep 2
reboot_now
