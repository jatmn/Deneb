/**
 * SPDX-License-Identifier: MPL-2.0
 * Deneb Web UI - Main application
 */

var Deneb = Deneb || {};

Deneb.motion = {
    stepValue: 10,

    currentStep: function() {
        return this.stepValue;
    },

    normalizeStep: function(value) {
        var text = String(value == null ? '' : value).trim();
        if (!/^[0-9]+$/.test(text)) return null;
        var parsed = parseInt(text, 10);
        if (parsed < 1 || parsed > 50) return null;
        return parsed;
    },

    setStep: function(value, silent) {
        var normalized = this.normalizeStep(value);
        if (normalized === null) {
            if (!silent) {
                Deneb.ui.showError(Deneb.i18n.t('web.control.step_invalid') || 'Enter a whole number from 1 to 50 mm.');
            }
            Deneb.ui.updateMotionState();
            return false;
        }
        this.stepValue = normalized;
        Deneb.ui.updateMotionState();
        return true;
    },

    setStepFromInput: function() {
        var input = document.getElementById('motion-step-input');
        if (!input) return true;
        return this.setStep(input.value, false);
    }
};

Deneb.init = function() {
    /* Load locale */
    Deneb.i18n.load();

    /* Check if setup is needed */
    Deneb.api.get('/deneb/version').then(function(data) {
        if (!data.setup_complete) {
            window.location.hash = '#/setup';
        }
    }).catch(function() {});

    /* Connect SSE for live status */
    Deneb.api.connectSSE();
    Deneb.api.onStatus(function(data) {
        Deneb.ui.updateStatus(data);
    });

    /* Router */
    window.addEventListener('hashchange', Deneb.route);
    Deneb.route();

    /* Initial status poll */
    Deneb.api.pollStatus();
};

Deneb.route = function() {
    var hash = window.location.hash || '#/status';
    var page = hash.replace('#/', '') || 'status';
    if (Deneb.ui.pages[page]) {
        Deneb.ui.showPage(page);
        /* Load page-specific data */
        if (page === 'about') Deneb.loadAbout();
        if (page === 'settings') Deneb.loadSettings();
    }
};

Deneb.loadAbout = function() {
    Deneb.api.get('/deneb/version').then(function(data) {
        var el = document.getElementById('about-version');
        if (el) el.textContent = data.deneb_version || '--';
    });
};

Deneb.loadSettings = function() {
    Deneb.api.get('/deneb/config').then(function(data) {
        var toggle = document.getElementById('auth-toggle');
        if (toggle) toggle.classList.toggle('on', data.auth_enabled);
        var langSel = document.getElementById('lang-select');
        if (langSel) langSel.value = data.language || 'en';
    });
};

/* Commands */
Deneb.cmd = function(action) {
    Deneb.api.request('PUT', '/print_job/state', action).then(function() {
        Deneb.api.pollStatus();
    });
};

Deneb.cmdAbort = function() {
    if (confirm(Deneb.i18n.t('web.control.confirm_abort') || 'Cancel the print?')) {
        Deneb.api.request('PUT', '/print_job/state', 'abort').then(function() {
            Deneb.api.pollStatus();
        });
    }
};

Deneb.cmdCooldown = function() {
    Deneb.api.put('/printer/bed/temperature', {temperature: 0});
    Deneb.api.put('/printer/heads/0/extruders/0/hotend/temperature', {temperature: 0});
};

Deneb.setNozzle = function() {
    var val = parseInt(document.getElementById('nozzle-slider').value, 10);
    if (val > 260) val = 260;
    Deneb.api.put('/printer/heads/0/extruders/0/hotend/temperature', {temperature: val});
};

Deneb.setBed = function() {
    var val = parseInt(document.getElementById('bed-slider').value, 10);
    if (val > 110) val = 110;
    Deneb.api.put('/printer/bed/temperature', {temperature: val});
};

Deneb.runMotion = function(fn) {
    fn().then(function() {
        Deneb.api.pollStatus();
    }).catch(function(err) {
        Deneb.ui.showError(err.message || Deneb.i18n.t('web.control.motion_failed'));
    });
};

Deneb.jog = function(axis, direction) {
    if (!Deneb.motion.setStepFromInput()) return;
    var step = Deneb.motion.currentStep() * direction;
    Deneb.runMotion(function() {
        return Deneb.api.jog(axis, step);
    });
};

Deneb.setMotionStep = function(value) {
    Deneb.motion.setStep(value, false);
};

Deneb.home = function() {
    Deneb.runMotion(function() {
        return Deneb.api.home();
    });
};

Deneb.zHome = function() {
    Deneb.runMotion(function() {
        return Deneb.api.zHome();
    });
};

Deneb.bedUp = function() {
    Deneb.runMotion(function() {
        return Deneb.api.bedUp();
    });
};

Deneb.bedDown = function() {
    Deneb.runMotion(function() {
        return Deneb.api.bedDown();
    });
};

Deneb.toggleAuth = function() {
    var toggle = document.getElementById('auth-toggle');
    var currentlyOn = toggle && toggle.classList.contains('on');
    var body = {auth_required: !currentlyOn};
    if (currentlyOn) {
        body.password = '';
    } else {
        var pw = document.getElementById('new-password').value;
        if (!pw) {
            Deneb.ui.showError('Enter a password first');
            return;
        }
        body.password = pw;
    }
    Deneb.api.post('/deneb/setup', body).then(function() {
        Deneb.loadSettings();
    });
};

Deneb.changePassword = function() {
    var pw = document.getElementById('new-password').value;
    if (!pw) return;
    Deneb.api.post('/deneb/setup', {password: pw, auth_required: true}).then(function() {
        Deneb.ui.showError('Password changed');
    });
};

Deneb.setup = function(withAuth) {
    var pw = document.getElementById('setup-password').value;
    var confirm = document.getElementById('setup-confirm').value;
    if (withAuth && pw !== confirm) {
        Deneb.ui.showError('Passwords do not match');
        return;
    }
    Deneb.api.post('/deneb/setup', {
        password: withAuth ? pw : '',
        auth_required: withAuth
    }).then(function(data) {
        if (data.token) Deneb.api.setToken(data.token, data.expires);
        window.location.hash = '#/status';
    });
};

/* Start */
document.addEventListener('DOMContentLoaded', Deneb.init);
