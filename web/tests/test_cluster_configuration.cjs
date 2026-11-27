// SPDX-License-Identifier: MPL-2.0
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');

const apiBinary = process.argv[2];
const guid = '11111111-2222-4333-8aaa-bbbbbbbbbbbb';
const fallbackGuid = '22222222-2222-4333-8aaa-bbbbbbbbbbbb';
const defaultGuid = '00000000-0000-0000-0000-000000000000';
const pla = '506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9';
const petg = '1cbfaeb3-1906-4b26-b2e7-6f777a8c197a';

function getPrinter(socketPath, timeout = 2000) {
    return new Promise((resolve, reject) => {
        const req = http.get({socketPath, path: '/cluster-api/v1/printers', agent: false}, (res) => {
            let body = '';
            res.setEncoding('utf8');
            res.on('data', (chunk) => { body += chunk; });
            res.once('error', reject);
            res.on('end', () => {
                try {
                    assert.equal(res.statusCode, 200, body);
                    const records = JSON.parse(body);
                    assert.equal(records.length, 1);
                    resolve(records[0]);
                } catch (error) {
                    reject(error);
                }
            });
        });
        req.once('error', reject);
        req.setTimeout(timeout, () => req.destroy(new Error('Cluster request timeout')));
    });
}

async function main() {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'c80-'));
    const store = (name, value) => fs.writeFileSync(path.join(tmp, name), value);
    for (const name of ['bin', 'config', 'delta', 'override'])
        fs.mkdirSync(path.join(tmp, name));
    store('bin/uci', `#!/bin/sh
option=\${3:-$2}
printf '%s\\n' "$option" >> uci-calls
if [ -f fail-always ] && IFS= read -r failing < fail-always && [ "$option" = "$failing" ]; then
    if [ -f fail-slow ]; then
        rm fail-slow
        sleep 5.1
    fi
    printf 'uci: I/O error\\n' >&2
    exit 1
fi
if [ -f fail-once ]; then
    IFS=' ' read -r failing mode < fail-once
    if [ "$option" = "$failing" ]; then
        rm fail-once
        case "$mode" in
            no-output) exit 0 ;;
            signal) kill -TERM "$$" ;;
            io) [ "$1" = -q ] || printf 'uci: I/O error\\n' >&2 ;;
        esac
        exit 1
    fi
fi
package=\${option%%.*}
base="config/$package"
[ ! -f "override/$package" ] || base="override/$package"
if [ -r "$base" ]; then
    for file in "delta/$package" "$base"; do
        [ -r "$file" ] || continue
        while IFS='=' read -r key value; do
            if [ "$key" = "$option" ]; then
                printf '%s\\n' "$value"
                exit 0
            fi
        done < "$file"
    done
fi
[ "$1" = -q ] || printf 'uci: Entry not found\\n' >&2
exit 1
`);
    fs.chmodSync(path.join(tmp, 'bin/uci'), 0o755);
    store('uci-calls', '');
    const calls = () => fs.readFileSync(path.join(tmp, 'uci-calls'), 'utf8').trim().split('\n').filter(Boolean);
    const firmware = 'ultimaker.version.nr=1.0\nultimaker.version.latest=1.1\n';
    const config = 'ultimaker.option.printer_name=Original Name\n' +
        'ultimaker.option.host_guid=' + guid + '\n' +
        'ultimaker.option.material_guid=' + pla + '\n' +
        'ultimaker.option.nozzle_size=0.4\n' + firmware;
    store('config/ultimaker', config);
    store('config/deneb', 'deneb.system.guid=' + fallbackGuid + '\n');
    const socketPath = path.join(tmp, 'api.sock');
    const api = spawn(apiBinary, [], {
        cwd: tmp,
        env: {...process.env, PATH: path.join(tmp, 'bin') + ':' + process.env.PATH},
        stdio: ['ignore', 'ignore', 'pipe']
    });
    let stderr = '';
    api.stderr.setEncoding('utf8').on('data', (chunk) => { stderr += chunk; });
    const stopped = new Promise((resolve) => {
        api.once('close', resolve);
        api.on('error', (error) => { stderr += error.message + '\n'; });
    });
    try {
        for (let i = 0; i < 80 && api.exitCode === null && !fs.existsSync(socketPath); i++)
            await delay(25);
        assert.ok(fs.existsSync(socketPath), stderr);
        store('fail-once', 'ultimaker.option.host_guid io\n');
        const cold = await getPrinter(socketPath);
        assert.equal(cold.uuid, defaultGuid, 'cold failure used the alternate GUID');
        assert.equal(cold.friendly_name, 'Original Name');
        store('uci-calls', '');
        const first = await getPrinter(socketPath);
        assert.equal(first.friendly_name, 'Original Name');
        assert.equal(first.uuid, guid);
        assert.equal(first.configuration[0].print_core_id, '0.4 mm');
        assert.equal(first.configuration[0].material.guid, pla);
        assert.equal(first.firmware_update_status, 'update_available');
        assert.equal(calls().length, 6);

        for (let i = 0; i < 3; i++)
            assert.deepEqual(await getPrinter(socketPath), first);
        assert.equal(calls().length, 6, 'unchanged status reran UCI commands');

        for (const mode of ['io', 'no-output', 'signal']) {
            const before = calls().length;
            store('fail-once', 'ultimaker.option.host_guid ' + mode + '\n');
            // Change the file stamp without changing the effective options.
            store('config/ultimaker', config + '# ' + mode + '\n');
            assert.deepEqual(await getPrinter(socketPath), first,
                'transient UCI failure replaced a published snapshot');
            assert.ok(calls().length > before, 'test must trigger a full refresh');
            const failed = calls().length;
            assert.deepEqual(await getPrinter(socketPath), first);
            assert.ok(calls().length > failed, 'refresh must retry before cache expiry');
        }

        store('fail-always', 'ultimaker.option.host_guid\n');
        store('config/ultimaker', config + '# repeated failure\n');
        const beforePersistent = calls().length;
        assert.deepEqual(await getPrinter(socketPath), first);
        assert.equal(calls().length, beforePersistent + 6);
        // A refresh taking longer than the interval must still get a cooldown.
        store('fail-slow', '');
        assert.deepEqual(await getPrinter(socketPath, 8000), first);
        assert.equal(calls().length, beforePersistent + 12, 'one retry must be immediate');
        for (let i = 0; i < 3; i++)
            assert.deepEqual(await getPrinter(socketPath), first);
        assert.equal(calls().length, beforePersistent + 12, 'persistent failure reran UCI on every poll');
        await delay(5100);
        assert.deepEqual(await getPrinter(socketPath), first);
        assert.equal(calls().length, beforePersistent + 18, 'failed refresh was never retried');
        fs.unlinkSync(path.join(tmp, 'fail-always'));

        // The new delta must trigger an immediate refresh despite the recent failure.
        store('delta/ultimaker', 'ultimaker.option.printer_name=Staged Name\n');
        assert.equal((await getPrinter(socketPath)).friendly_name, 'Staged Name');
        fs.unlinkSync(path.join(tmp, 'delta/ultimaker'));
        store('config/replacement', 'ultimaker.option.material_guid=' + petg + '\n' +
              'ultimaker.option.nozzle_size=0.6\n' + firmware);
        fs.renameSync(path.join(tmp, 'config/replacement'), path.join(tmp, 'config/ultimaker'));
        const replaced = await getPrinter(socketPath);
        assert.equal(replaced.friendly_name, replaced.unique_name);
        assert.equal(replaced.uuid, fallbackGuid);
        assert.equal(replaced.configuration[0].print_core_id, '0.6 mm');
        assert.equal(replaced.configuration[0].material.guid, petg);
        assert.equal(replaced.configuration[0].material.material, 'PETG');

        store('override/ultimaker', 'ultimaker.version.latest=1.0\n');
        store('delta/ultimaker', 'ultimaker.option.printer_name=Delta on Override\n');
        const overridden = await getPrinter(socketPath);
        assert.equal(overridden.latest_available_firmware, '1.0');
        assert.notEqual(overridden.firmware_version, '1.0');
        assert.equal(overridden.firmware_update_status, 'update_available');
        assert.equal(overridden.configuration[0].print_core_id, '0.4 mm');
        assert.equal(overridden.configuration[0].material.guid, pla);
        assert.equal(overridden.friendly_name, 'Delta on Override');
        assert.equal(overridden.uuid, fallbackGuid);
    } finally {
        if (api.exitCode === null && api.signalCode === null) api.kill('SIGTERM');
        const force = setTimeout(() => api.kill('SIGKILL'), 1000);
        await stopped;
        clearTimeout(force);
        fs.rmSync(tmp, {recursive: true, force: true});
    }
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
