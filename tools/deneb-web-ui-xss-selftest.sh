#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Prove the Web UI jobs renderer does not parse untrusted job fields as HTML.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UI_JS="$REPO_ROOT/web/www/js/ui.js"

if [ ! -f "$UI_JS" ]; then
    echo "FAIL: missing $UI_JS" >&2
    exit 1
fi

if grep -E 'innerHTML|outerHTML|document\.write' "$UI_JS"; then
    echo "FAIL: HTML sinks remain in web/www/js/ui.js" >&2
    exit 1
fi
echo "PASS: web/www/js/ui.js has no innerHTML/outerHTML/document.write sinks"

if ! command -v node >/dev/null 2>&1; then
    echo "FAIL: node is required for the jobs XSS renderer self-test" >&2
    exit 1
fi

UI_JS="$UI_JS" node <<'EOF'
var fs = require('fs');
var vm = require('vm');
var uiPath = process.env.UI_JS;
var failures = 0;

function fail(msg) {
    failures += 1;
    console.log('FAIL: ' + msg);
}

function pass(msg) {
    console.log('PASS: ' + msg);
}

function makeEl(tag) {
    var el = {
        tagName: String(tag).toUpperCase(),
        childNodes: [],
        attributes: {},
        style: {},
        className: '',
        disabled: false,
        parentNode: null,
        id: '',
        getAttribute: function(name) {
            return Object.prototype.hasOwnProperty.call(this.attributes, name)
                ? this.attributes[name] : null;
        },
        setAttribute: function(name, value) {
            this.attributes[name] = String(value);
            if (name === 'class') this.className = String(value);
            if (name === 'id') this.id = String(value);
        },
        appendChild: function(child) {
            this.childNodes.push(child);
            child.parentNode = this;
            return child;
        },
        removeChild: function(child) {
            var i = this.childNodes.indexOf(child);
            if (i < 0) throw new Error('removeChild: not found');
            this.childNodes.splice(i, 1);
            child.parentNode = null;
            return child;
        }
    };
    Object.defineProperty(el, 'firstChild', {
        get: function() { return this.childNodes[0] || null; }
    });
    Object.defineProperty(el, 'textContent', {
        get: function() {
            if (this.childNodes.length === 0) return this._text || '';
            return this.childNodes.map(function(c) { return c.textContent; }).join('');
        },
        set: function(value) {
            this.childNodes = [];
            this._text = value == null ? '' : String(value);
        }
    });
    Object.defineProperty(el, 'innerHTML', {
        get: function() { throw new Error('innerHTML getter used'); },
        set: function() { throw new Error('innerHTML setter used'); }
    });
    Object.defineProperty(el, 'outerHTML', {
        get: function() { throw new Error('outerHTML getter used'); },
        set: function() { throw new Error('outerHTML setter used'); }
    });
    return el;
}

function collectTags(el, acc) {
    acc = acc || [];
    acc.push(el.tagName);
    el.childNodes.forEach(function(child) { collectTags(child, acc); });
    return acc;
}

function collectAttrValues(el, acc) {
    acc = acc || [];
    Object.keys(el.attributes).forEach(function(k) { acc.push(el.attributes[k]); });
    acc.push(el.className);
    if (el.style && el.style.width) acc.push(el.style.width);
    el.childNodes.forEach(function(child) { collectAttrValues(child, acc); });
    return acc;
}

function findByClass(el, cls) {
    var hay = ' ' + el.className + ' ';
    if (hay.indexOf(' ' + cls + ' ') !== -1) return el;
    for (var i = 0; i < el.childNodes.length; i++) {
        var found = findByClass(el.childNodes[i], cls);
        if (found) return found;
    }
    return null;
}

var jobsCurrent = makeEl('div');
jobsCurrent.id = 'jobs-current';
var jobsPending = makeEl('div');
jobsPending.id = 'jobs-pending';
var jobsHistory = makeEl('div');
jobsHistory.id = 'jobs-history';
var byId = {
    'jobs-current': jobsCurrent,
    'jobs-pending': jobsPending,
    'jobs-history': jobsHistory
};

var document = {
    getElementById: function(id) { return byId[id] || null; },
    createElement: function(tag) { return makeEl(tag); },
    querySelectorAll: function() { return []; },
    write: function() { throw new Error('document.write used'); },
    importNode: function() { throw new Error('unexpected importNode'); }
};

var context = {
    Deneb: { i18n: { t: function(key) { return key; } } },
    document: document,
    DOMParser: function() {
        throw new Error('unexpected DOMParser in jobs renderer test');
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(uiPath, 'utf8'), context);
var ui = context.Deneb.ui;

var xssName = '<img src=x onerror=alert(1)>';
var xssState = 'printing"><img src=x onerror=alert(1)>';
var xssSource = '<svg onload=alert(1)>';

ui.updateJobsPage({
    current: {
        name: xssName,
        state: xssState,
        progress: 40,
        time_total: 120,
        time_elapsed: 60,
        time_left: 60
    },
    pending: [{ name: xssName, source: xssSource }],
    history: [{
        name: xssName,
        source: xssSource,
        state: xssState,
        progress: 80,
        time_total: 120,
        time_elapsed: 96
    }]
});

function assertSafeTree(label, root, expectedName) {
    var tags = collectTags(root);
    tags.forEach(function(tag) {
        if (tag !== 'DIV') fail(label + ' created non-div element ' + tag);
    });
    var attrs = collectAttrValues(root);
    attrs.forEach(function(value) {
        if (String(value).indexOf('<') !== -1 || String(value).indexOf('onerror') !== -1 ||
            String(value).indexOf('onload') !== -1) {
            fail(label + ' leaked markup into an attribute: ' + value);
        }
    });
    var nameEl = findByClass(root, 'jobs-item-name');
    if (!nameEl) fail(label + ' missing jobs-item-name');
    else if (nameEl.textContent !== expectedName) {
        fail(label + ' name text was ' + JSON.stringify(nameEl.textContent));
    }
}

assertSafeTree('current', jobsCurrent, xssName);
assertSafeTree('pending', jobsPending, xssName);
assertSafeTree('history', jobsHistory, xssName);

var currentStatus = findByClass(jobsCurrent, 'jobs-item-status');
if (!currentStatus) fail('current missing status');
else {
    if (currentStatus.className !== 'jobs-item-status') {
        fail('malicious state became a class: ' + currentStatus.className);
    }
    if (currentStatus.textContent !== xssState) {
        fail('current status text was ' + JSON.stringify(currentStatus.textContent));
    }
}

ui.updateJobsPage({
    current: {
        name: 'benchy.gcode',
        state: 'printing',
        progress: 25,
        time_total: 60,
        time_elapsed: 15,
        time_left: 45
    },
    pending: [],
    history: [{ name: 'done.gcode', state: 'completed', progress: 100, time_total: 10, time_elapsed: 10, source: 'local' }]
});

var benignStatus = findByClass(jobsCurrent, 'jobs-item-status');
if (!benignStatus || benignStatus.className !== 'jobs-item-status printing') {
    fail('printing state class was ' + (benignStatus && benignStatus.className));
}
if (findByClass(jobsHistory, 'jobs-item-status').className !== 'jobs-item-status completed') {
    fail('completed history class missing');
}
if (jobsPending.textContent.indexOf('No pending jobs') === -1) {
    fail('empty pending state missing');
}

if (failures) {
    process.exit(1);
}
pass('jobs renderer treats untrusted fields as text');
pass('allowlisted job states still apply CSS classes');
EOF
