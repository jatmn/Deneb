#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

DENEB_HASH='$1$deneb$wCyvDhYQ1xixNsW2sCDBW0'

log() {
    logger -t deneb-ssh-bootstrap "$*"
    echo "deneb-ssh-bootstrap: $*"
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

    uci set dropbear.@dropbear[0].PasswordAuth='on'
    uci set dropbear.@dropbear[0].RootPasswordAuth='on'
    uci set dropbear.@dropbear[0].RootLogin='on'
    uci set dropbear.@dropbear[0].Port='22'
    uci commit dropbear
}

log "starting"

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

/etc/init.d/dropbear enable

if /etc/init.d/dropbear restart; then
    log "dropbear restarted"
else
    log "dropbear restart failed; trying start"
    /etc/init.d/dropbear start
fi

log "finished; ssh should be available on port 22 with root password deneb"
exit 0

