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
            else this.updateMotionState();
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
        this.updateMotionState();
        this.updateJobsPage();
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

        el = document.getElementById('motion-pos-x');
        if (el) el.textContent = this.formatNumber(this.position(data, 'x'), 1);
        el = document.getElementById('motion-pos-y');
        if (el) el.textContent = this.formatNumber(this.position(data, 'y'), 1);
        el = document.getElementById('motion-pos-z');
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
            var tl = data.time_left || data.time_total || 0;
            el.textContent = tl > 0 ? this.formatTime(tl) : '--:--';
        }
        el = document.getElementById('stop-print-btn');
        if (el) {
            el.disabled = !(data && (data.is_printing || data.is_paused));
        }
    },

    updateJobsPage: function(data) {
        data = data || {};
        var status = this.lastStatus || {};
        var current = data.current;
        if (!current && (status.is_printing || status.is_paused)) {
            current = {
                name: status.name || status.filename,
                state: status.has_error ? 'error' : (status.is_paused ? 'paused' : 'printing'),
                progress: this.printProgress(status),
                time_total: status.time_total || 0,
                time_elapsed: status.time_total > 0 ? status.time_total - (status.time_left || 0) : 0,
                time_left: status.time_left || 0
            };
        }

        var container = document.getElementById('jobs-current');
        if (container) {
            if (current) {
                var elapsed = this.formatTime(current.time_elapsed || 0);
                var total = current.time_total > 0 ? this.formatTime(current.time_total) : '--:--';
                var left = (current.time_left || 0) > 0 ? this.formatTime(current.time_left) : '--:--';
                var progress = current.progress || 0;
                container.innerHTML = '<div class="jobs-item">' +
                    '<div class="jobs-item-main">' +
                    '<div class="jobs-item-name">' + this.escapeHtml(current.name || 'Unknown') + '</div>' +
                    '<div class="jobs-item-meta">' + elapsed + ' / ' + total + ' &middot; ' + (Deneb.i18n.t('web.jobs.left') || 'left') + ': ' + left + '</div>' +
                    '<div class="jobs-progress-bar"><div class="jobs-progress-fill" style="width:' + progress.toFixed(0) + '%"></div></div>' +
                    '</div>' +
                    '<div class="jobs-item-status ' + this.escapeHtml(current.state || '') + '">' + this.escapeHtml(current.state || '') + '</div>' +
                    '</div>';
            } else {
                container.innerHTML = '<div class="jobs-empty" data-i18n="web.jobs.no_current">No active print job</div>';
            }
        }

        var pending = document.getElementById('jobs-pending');
        if (pending) {
            var jobs = data.pending || [];
            if (jobs.length > 0) {
                var html = '';
                for (var i = 0; i < jobs.length; i++) {
                    var job = jobs[i];
                    html += '<div class="jobs-item">' +
                        '<div class="jobs-item-main">' +
                        '<div class="jobs-item-name">' + this.escapeHtml(job.name || 'Unknown') + '</div>' +
                        '<div class="jobs-item-meta">' + this.escapeHtml(job.source || '') + '</div>' +
                        '</div>' +
                        '<div class="jobs-item-status">pending</div>' +
                        '</div>';
                }
                pending.innerHTML = html;
            } else {
                pending.innerHTML = '<div class="jobs-empty" data-i18n="web.jobs.no_pending">No pending jobs</div>';
            }
        }

        var history = document.getElementById('jobs-history');
        if (history) {
            var hist = data.history || [];
            if (hist.length > 0) {
                var html = '';
                for (var i = hist.length - 1; i >= 0; i--) {
                    var job = hist[i];
                    var elapsed = this.formatTime(job.time_elapsed || 0);
                    var total = job.time_total > 0 ? this.formatTime(job.time_total) : '--:--';
                    var progress = (job.progress || 0).toFixed(0) + '%';
                    html += '<div class="jobs-item">' +
                        '<div class="jobs-item-main">' +
                        '<div class="jobs-item-name">' + this.escapeHtml(job.name || 'Unknown') + '</div>' +
                        '<div class="jobs-item-meta">' + elapsed + ' / ' + total + ' &middot; ' + progress + ' &middot; ' + this.escapeHtml(job.source || '') + '</div>' +
                        '</div>' +
                        '<div class="jobs-item-status ' + this.escapeHtml(job.state || '') + '">' + this.escapeHtml(job.state || '') + '</div>' +
                        '</div>';
                }
                history.innerHTML = html;
            } else {
                history.innerHTML = '<div class="jobs-empty" data-i18n="web.jobs.no_history">No print history yet</div>';
            }
        }
    },

    escapeHtml: function(text) {
        if (typeof text !== 'string') return '';
        return text
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    },

    updateMotionState: function() {
        var data = this.lastStatus || {};
        var allowed = this.motionAllowed(data);
        var buttons = document.querySelectorAll('[data-motion-btn]');
        for (var i = 0; i < buttons.length; i++) {
            buttons[i].disabled = !allowed;
        }

        var note = document.getElementById('motion-note');
        if (note) {
            note.textContent = allowed ?
                (Deneb.i18n.t('web.control.motion_ready') || 'Motion controls are ready.') :
                (Deneb.i18n.t('web.control.motion_unavailable') || 'Motion unavailable while disconnected, printing, paused, or in error.');
            note.classList.toggle('is-disabled', !allowed);
        }

        var step = document.getElementById('motion-step');
        if (step) step.textContent = Deneb.motion.currentStep() + ' mm';

        var stepInput = document.getElementById('motion-step-input');
        if (stepInput && document.activeElement !== stepInput) {
            stepInput.value = String(Deneb.motion.currentStep());
        }

        var presetButtons = document.querySelectorAll('[data-motion-step]');
        for (var j = 0; j < presetButtons.length; j++) {
            var preset = parseInt(presetButtons[j].getAttribute('data-motion-step'), 10);
            presetButtons[j].classList.toggle('is-active', preset === Deneb.motion.currentStep());
        }
    },


    motionAllowed: function(data) {
        if (!data) return false;

        var status = this.statusFromData(data);
        if (status === 'offline' || status === 'printing' || status === 'paused' || status === 'error') {
            return false;
        }

        if (typeof data.connected !== 'undefined' || typeof data.is_printing !== 'undefined' ||
            typeof data.is_paused !== 'undefined' || typeof data.has_error !== 'undefined' || data.status) {
            return true;
        }

        return false;
    },

    statusFromData: function(data) {
        if (!data) return 'idle';
        if (data.connected === false) return 'offline';
        if (data.status) return data.status;
        if (data.has_error) return 'error';
        if (data.is_paused) return 'paused';
        if (data.is_printing) return 'printing';
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
                '<div class="card-title">Print Control</div>' +
                '<div class="control-group"><button class="btn btn-warn" id="stop-print-btn" onclick="Deneb.cmdStop()">Stop</button></div>' +
                '</div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.status.position">Position</div>' +
                '<div class="status-grid">' +
                '<div class="status-item"><div class="value" id="pos-x">--</div><div class="label">X</div></div>' +
                '<div class="status-item"><div class="value" id="pos-y">--</div><div class="label">Y</div></div>' +
                '<div class="status-item"><div class="value" id="pos-z">--</div><div class="label">Z</div></div>' +
                '</div></div>';
        },

        jobs: function() {
            return '<div class="card jobs-current-card">' +
                '<div class="card-title" data-i18n="web.jobs.current">Current Print Job</div>' +
                '<div id="jobs-current">' +
                '<div class="jobs-empty" data-i18n="web.jobs.no_current">No active print job</div>' +
                '</div></div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.jobs.pending">Pending Jobs</div>' +
                '<div id="jobs-pending">' +
                '<div class="jobs-empty" data-i18n="web.jobs.no_pending">No pending jobs</div>' +
                '</div></div>' +
                '<div class="card">' +
                '<div class="card-title" data-i18n="web.jobs.history">Print History</div>' +
                '<div id="jobs-history">' +
                '<div class="jobs-empty" data-i18n="web.jobs.no_history">No print history yet</div>' +
                '</div></div>';
        },

        control: function() {
            return '<div class="card">' +
                '<div class="card-title" data-i18n="web.control.print">Print Control</div>' +
                '<div class="control-group">' +
                '<button class="btn btn-secondary" onclick="Deneb.cmd(\'pause\')" data-i18n="web.control.pause">Pause</button>' +
                '<button class="btn btn-secondary" onclick="Deneb.cmd(\'resume\')" data-i18n="web.control.resume">Resume</button>' +
                '<button class="btn btn-warn" onclick="Deneb.cmdAbort()" data-i18n="web.control.cancel">Cancel</button>' +
                '<button class="btn btn-warn" onclick="Deneb.cmdStop()">Stop</button>' +
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
                '</div>' +
                '<div class="card motion-card">' +
                '<div class="card-title" data-i18n="web.control.motion">Manual Control</div>' +
                '<div class="motion-layout">' +
                '<div class="motion-jog">' +
                '<button class="btn btn-secondary motion-btn motion-up" data-motion-btn onclick="Deneb.jog(\'Y\', 1)">&#9650;</button>' +
                '<button class="btn btn-secondary motion-btn motion-left" data-motion-btn onclick="Deneb.jog(\'X\', -1)">&#9664;</button>' +
                '<button class="btn btn-primary motion-btn motion-home" data-motion-btn onclick="Deneb.home()" data-i18n="web.control.home">Home</button>' +
                '<button class="btn btn-secondary motion-btn motion-right" data-motion-btn onclick="Deneb.jog(\'X\', 1)">&#9654;</button>' +
                '<button class="btn btn-secondary motion-btn motion-down" data-motion-btn onclick="Deneb.jog(\'Y\', -1)">&#9660;</button>' +
                '</div>' +
                '<div class="motion-side">' +
                '<div class="motion-axis-group">' +
                '<div class="motion-axis-label">Z</div>' +
                '<div class="control-group motion-vertical">' +
                '<button class="btn btn-secondary" data-motion-btn onclick="Deneb.jog(\'Z\', -1)">&#9650;</button>' +
                '<button class="btn btn-secondary" data-motion-btn onclick="Deneb.jog(\'Z\', 1)">&#9660;</button>' +
                '<button class="btn btn-secondary" data-motion-btn onclick="Deneb.zHome()" data-i18n="web.control.z_home">Z Home</button>' +
                '</div></div>' +
                '<div class="motion-axis-group">' +
                '<div class="motion-axis-label" data-i18n="web.control.build_plate">Build Plate</div>' +
                '<div class="control-group motion-vertical">' +
                '<button class="btn btn-secondary" data-motion-btn onclick="Deneb.bedUp()" data-i18n="web.control.bed_up">Up</button>' +
                '<button class="btn btn-secondary" data-motion-btn onclick="Deneb.bedDown()" data-i18n="web.control.bed_down">Down</button>' +
                '</div></div>' +
                '</div></div>' +
                '<div class="motion-toolbar">' +
                '<div class="motion-step-row">' +
                '<label class="motion-step-label" for="motion-step-input" data-i18n="web.control.step_input">Step (mm)</label>' +
                '<input class="input motion-step-input" id="motion-step-input" type="text" inputmode="numeric" value="10" onblur="Deneb.motion.setStepFromInput()">' +
                '</div>' +
                '<div class="motion-presets">' +
                '<button class="btn btn-secondary" type="button" data-motion-step="1" onclick="Deneb.setMotionStep(1)">1</button>' +
                '<button class="btn btn-secondary" type="button" data-motion-step="10" onclick="Deneb.setMotionStep(10)">10</button>' +
                '<button class="btn btn-secondary" type="button" data-motion-step="50" onclick="Deneb.setMotionStep(50)">50</button>' +
                '</div>' +
                '<div class="motion-position">' +
                '<span>X <strong id="motion-pos-x">--</strong></span>' +
                '<span>Y <strong id="motion-pos-y">--</strong></span>' +
                '<span>Z <strong id="motion-pos-z">--</strong></span>' +
                '</div></div>' +
                '<div class="motion-note" id="motion-note" data-i18n="web.control.motion_ready">Motion controls are ready.</div>' +
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
