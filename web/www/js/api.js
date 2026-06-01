/**
 * SPDX-License-Identifier: MPL-2.0
 * Deneb Web UI - API client and SSE connection
 */

var Deneb = Deneb || {};

Deneb.api = {
    base: '/api/v1',
    token: localStorage.getItem('deneb_token'),
    tokenExpiry: parseInt(localStorage.getItem('deneb_token_expiry') || '0', 10),
    sseSource: null,
    statusCallbacks: [],

    request: function(method, path, body) {
        var opts = {
            method: method,
            headers: {}
        };
        if (this.token) {
            opts.headers['Authorization'] = 'Bearer ' + this.token;
        }
        if (body !== undefined && body !== null) {
            if (typeof body === 'string') {
                opts.headers['Content-Type'] = 'text/plain';
                opts.body = body;
            } else {
                opts.headers['Content-Type'] = 'application/json';
                opts.body = JSON.stringify(body);
            }
        }
        return fetch(this.base + path, opts)
            .then(function(r) {
                if (r.status === 401) {
                    window.location.hash = '#/setup';
                    throw new Error('Unauthorized');
                }
                return r.json().catch(function() { return {}; }).then(function(data) {
                    if (!r.ok) {
                        throw new Error(data.message || ('HTTP ' + r.status));
                    }
                    return data;
                });
            });
    },

    get: function(path) { return this.request('GET', path); },
    post: function(path, body) { return this.request('POST', path, body); },
    put: function(path, body) { return this.request('PUT', path, body); },

    isTokenValid: function() {
        return this.token && Date.now() / 1000 < this.tokenExpiry;
    },

    connectSSE: function() {
        this.disconnectSSE();
        if (!this.isTokenValid() && this.token) {
            this.setToken(null);
            window.location.hash = '#/setup';
            return;
        }
        var url = this.base + '/deneb/events';
        if (this.token) {
            url += '?token=' + this.token;
        }
        var self = this;
        this.sseSource = new EventSource(url);
        this.sseSource.onmessage = function(e) {
            try {
                var data = JSON.parse(e.data);
                for (var i = 0; i < self.statusCallbacks.length; i++) {
                    self.statusCallbacks[i](data);
                }
            } catch(err) {}
        };
        this.sseSource.onerror = function() {
            /* Reconnect after 3s */
            setTimeout(function() { self.connectSSE(); }, 3000);
        };
    },

    disconnectSSE: function() {
        if (this.sseSource) {
            this.sseSource.close();
            this.sseSource = null;
        }
    },

    onStatus: function(callback) {
        this.statusCallbacks.push(callback);
    },

    setToken: function(token, expires) {
        this.token = token;
        this.tokenExpiry = expires || 0;
        if (token) {
            localStorage.setItem('deneb_token', token);
            localStorage.setItem('deneb_token_expiry', String(expires || 0));
        } else {
            localStorage.removeItem('deneb_token');
            localStorage.removeItem('deneb_token_expiry');
        }
    },

    /* Poll status via GET when SSE is not available */
    pollStatus: function() {
        var self = this;
        this.get('/printer').then(function(data) {
            for (var i = 0; i < self.statusCallbacks.length; i++) {
                self.statusCallbacks[i](data);
            }
        }).catch(function() {});
    }
};
