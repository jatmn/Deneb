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

async function testStatusFallback() {
    const updates = [];
    const requests = [];
    api.onStatus(data => updates.push(data.status));
    api.get = path => {
        assert.equal(path, '/printer');
        return new Promise(resolve => requests.push(resolve));
    };

    api.connectSSE();
    const startup = api.pollStatus();
    requests.shift()({status: 'idle'});
    await startup;
    assert.deepEqual(updates, ['idle']);

    const delayed = api.pollStatus();
    sources.at(-1).onmessage({data: '{"status":"printing"}'});
    api.disconnectSSE();
    requests.shift()({status: 'idle'});
    await delayed;
    assert.deepEqual(updates, ['idle', 'printing'], 'a delayed poll cannot replace streamed status');

    const disconnected = api.pollStatus();
    requests.shift()({status: 'offline'});
    await disconnected;
    assert.deepEqual(updates, ['idle', 'printing', 'offline']);

    api.connectSSE();
    sources.at(-1).onmessage({data: '{"status":"idle"}'});
    api.pollStatus();
    assert.equal(requests.length, 0, 'an active status stream owns display updates');
    api.disconnectSSE();
}

testStatusFallback().then(() => {
    console.log('SSE client lifecycle and status fallback passed');
}).catch(error => {
    console.error(error);
    process.exitCode = 1;
});
