/* SPDX-License-Identifier: MPL-2.0 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const context = {};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../www/js/ui.js'), 'utf8'), context);
const ui = context.Deneb.ui;
const idle = {status: 'idle', connected: true, is_printing: false, is_paused: false, has_error: false};
/* Motion controls follow the backend's manual admission while a queued
 * operation (for example Home after its RPC returned) still runs. */
assert.equal(ui.motionAllowed(Object.assign({}, idle, {manual_action_allowed: false})), false);
assert.equal(ui.motionAllowed(Object.assign({}, idle, {manual_action_allowed: true})), true);
assert.equal(ui.motionAllowed(idle), true, 'older status without the field keeps prior behavior');
console.log('Web motion readiness passed');
