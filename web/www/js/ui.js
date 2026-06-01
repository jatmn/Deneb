/**
 * SPDX-License-Identifier: MPL-2.0
 * Deneb Web UI - DOM helpers and page rendering
 */

var Deneb = Deneb || {};

Deneb.ui = {
    lastStatus: null,

    showPage: function(name) {
        var content = document.getElementById('content');
        var navBtns = document.querySelectorAll('.nav-btn');

        /* Update nav */
        for (var i = 0; i < navBtns.length; i++) {
            navBtns[i].classList.toggle('active', navBtns[i].getAttribute('data-page') === name);
        }

        /* Render page */
        if (this.pages[name]) {
            content.innerHTML = this.pages[name]();
            Deneb.i18n.apply();
            if (this.lastStatus) this.updateStatus(this.lastStatus);
        }
    },

    updateStatus: function(data) {
        this.lastStatus = data || {};
        var dot = document.getElementById('status-dot');
        var text = document.getElementById('status-text');
        if (!dot || !text) return;

        var status = this.statusFromData(data);
        dot.className = 'status-dot ' + status;
        text.textContent = Deneb.i18n.t('web.status.' + status) || status;

        /* Update status page if visible */
        this.updateStatusPage(data);
    },

    updateStatusPage: function(data) {
        var el;
        el = document.getElementById('nozzle-temp');
        if (el) el.textContent = this.formatNumber(this.nozzleTemperature(data, 'current'), 1);
        el = document.getElementById('nozzle-target');
        if (el) {
            var nt = this.nozzleTemperature(data, 'target');
            el.textContent = nt > 0 ? '/ ' + nt.toFixed(0) + '\u00b0C' : '';
        }
        el = document.getElementById('bed-temp');
        if (el) el.textContent = this.formatNumber(this.bedTemperature(data, 'current'), 1);
        el = document.getElementById('bed-target');
        if (el) {
            var bt = this.bedTemperature(data, 'target');
            el.textContent = bt > 0 ? '/ ' + bt.toFixed(0) + '\u00b0C' : '';
        }
        el = document.getElementById('pos-x');
        if (el) el.textContent = this.formatNumber(this.position(data, 'x'), 1);
        el = document.getElementById('pos-y');
        if (el) el.textContent = this.formatNumber(this.position(data, 'y'), 1);
        el = document.getElementById('pos-z');
        if (el) el.textContent = this.formatNumber(this.position(data, 'z'), 1);

        /* Progress */
        var progress = this.printProgress(data);
        el = document.getElementById('progress-fill');
        if (el) el.style.width = progress + '%';
        el = document.getElementById('progress-text');
        if (el) el.textContent = progress.toFixed(0) + '%';
        el = document.getElementById('filename');
        if (el) el.textContent = data.name || data.filename || Deneb.i18n.t('web.status.no_print');
        el = document.getElementById('time-left');
        if (el) {
            var tl = data.time_total || data.time_left || 0;
            el.textContent = tl > 0 ? this.formatTime(tl) : '--:--';
        }
    },

    statusFromData: function(data) {
        if (!data) return 'idle';
        if (data.status) return data.status;
        if (data.has_error) return 'error';
        if (data.is_paused) return 'paused';
        if (data.is_printing) return 'printing';
        if (data.connected === false) return 'offline';
        return 'idle';
    },

    asNumber: function(value, fallback) {
        var n = Number(value);
        return isFinite(n) ? n : fallback;
    },

    formatNumber: function(value, digits) {
        return this.asNumber(value, 0).toFixed(digits);
    },

    nozzleTemperature: function(data, key) {
        var extruder = data && data.heads && data.heads[0] &&
            data.heads[0].extruders && data.heads[0].extruders[0];
        if (extruder && extruder.hotend && extruder.hotend.temperature) {
            return this.asNumber(extruder.hotend.temperature[key], 0);
        }
        return this.asNumber(data && (key === 'current' ? data.nozzle_temp_cur : data.nozzle_temp_set), 0);
    },

    bedTemperature: function(data, key) {
        if (data && data.bed && data.bed.temperature) {
            return this.asNumber(data.bed.temperature[key], 0);
        }
        return this.asNumber(data && (key === 'current' ? data.bed_temp_cur : data.bed_temp_set), 0);
    },

    position: function(data, axis) {
        if (data && data.heads && data.heads[0] && data.heads[0].position) {
            return this.asNumber(data.heads[0].position[axis], 0);
        }
        return this.asNumber(data && data['pos_' + axis], 0);
    },

    printProgress: function(data) {
        var progress = this.asNumber(data && data.progress, 0);
        if (progress > 0 && progress <= 1) progress *= 100;
        if (progress < 0) return 0;
        if (progress > 100) return 100;
        return progress;
    },

    formatTime: function(seconds) {
        var h = Math.floor(seconds / 3600);
        var m = Math.floor((seconds % 3600) / 60);
        if (h > 0) return h + 'h ' + m + 'm';
        return m + 'm';
    },

    showError: function(msg) {
        /* Simple toast */
        var toast = document.createElement('div');
        toast.style.cssText = 'position:fixed;top:50px;left:50%;transform:translateX(-50%);background:var(--error);color:white;padding:10px 20px;border-radius:6px;z-index:100;font-size:13px;';
        toast.textContent = msg;
        document.body.appendChild(toast);
        setTimeout(function() { toast.remove(); }, 3000);
    },

    /* Page templates */
    pages: {
        status: function() {
            return '<div class="card">' +
                '<div class="card-title" data-i18n="web.status.temperatures">Temperatures</div>' +
                '<div class="status-grid">' +
                '<div class="status-item"><div class="value" id="nozzle-temp">--</div><div class="label"><span data-i18n="web.status.nozzle">Nozzle</span> <span id="nozzle-target" class="temp-target"></span></div></div>' +
                '<div class="status-item"><div class="value" id="bed-temp">--</div><div class="label"><span data-i18n="web.status.bed">Bed</span> <span id="bed-target" class="temp-target"></span></div></div>' +
                '</div></div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.status.progress">Print Progress</div>' +
                '<div class="progress-text" id="progress-text">0%</div>' +
                '<div class="progress-bar"><div class="progress-fill" id="progress-fill"></div></div>' +
                '<div class="progress-info"><span id="filename">--</span> &middot; <span id="time-left">--:--</span></div>' +
                '</div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.status.position">Position</div>' +
                '<div class="status-grid">' +
                '<div class="status-item"><div class="value" id="pos-x">--</div><div class="label">X</div></div>' +
                '<div class="status-item"><div class="value" id="pos-y">--</div><div class="label">Y</div></div>' +
                '<div class="status-item"><div class="value" id="pos-z">--</div><div class="label">Z</div></div>' +
                '</div></div>';
        },

        control: function() {
            return '<div class="card">' +
                '<div class="card-title" data-i18n="web.control.print">Print Control</div>' +
                '<div class="control-group">' +
                '<button class="btn btn-secondary" onclick="Deneb.cmd(\'pause\')" data-i18n="web.control.pause">Pause</button>' +
                '<button class="btn btn-secondary" onclick="Deneb.cmd(\'resume\')" data-i18n="web.control.resume">Resume</button>' +
                '<button class="btn btn-warn" onclick="Deneb.cmdAbort()" data-i18n="web.control.cancel">Cancel</button>' +
                '</div></div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.control.temperature">Temperature</div>' +
                '<label data-i18n="web.control.set_nozzle">Nozzle (\u00b0C)</label>' +
                '<input type="range" class="input" id="nozzle-slider" min="0" max="260" value="0" oninput="document.getElementById(\'nozzle-val\').textContent=this.value">' +
                '<span id="nozzle-val">0</span>\u00b0C ' +
                '<button class="btn btn-primary" onclick="Deneb.setNozzle()" data-i18n="web.control.set">Set</button>' +
                '<label data-i18n="web.control.set_bed">Bed (\u00b0C)</label>' +
                '<input type="range" class="input" id="bed-slider" min="0" max="110" value="0" oninput="document.getElementById(\'bed-val\').textContent=this.value">' +
                '<span id="bed-val">0</span>\u00b0C ' +
                '<button class="btn btn-primary" onclick="Deneb.setBed()" data-i18n="web.control.set">Set</button>' +
                '<button class="btn btn-secondary btn-block" onclick="Deneb.cmdCooldown()" data-i18n="web.control.cooldown">Cooldown</button>' +
                '</div>';
        },

        settings: function() {
            return '<div class="card">' +
                '<div class="card-title" data-i18n="web.settings.auth">Authentication</div>' +
                '<div class="toggle">' +
                '<span data-i18n="web.settings.require_auth">Require password</span>' +
                '<div class="toggle-switch" id="auth-toggle" onclick="Deneb.toggleAuth()"></div>' +
                '</div>' +
                '<label data-i18n="web.settings.new_password">New Password</label>' +
                '<input type="password" class="input" id="new-password" placeholder="">' +
                '<button class="btn btn-primary btn-block" onclick="Deneb.changePassword()" data-i18n="web.settings.change_password">Change Password</button>' +
                '</div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.settings.language">Language</div>' +
                '<select class="input" id="lang-select" onchange="Deneb.i18n.load(this.value)">' +
                '<option value="en">English</option>' +
                '<option value="nl">Nederlands</option>' +
                '<option value="de">Deutsch</option>' +
                '<option value="fr">Fran\u00e7ais</option>' +
                '<option value="zh-Hans">\u4e2d\u6587</option>' +
                '</select>' +
                '</div>';
        },

        about: function() {
            return '<div class="card">' +
                '<div class="card-title" data-i18n="web.about.title">About Deneb</div>' +
                '<dl class="about-info">' +
                '<dt>Version</dt><dd id="about-version">--</dd>' +
                '<dt>Printer</dt><dd>Ultimaker 2+ Connect</dd>' +
                '<dt>API</dt><dd>UltiMaker REST API v1</dd>' +
                '<dt>License</dt><dd>MPL-2.0</dd>' +
                '</dl></div>';
        },

        setup: function() {
            return '<div class="setup-page">' +
                '<h1>Deneb</h1>' +
                '<p data-i18n="web.setup.subtitle">Set up access to your printer\'s web interface</p>' +
                '<label data-i18n="web.setup.password">Choose a password</label>' +
                '<input type="password" class="input" id="setup-password" placeholder="">' +
                '<label data-i18n="web.setup.confirm">Confirm password</label>' +
                '<input type="password" class="input" id="setup-confirm" placeholder="">' +
                '<button class="btn btn-primary btn-block" onclick="Deneb.setup(true)" data-i18n="web.setup.save">Save &amp; Continue</button>' +
                '<button class="btn btn-secondary btn-block" onclick="Deneb.setup(false)" data-i18n="web.setup.skip">Skip (Open Access)</button>' +
                '</div>';
        }
    }
};
