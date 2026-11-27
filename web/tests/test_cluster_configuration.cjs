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
const pla = '506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9';
const petg = '1cbfaeb3-1906-4b26-b2e7-6f777a8c197a';

function getPrinter(socketPath) {
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
        req.setTimeout(2000, () => req.destroy(new Error('Cluster request timeout')));
    });
}

async function main() {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'c80-'));
    const store = (name, value) => fs.writeFileSync(path.join(tmp, name), value);
    for (const name of ['bin', 'config', 'delta', 'override'])
        fs.mkdirSync(path.join(tmp, name));
    store('bin/uci', `#!/bin/sh
printf '%s\\n' "$3" >> uci-calls
package=\${3%%.*}
for source in delta override config; do
    [ -r "$source/$package" ] || continue
    while IFS='=' read -r key value; do
        if [ "$key" = "$3" ]; then
            printf '%s\\n' "$value"
            exit 0
        fi
    done < "$source/$package"
done
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
        const overridden = await getPrinter(socketPath);
        assert.equal(overridden.latest_available_firmware, '1.0');
        assert.equal(overridden.firmware_update_status, 'up_to_date');
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
