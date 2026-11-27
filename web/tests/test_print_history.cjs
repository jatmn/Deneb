// SPDX-License-Identifier: MPL-2.0
const assert = require('node:assert/strict');
const { spawn, execFileSync } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');

const [apiBinary, writerBinary] = process.argv.slice(2);
const children = [];

async function withinDeadline(description, predicate) {
    const deadline = performance.now() + 4000;
    do {
        if (predicate()) return;
        await delay(10);
    } while (performance.now() < deadline);
    assert.fail(description);
}

function start(binary, args, cwd, mode) {
    const child = spawn(binary, args, { cwd, env: { ...process.env, DENEB_HISTORY_TEST_RENAME: mode } });
    const run = { child, finished: false, output: '', error: '' };
    child.stdout.setEncoding('utf8').on('data', (chunk) => { run.output += chunk; });
    child.stderr.setEncoding('utf8').on('data', (chunk) => { run.error += chunk; });
    run.done = new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('close', (code, signal) => {
            run.finished = true;
            resolve({ code, signal });
        });
    });
    run.done.catch(() => {});
    children.push(run);
    return run;
}

function getJobs(socketPath) {
    return new Promise((resolve, reject) => {
        const req = http.get({ socketPath, path: '/api/v1/deneb/print_jobs', agent: false }, (res) => {
            let body = '';
            res.setEncoding('utf8').on('data', (chunk) => { body += chunk; });
            res.on('error', reject);
            res.on('end', () => {
                try {
                    assert.equal(res.statusCode, 200);
                    resolve(JSON.parse(body));
                } catch (err) { reject(err); }
            });
        });
        req.on('error', reject);
        req.setTimeout(1000, () => req.destroy(new Error('Jobs response timed out')));
    });
}

function id(record) {
    return Number(record.uuid.slice(4));
}

function assertNewest(history, last) {
    assert.ok(history.length > 1);
    assert.equal(id(history.at(-1)), last);
    for (let i = 1; i < history.length; i++)
        assert.equal(id(history[i]), id(history[i - 1]) + 1);
}

function fileMode(file) {
    const fd = fs.openSync(file, 'r');
    try {
        return fs.fstatSync(fd).mode & 0o777;
    } finally {
        fs.closeSync(fd);
    }
}

