/**
 * SPDX-License-Identifier: MPL-2.0
 * Deneb Web UI - Internationalization
 */

var Deneb = Deneb || {};

Deneb.i18n = {
    strings: {},
    lang: localStorage.getItem('deneb_lang') || 'en',

    load: function(lang) {
        var self = this;
        lang = lang || self.lang;
        return fetch('/api/v1/deneb/locale/' + lang)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                self.strings = data;
                self.lang = lang;
                localStorage.setItem('deneb_lang', lang);
                self.apply();
            })
            .catch(function() {
                /* Fallback to English */
                if (lang !== 'en') return self.load('en');
            });
    },

    t: function(key) {
        return this.strings[key] || key;
    },

    apply: function() {
        var els = document.querySelectorAll('[data-i18n]');
        for (var i = 0; i < els.length; i++) {
            var key = els[i].getAttribute('data-i18n');
            els[i].textContent = this.t(key);
        }
    }
};
