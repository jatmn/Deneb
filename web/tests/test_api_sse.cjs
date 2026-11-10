/* SPDX-License-Identifier: MPL-2.0 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const timers = new Map();
const sources = [];
let id = 0;
const context = {
    localStorage: {getItem: () => null, setItem() {}, removeItem() {}},
    window: {location: {}},
    setTimeout(fn) { timers.set(++id, fn); return id; },
    clearTimeout(key) { timers.delete(key); },
    EventSource: function() { this.close = () => { this.closed = true; }; sources.push(this); }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname, '../www/js/api.js'), 'utf8'), context);
const api = context.Deneb.api;
api.connectSSE();
const oldError = sources[0].onerror;
oldError();
oldError();
assert.equal(timers.size, 1, 'only one pending reconnect');
api.disconnectSSE();
assert.equal(timers.size, 0, 'explicit disconnect cancels reconnect');
oldError();
assert.equal(timers.size, 0, 'obsolete source cannot reconnect');
api.connectSSE();
oldError();
assert.equal(timers.size, 0, 'obsolete source cannot replace a new connection');
const active = sources.at(-1);
active.onerror();
assert.equal(active.closed, true, 'native retry is closed before our retry');
const retry = [...timers.values()][0];
timers.clear();
retry();
assert.equal(sources.length, 3);
api.disconnectSSE();
console.log('SSE client lifecycle passed');