async function main() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'dh-'));
    const directory = path.join(root, 'tmp');
    const historyFile = path.join(directory, 'deneb-print-history.json');
    const socket = path.join(root, 'api.sock');
    try {
        fs.mkdirSync(directory);
        const limit = Number(execFileSync(writerBinary, ['limit'], { cwd: root, encoding: 'utf8' }));
        start(apiBinary, [], root, 'normal');
        await withinDeadline('API socket did not start', () => fs.existsSync(socket));
        assert.deepEqual(await getJobs(socket), { current: null, pending: [], history: [] });

        const append = (first, count = 1, pause = 0, mode = 'normal') =>
            start(writerBinary, [first, count, pause].map(String), root, mode);
        const initial = append(0, 20);
        assert.equal((await initial.done).code, 0, initial.error);
        assert.equal(fileMode(historyFile), 0o664 & ~process.umask());
        const writer = append(20, 500, 2);
        let latest = 19;
        let observed = 0;
        while (!writer.finished) {
            const history = (await getJobs(socket)).history;
            const next = id(history.at(-1));
            assert.ok(next >= latest);
            assertNewest(history, next);
            assert.ok(fs.readFileSync(historyFile).length <= limit);
            if (next > latest && next < 519) observed++;
            latest = next;
            await delay(5);
        }
        assert.equal((await writer.done).code, 0, writer.error);
        assert.ok(observed > 1, 'API observed atomically published history during writes');
        let contents = fs.readFileSync(historyFile, 'utf8');
        let history = JSON.parse(contents);
        assertNewest(history, 519);
        assert.ok(id(history[0]) > 0, 'Retention evicted the oldest records');
        assert.deepEqual((await getJobs(socket)).history, history);

        // Existing oversized histories can contain pretty whitespace and escaped
        // punctuation, including in additional nested fields.
        const legacy = Array.from({ length: 400 }, (_, i) => ({
            uuid: 'job-' + (i + 1000), name: 'legacy "}\\,[\n',
            source: 'USB', state: 'completed', detail: { boundaries: ['{', ']'], note: 'x'.repeat(125) }
        }));
        const records = legacy.map((item) => JSON.stringify(item, null, 2));
        contents = ' \n[\n' + records.join(',\n') + '\n]\n' + ' '.repeat(limit);
        assert.ok(Buffer.byteLength(contents) > 65536);
        const legacyFile = fs.openSync(historyFile, 'w');
        try {
            fs.writeFileSync(legacyFile, contents);
            fs.fchmodSync(legacyFile, 0o640);
        } finally {
            fs.closeSync(legacyFile);
        }
        history = (await getJobs(socket)).history;
        assertNewest(history, 1399);
        assert.deepEqual(history, legacy.slice(-history.length));
        const firstRetained = id(history[0]) - 1000;
        assert.ok(Buffer.byteLength('[' + records.slice(firstRetained - 1).join(',\n') + ']') > limit);

        const migration = append(1400);
        assert.equal((await migration.done).code, 0, migration.error);
        assert.equal(fileMode(historyFile), 0o640);
        contents = fs.readFileSync(historyFile, 'utf8');
        assert.ok(Buffer.byteLength(contents) <= limit);
        history = JSON.parse(contents);
        assertNewest(history, 1400);
        assert.deepEqual(history.slice(0, -1), legacy.slice(-(history.length - 1)));
        assert.deepEqual((await getJobs(socket)).history, history);

        const failed = append(1401, 1, 0, 'fail');
        assert.equal((await failed.done).code, 1);
        assert.equal(fs.readFileSync(historyFile, 'utf8'), contents);
        assert.deepEqual(fs.readdirSync(directory), ['deneb-print-history.json']);

        const interrupted = append(1402, 1, 0, 'hold');
        await withinDeadline('Writer did not reach the publication boundary', () =>
            interrupted.output.includes('ready'));
        assert.deepEqual((await getJobs(socket)).history, history);
        interrupted.child.kill('SIGKILL');
        assert.equal((await interrupted.done).signal, 'SIGKILL');
        assert.equal(fs.readFileSync(historyFile, 'utf8'), contents);
        assert.deepEqual((await getJobs(socket)).history, history);
        assert.deepEqual(fs.readdirSync(directory).sort(),
            ['deneb-print-history.json', 'deneb-print-history.json.tmp']);
        assert.ok(fs.readFileSync(historyFile + '.tmp').length <= limit);

        const retried = append(1401);
        assert.equal((await retried.done).code, 0, retried.error);
        assertNewest((await getJobs(socket)).history, 1401);
        assert.deepEqual(fs.readdirSync(directory), ['deneb-print-history.json']);

        const oversizedRecord = { uuid: 'job-1402', name: 'x'.repeat(limit) };
        fs.writeFileSync(historyFile, JSON.stringify([...legacy.slice(-3), oversizedRecord]));
        assert.deepEqual((await getJobs(socket)).history, legacy.slice(-3));
        const oversizedAppend = await append(1403).done;
        assert.equal(oversizedAppend.code, 0);
        history = (await getJobs(socket)).history;
        assert.deepEqual(history.slice(0, -1), legacy.slice(-3));
        assert.equal(id(history.at(-1)), 1403);
        assert.ok(fs.readFileSync(historyFile).length <= limit);

        const damagedHistories = [
            '',
            '[{"name":"incomplete',
            '[' + records.join(',\n') + ',{"name":}]',
        ];
        for (const damaged of damagedHistories) {
            fs.writeFileSync(historyFile, damaged);
            assert.deepEqual((await getJobs(socket)).history, []);
            assert.equal((await append(1404, 1, 0, 'fail').done).code, 1);
            assert.equal(fs.readFileSync(historyFile, 'utf8'), damaged);
            assert.equal((await append(1404, 2).done).code, 0);
            assert.deepEqual((await getJobs(socket)).history.map(id), [1404, 1405]);
        }

        fs.writeFileSync(historyFile, ' [\n ]\n');
        const empty = append(1406);
        assert.equal((await empty.done).code, 0, empty.error);
        assert.deepEqual((await getJobs(socket)).history.map(id), [1406]);
    } finally {
        for (const run of children)
            if (!run.finished) run.child.kill('SIGKILL');
        await Promise.allSettled(children.map((run) => run.done));
        fs.rmSync(root, { recursive: true, force: true });
    }
}

main().catch((error) => {
    for (const run of children) process.stderr.write(run.error);
    console.error(error);
    process.exitCode = 1;
});
