/**
 * SPDX-License-Identifier: MPL-2.0
 * Deneb Web UI - Main application
 */

var Deneb = Deneb || {};

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
