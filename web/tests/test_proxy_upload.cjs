// SPDX-License-Identifier: MPL-2.0
const assert = require('node:assert/strict');
const { spawn, execFileSync } = require('node:child_process');
const { createHash, randomBytes } = require('node:crypto');
const { once } = require('node:events');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const { deflateSync } = require('node:zlib');

const [apiBinary, lighttpd, packagedConfig, cmake, unzip, digestFixture] = process.argv.slice(2);
const DEADLINE_MS = 2000;
const SSE_CLIENTS = 4;
const STATUS_PATH = '/api/v1/printer/status';
const LOCALE_PATH = '/api/v1/deneb/locale/test';
const UPLOAD_PATH = '/cluster-api/v1/print_jobs';
const AUTH_UPLOAD_PATH = '/api/v1/print_job';
const boundary = 'deneb-host-upload';
const sockets = new Set();
const children = [];

async function withinDeadline(description, predicate) {
    const end = performance.now() + DEADLINE_MS;
    do {
        if (predicate()) return;
        await delay(25);
    } while (performance.now() < end);
    assert.fail(description);
}

async function unusedPort() {
    const server = net.createServer();
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    const port = server.address().port;
    await new Promise((resolve, reject) => {
        server.close((error) => error ? reject(error) : resolve());
    });
    return port;
}

function startService(binary, args, cwd, env) {
    const process = spawn(binary, args, { cwd, env, stdio: ['ignore', 'ignore', 'pipe'] });
    const service = { process, log: '', stopped: false };
    process.stderr.setEncoding('utf8').on('data', (chunk) => { service.log += chunk; });
    process.on('error', (error) => { service.log += error.message + '\n'; });
    service.closed = new Promise((resolve) => {
        process.on('close', () => {
            service.stopped = true;
            resolve();
        });
    });
    children.push(service);
    return service;
}

async function stopService(service) {
    if (service.stopped) return;
    const timer = setTimeout(() => service.process.kill('SIGKILL'), DEADLINE_MS);
    service.process.kill('SIGTERM');
    try {
        await service.closed;
    } finally {
        clearTimeout(timer);
    }
}

function request(port, pathname, options, onResponse) {
    const req = http.request({
        host: '127.0.0.1', port, path: pathname, agent: false, ...options
    }, onResponse);
    req.on('socket', trackSocket);
    return req;
}

function trackSocket(socket) {
    sockets.add(socket);
    socket.once('close', () => sockets.delete(socket));
}

function collect(response, resolve, reject) {
    const chunks = [];
    response.on('data', (chunk) => chunks.push(chunk));
    response.once('error', reject);
    response.once('end', () => resolve({
        code: response.statusCode, headers: response.headers, data: Buffer.concat(chunks).toString()
    }));
}

function get(port, pathname) {
    return new Promise((resolve, reject) => {
        const req = request(port, pathname, {}, (response) => collect(response, resolve, reject));
        req.once('error', reject);
        req.setTimeout(DEADLINE_MS, () => req.destroy(new Error('GET deadline exceeded for ' + pathname)));
        req.end();
    });
}

function localeBody(length) {
    const prefix = '{"example":"';
    const suffix = 'é"}\n';
    return prefix + 'x'.repeat(length - Buffer.byteLength(prefix + suffix)) + suffix;
}

function pausedLocale(apiSocket) {
    const flag = path.join(path.dirname(apiSocket), 'small-send-buffer');
    fs.writeFileSync(flag, '', { flag: 'wx' });
    const socket = net.createConnection({ path: apiSocket, allowHalfOpen: true, highWaterMark: 1024 });
    trackSocket(socket);
    socket.setTimeout(DEADLINE_MS, () => socket.destroy(new Error('Paused locale response timed out')));
    const received = [];
    const reply = new Promise((resolve, reject) => {
        socket.once('error', reject);
        socket.once('end', () => resolve(Buffer.concat(received)));
        socket.once('close', () => reject(new Error('Locale socket closed before reply')));
    });
    reply.catch(() => {});
    socket.pause();
    const started = once(socket, 'readable').then(() => {
        assert.equal(fs.existsSync(flag), false, 'API did not arm the slow-reader socket');
    });
    socket.once('connect', () => socket.end(
        'GET ' + LOCALE_PATH + ' HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n'));
    return {
        started,
        finish: () => {
            socket.on('data', (chunk) => received.push(chunk));
            socket.resume();
            return reply;
        },
        disconnect: () => socket.destroy()
    };
}

function liveStatus(port) {
    const client = { events: 0 };
    const req = request(port, '/api/v1/deneb/events', {}, (response) => {
        try {
            assert.equal(response.statusCode, 200);
            assert.match(response.headers['content-type'], /^text\/event-stream\b/);
        } catch (error) {
            client.error = error;
        }
        let pending = '';
        response.setEncoding('utf8');
        response.on('data', (chunk) => {
            pending += chunk;
            let end;
            while ((end = pending.indexOf('\n\n')) !== -1) {
                if (pending.slice(0, end) === 'data: {}') client.events++;
                pending = pending.slice(end + 2);
            }
        });
        response.on('error', (error) => { client.error = error; });
    });
    req.on('error', (error) => { client.error = error; });
    req.end();
    client.close = () => req.destroy();
    return client;
}

function multipartPayload(body, filename) {
    return Buffer.concat([
        Buffer.from('--' + boundary + '\r\nContent-Disposition: form-data; name="owner"\r\n\r\nCura\r\n' +
            '--' + boundary + '\r\nContent-Disposition: form-data; name="file"; filename="' + filename +
            '"\r\nContent-Type: application/octet-stream\r\n\r\n'),
        body,
        Buffer.from('\r\n--' + boundary + '--\r\n')
    ]);
}

function upFilteredRgbaPng(width, height, colors) {
    function chunk(type, data) {
        const body = Buffer.concat([Buffer.from(type), data]);
        const result = Buffer.alloc(body.length + 8);
        let crc = -1;
        for (const byte of body) {
            crc ^= byte;
            for (let bit = 0; bit < 8; bit++) {
                crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
            }
        }
        result.writeUInt32BE(data.length);
        body.copy(result, 4);
        result.writeUInt32BE((~crc) >>> 0, result.length - 4);
        return result;
    }

    const header = Buffer.alloc(13);
    header.writeUInt32BE(width);
    header.writeUInt32BE(height, 4);
    header[8] = 8;
    header[9] = 6;
    const stride = width * 4 + 1;
    const scanlines = Buffer.alloc(stride * height);
    const pixel = Buffer.alloc(4);
    const previous = Buffer.alloc(4);
    for (let y = 0; y < height; y++) {
        const row = scanlines.subarray(y * stride, (y + 1) * stride);
        pixel.writeUInt32BE(colors[y % colors.length]);
        row[0] = 2; // Up filter predicts each channel from the previous row.
        row.fill(pixel.map((channel, index) => channel - previous[index]), 1);
        pixel.copy(previous);
    }
    return Buffer.concat([
        Buffer.from('89504e470d0a1a0a', 'hex'),
        chunk('IHDR', header),
        chunk('IDAT', deflateSync(scanlines)),
        chunk('IEND', Buffer.alloc(0))
    ]);
}

function startUpload(port, body, filename, { chunked = false, declaredLength } = {}) {
    const payload = multipartPayload(body, filename);
    const split = Math.floor(payload.length / 2);
    let req;
    const result = new Promise((resolve, reject) => {
        req = request(port, UPLOAD_PATH, {
            method: 'POST',
            headers: {
                'Content-Type': 'multipart/form-data; boundary=' + boundary,
                ...(chunked ? { 'Transfer-Encoding': 'chunked' } :
                    { 'Content-Length': declaredLength ?? payload.length })
            }
        }, (response) => collect(response, resolve, reject));
        req.once('error', reject);
    });
    // Cancellation rejects the client request before its response is awaited.
    result.catch(() => {});
    req.write(payload.subarray(0, split));
    return {
        finish: () => {
            req.setTimeout(DEADLINE_MS, () => req.destroy(new Error('Upload response deadline exceeded')));
            req.end(payload.subarray(split));
            return result;
        },
        cancel: () => req.destroy(),
        reset: () => req.socket.resetAndDestroy()
    };
}

function startSocketPost(apiSocket, pathname, contentType, firstBody, restBody, authorizations = []) {
    const socket = net.createConnection({ path: apiSocket, allowHalfOpen: true });
    trackSocket(socket);
    const result = new Promise((resolve, reject) => {
        socket.on('error', reject);
        let response = '';
        socket.setEncoding('utf8').on('data', (chunk) => { response += chunk; });
        socket.once('end', () => resolve(response));
    });
    result.catch(() => {});
    const headers = 'POST ' + pathname + ' HTTP/1.1\r\n' +
        'Host: localhost\r\nConnection: close\r\n' +
        authorizations.map((value) => 'Authorization: ' + value + '\r\n').join('') +
        'Content-Type: ' + contentType + '\r\n' +
        'Content-Length: ' + (firstBody.length + restBody.length) + '\r\n\r\n';
    socket.write(Buffer.concat([Buffer.from(headers), firstBody]));
    return {
        finish: () => {
            socket.setTimeout(DEADLINE_MS, () => socket.destroy(new Error('Half-closed upload reply timed out')));
            socket.end(restBody);
            return result;
        },
        cancel: () => socket.destroy()
    };
}

function startSocketUpload(apiSocket, archive, filename,
    { pathname = UPLOAD_PATH, authorizations = [] } = {}) {
    const payload = multipartPayload(archive, filename);
    const split = Math.floor(payload.length / 2);
    return startSocketPost(apiSocket, pathname, 'multipart/form-data; boundary=' + boundary,
        payload.subarray(0, split), payload.subarray(split), authorizations);
}

function halfClosedUpload(apiSocket, archive, filename) {
    return startSocketUpload(apiSocket, archive, filename).finish();
}

function enableAuth(port, password) {
    return new Promise((resolve, reject) => {
        const body = JSON.stringify({ password, auth_required: true });
        const req = request(port, '/api/v1/deneb/setup', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'Content-Length': body.length }
        }, (response) => collect(response, resolve, reject));
        req.once('error', reject);
        req.setTimeout(DEADLINE_MS, () => req.destroy(new Error('Auth setup timed out')));
        req.end(body);
    });
}

function digestAuthorization(challenge, password) {
    const match = /WWW-Authenticate: Digest realm="([^"]+)", nonce="([^"]+)"/i.exec(challenge);
    assert.ok(match, 'Digest challenge is present');
    const [, realm, nonce] = match;
    const nc = '00000001';
    const cnonce = 'upload-fixture';
    // Match the running Deneb server's existing Digest hash implementation.
    const md5 = (value) => execFileSync(digestFixture, { input: value, encoding: 'utf8' });
    const ha1 = md5('operator:' + realm + ':' + password);
    const ha2 = md5('POST:' + AUTH_UPLOAD_PATH);
    const response = md5([ha1, nonce, nc, cnonce, 'auth', ha2].join(':'));
    return 'Digest username="operator", realm="' + realm + '", nonce="' + nonce +
        '", uri="' + AUTH_UPLOAD_PATH + '", response="' + response +
        '", qop=auth, nc=' + nc + ', cnonce="' + cnonce + '"';
}

function countDescriptors(pid) {
    return fs.readdirSync('/proc/' + pid + '/fd').length;
}

function extractorState(pid) {
    try {
        return fs.readFileSync('/proc/' + pid + '/status', 'utf8').match(/^State:\s*(.*)$/m)?.[1];
    } catch (error) {
        if (error.code !== 'ENOENT') throw error;
        return 'exited';
    }
}

async function assertResponsive(port, clients, phase) {
    const before = clients.map((client) => client.events);
    const freshEvents = withinDeadline('Four fresh SSE updates exceeded the deadline ' + phase, () =>
        clients.every((client, index) => {
            if (client.error) throw client.error;
            return client.events > before[index];
        }));
    const replies = Promise.all([get(port, STATUS_PATH), get(port, STATUS_PATH)]).then((results) => {
        for (const result of results) {
            assert.equal(result.code, 200);
            assert.equal(JSON.parse(result.data), 'idle');
        }
    });
    const checks = await Promise.allSettled([replies, freshEvents]);
    const failures = checks.filter((check) => check.status === 'rejected').map((check) => check.reason);
    if (failures.length) throw new AggregateError(failures, 'Status stalled ' + phase);
}

async function main() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'du-'));
    const extractionStarted = path.join(root, 'extraction-started');
    const releaseExtraction = path.join(root, 'release-extraction');
    const delayExtraction = path.join(root, 'delay-extraction');
    const failExtraction = path.join(root, 'fail-extraction');
    let releaseFd;
    function armExtraction() {
        fs.rmSync(extractionStarted, { force: true });
        fs.writeFileSync(delayExtraction, '');
    }
    async function blockedExtractor() {
        await withinDeadline('UFP extraction did not start', () =>
            fs.statSync(extractionStarted, { throwIfNoEntry: false })?.size > 0);
        const pid = Number(fs.readFileSync(extractionStarted, 'utf8'));
        assert.ok(pid > 1 && fs.existsSync('/proc/' + pid));
        return pid;
    }
    try {
        const temp = path.join(root, 'tmp');
        const pendingJob = path.join(temp, 'deneb-cluster-print-job.json');
        const spool = path.join(root, 'spool');
        const apiSocket = path.join(root, 'api.sock');
        fs.mkdirSync(temp);
        execFileSync('mkfifo', [releaseExtraction]);
        releaseFd = fs.openSync(releaseExtraction, fs.constants.O_RDWR | fs.constants.O_NONBLOCK);
        const bin = path.join(root, 'bin');
        fs.mkdirSync(bin);
        fs.writeFileSync(path.join(bin, 'unzip'), [
            '#!/bin/sh',
            'if [ -f "$DENEB_FAIL_EXTRACTION" ]; then',
            '    printf ";TIME:12\\n"',
            '    exit 50',
            'fi',
            'if [ "$3" = "3D/model.gcode" ] && [ -f "$DENEB_DELAY_EXTRACTION" ]; then',
            '    printf "%s\\n" "$$" > "$DENEB_EXTRACTION_STARTED"',
            '    IFS= read -r release < "$DENEB_RELEASE_EXTRACTION" || exit 1',
            'fi',
            'exec "$DENEB_REAL_UNZIP" "$@"',
            ''
        ].join('\n'), { mode: 0o700 });
        fs.mkdirSync(path.join(root, '3D'));
        fs.mkdirSync(path.join(root, 'Metadata'));
        fs.writeFileSync(path.join(root, 'web-auth.conf'), 'setup_complete=1\nauth_disabled=1\n');
        // Keep the compressed upload larger than the API's receive chunk.
        const comments = Array.from({ length: 512 }, (_, index) =>
            ';' + createHash('sha256').update(String(index)).digest('hex') + '\n');
        const gcode = ';TIME:12\nG28\n' + comments.join('');
        fs.writeFileSync(path.join(root, '3D', 'model.gcode'), gcode);
        // A 1×1 opaque red RGBA PNG. The accepted upload must publish a red preview.
        fs.writeFileSync(path.join(root, 'Metadata', 'thumbnail.png'), Buffer.from(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==',
            'base64'));
        execFileSync(cmake, ['-E', 'tar', 'cf', 'job.ufp', '--format=zip',
            '3D/model.gcode', 'Metadata/thumbnail.png'], { cwd: root });
        const archive = fs.readFileSync(path.join(root, 'job.ufp'));
        const uploadedBodyExists = () => fs.readdirSync(temp).some((file) =>
            file.startsWith('deneb-upload-') && fs.statSync(path.join(temp, file), { throwIfNoEntry: false })?.size > 0);

        const port = await unusedPort();
        const config = path.join(root, 'lighttpd.conf');
        const isolatedConfig = fs.readFileSync(packagedConfig, 'utf8')
            .replace('"/www/deneb"', JSON.stringify(root))
            .replace('server.port = 80', 'server.port = ' + port)
            .replace('"0.0.0.0"', '"127.0.0.1"')
            .replaceAll('"/var/run/deneb-api.sock"', JSON.stringify(apiSocket));
        fs.writeFileSync(config, isolatedConfig + '\nserver.upload-dirs = (' + JSON.stringify(temp) + ')\n');

        const api = startService(apiBinary, [], root, {
            ...process.env,
            PATH: bin + path.delimiter + process.env.PATH,
            DENEB_REAL_UNZIP: unzip,
            DENEB_FAIL_EXTRACTION: failExtraction,
            DENEB_DELAY_EXTRACTION: delayExtraction,
            DENEB_EXTRACTION_STARTED: extractionStarted,
            DENEB_RELEASE_EXTRACTION: releaseExtraction
        }).process;
        await withinDeadline('deneb-api socket did not start', () => fs.existsSync(apiSocket));
        const proxy = startService(lighttpd, ['-D', '-f', config], root).process;
        await withinDeadline('lighttpd did not start listening', () => children[1].log.includes('server started'));
        assert.equal((await get(port, STATUS_PATH)).code, 200);
        await withinDeadline('REST client socket did not close', () => sockets.size === 0);
        const baseline = [process.pid, api.pid, proxy.pid].map(countDescriptors);

        const clients = Array.from({ length: SSE_CLIENTS }, () => liveStatus(port));
        await withinDeadline('Four SSE streams did not start through lighttpd', () =>
            clients.every((client) => {
                if (client.error) throw client.error;
                return client.events > 0;
            }));
        const active = [api.pid, proxy.pid].map(countDescriptors);
        assert.ok(active[0] >= baseline[1] + SSE_CLIENTS, 'API accepted four SSE clients');

        const interrupted = startUpload(port, archive, 'interrupted.ufp');
        await withinDeadline('Partial multipart data did not reach the API', uploadedBodyExists);
        await assertResponsive(port, clients, 'during incomplete upload');
        const overflow = await startUpload(port, archive, 'over-budget.ufp', {
            declaredLength: 50 * 1024 * 1024
        }).finish();
        assert.equal(overflow.code, 500, overflow.data);
        assert.equal(JSON.parse(overflow.data).message, 'Upload failed');
        interrupted.cancel();

        await assertResponsive(port, clients, 'after cancellation');
        await withinDeadline('Cancellation did not release proxy/API upload resources', () =>
            fs.readdirSync(temp).length === 0 &&
            countDescriptors(api.pid) === active[0] && countDescriptors(proxy.pid) === active[1] &&
            sockets.size === SSE_CLIENTS);
        assert.equal(fs.existsSync(pendingJob), false);
        assert.equal(fs.existsSync(spool), false, 'An interrupted upload reached print storage');

        armExtraction();
        const cancelled = startUpload(port, archive, 'cancelled-finalization.ufp');
        void cancelled.finish();
        const cancelledPid = await blockedExtractor();
        const [receiveSpool, ...otherTempFiles] = fs.readdirSync(temp);
        assert.deepEqual(otherTempFiles, []);
        assert.match(receiveSpool, /^deneb-upload-/);
        assert.deepEqual(fs.readFileSync(path.join(temp, receiveSpool)), archive);
        const [staged, ...otherSpoolEntries] = fs.readdirSync(spool);
        assert.deepEqual(otherSpoolEntries, []);
        assert.match(staged, /^\.upload-/);
        assert.deepEqual(fs.readdirSync(path.join(spool, staged)), ['job']);
        await assertResponsive(port, clients, 'while a later-cancelled UFP is extracted');
        // A TCP FIN after the full body is also a valid write-side half-close.
        // Send a reset so the proxy recognizes that the client discarded its reply.
        cancelled.reset();
        try {
            await withinDeadline('Finalization cancellation did not release its child and temp files', () =>
                !fs.existsSync('/proc/' + cancelledPid) && fs.readdirSync(temp).length === 0 &&
                countDescriptors(api.pid) === active[0] && countDescriptors(proxy.pid) === active[1]);
        } catch (error) {
            console.error('Upload finalization cancellation', {
                extractorPid: cancelledPid,
                extractorState: extractorState(cancelledPid),
                tempFiles: fs.readdirSync(temp),
                apiDescriptors: { actual: countDescriptors(api.pid), expected: active[0] },
                proxyDescriptors: { actual: countDescriptors(proxy.pid), expected: active[1] },
                clientSockets: { actual: sockets.size, expected: SSE_CLIENTS }
            });
            throw error;
        }
        assert.equal(fs.existsSync(pendingJob), false);
        assert.deepEqual(fs.readdirSync(spool), []);

        fs.writeFileSync(failExtraction, '');
        const failedExtraction = await startUpload(port, archive, 'failed-extraction.ufp').finish();
        fs.rmSync(failExtraction);
        assert.equal(failedExtraction.code, 400, failedExtraction.data);
        assert.equal(JSON.parse(failedExtraction.data).message, 'Failed to extract UFP model.gcode');
        assert.deepEqual(fs.readdirSync(spool), []);
        assert.deepEqual(fs.readdirSync(temp), []);

        const complete = startUpload(port, archive, 'completed.ufp');
        await withinDeadline('Second partial upload did not reach the API', uploadedBodyExists);
        await assertResponsive(port, clients, 'during the subsequent upload');
        armExtraction();
        const completing = complete.finish();
        await blockedExtractor();
        await assertResponsive(port, clients, 'while UFP extraction is blocked');
        fs.writeSync(releaseFd, 'continue\n');
        const result = await completing;
        fs.rmSync(delayExtraction);
        assert.equal(result.code, 201, result.data);
        const response = JSON.parse(result.data);
        assert.equal(response.message, 'Print job accepted');
        assert.equal(response.name, 'completed.gcode');
        assert.equal(fs.readFileSync(path.join(spool, 'completed.gcode'), 'utf8'), gcode);
        assert.deepEqual(fs.readFileSync(path.join(root, 'deneb-active-thumb.rgb565')),
            Buffer.alloc(116 * 116 * 2, Buffer.from([0x00, 0xf8])));
        const pending = JSON.parse(fs.readFileSync(pendingJob, 'utf8'));
        assert.equal(pending.length, 1);
        assert.equal(pending[0].path, 'spool/completed.gcode');
        assert.deepEqual(fs.readdirSync(temp), ['deneb-cluster-print-job.json']);

        // The proxy still translates chunked requests for the Content-Length API.
        const chunkedRetry = startUpload(port, archive, 'completed.ufp', { chunked: true });
        await assertResponsive(port, clients, 'during a chunked multipart upload');
        const duplicate = await chunkedRetry.finish();
        assert.equal(duplicate.code, 200, duplicate.data);
        assert.equal(JSON.parse(duplicate.data).message, 'Print job already queued');

        // Keep the pending job while making the spool path unusable for staging.
        const savedSpool = path.join(root, 'saved-spool');
        fs.renameSync(spool, savedSpool);
        try {
            fs.writeFileSync(spool, 'not a directory');
            const failedStage = await startUpload(port, archive, 'completed.ufp').finish();
            assert.equal(failedStage.code, 500, failedStage.data);
            assert.equal(JSON.parse(failedStage.data).message, 'Failed to save file');
            assert.deepEqual(JSON.parse(fs.readFileSync(pendingJob, 'utf8')), pending);
            assert.deepEqual(fs.readdirSync(temp), ['deneb-cluster-print-job.json']);
        } finally {
            fs.rmSync(spool, { force: true });
            fs.renameSync(savedSpool, spool);
        }
        assert.equal(fs.readFileSync(path.join(spool, 'completed.gcode'), 'utf8'), gcode);

        const halfClosed = await halfClosedUpload(apiSocket, archive, 'completed.ufp');
        assert.match(halfClosed, /^HTTP\/1\.1 200 OK\r\n/);
        assert.match(halfClosed, /Print job already queued/);

        const password = randomBytes(16).toString('hex');
        const setup = await enableAuth(port, password);
        assert.equal(setup.code, 200, setup.data);
        const credentials = JSON.parse(setup.data);
        assert.equal(credentials.auth_enabled, true);

        // A boundary parameter selects spooling. Login still needs the JSON body prefix.
        const login = startSocketPost(apiSocket, '/api/v1/deneb/auth',
            'application/json; boundary=' + boundary,
            Buffer.from(JSON.stringify({ password })), Buffer.from('\r\n'));
        await withinDeadline('JSON login did not enter the spooled POST path', uploadedBodyExists);
        await assertResponsive(port, clients, 'during the spooled JSON login');
        const loginReply = await login.finish();
        assert.match(loginReply, /^HTTP\/1\.1 200 OK\r\n/);
        const { token } = JSON.parse(loginReply.slice(loginReply.indexOf('\r\n\r\n') + 4));
        assert.ok(token);
        const bearer = 'Bearer ' + token;
        const authorizedUpload = (authorizations) => startSocketUpload(
            apiSocket, archive, 'completed.ufp', { pathname: AUTH_UPLOAD_PATH, authorizations });

        // Keep all eight API slots occupied while REST and SSE use the proxy.
        const uploads = [
            authorizedUpload(['Basic unused', bearer]),
            authorizedUpload(['Bearer invalid', bearer]),
            ...Array.from({ length: 6 }, () => authorizedUpload([bearer]))
        ];
        await withinDeadline('Eight partial uploads did not reach the API', () =>
            fs.readdirSync(temp).filter((name) => name.startsWith('deneb-upload-')).length === 8);
        await assertResponsive(port, clients, 'with eight pending multipart uploads');
        const accepted = await uploads[0].finish();
        assert.match(accepted, /^HTTP\/1\.1 200 OK\r\n/);
        assert.match(accepted, /Print job already queued/);
        const rejected = await uploads[1].finish();
        assert.match(rejected, /^HTTP\/1\.1 401 Unauthorized\r\n/);
        for (const upload of uploads.slice(2)) upload.cancel();
        await withinDeadline('Eight-slot test retained upload files or descriptors', () =>
            countDescriptors(api.pid) === active[0] &&
            fs.readdirSync(temp).join() === 'deneb-cluster-print-job.json');

        const digest = digestAuthorization(rejected, password);
        const digestAccepted = await authorizedUpload([digest]).finish();
        assert.match(digestAccepted, /^HTTP\/1\.1 200 OK\r\n/);
        assert.match(digestAccepted, /Print job already queued/);
        const challenge = await authorizedUpload([]).finish();
        assert.match(challenge, /^HTTP\/1\.1 401 Unauthorized\r\n/);
        const badDigest = digestAuthorization(challenge, randomBytes(16).toString('hex'));
        const digestRejected = await authorizedUpload([badDigest]).finish();
        assert.match(digestRejected, /^HTTP\/1\.1 401 Unauthorized\r\n/);
        assert.deepEqual(JSON.parse(fs.readFileSync(pendingJob, 'utf8')), pending);

        // Keep response I/O tests separate from the upload cancellation sequence.
        const locales = path.join(root, 'locales');
        fs.mkdirSync(locales);
        const localePath = path.join(locales, 'test.json');
        for (const length of [65023, 65024, 65025, 65536]) {
            const expected = localeBody(length);
            fs.writeFileSync(localePath, expected);
            const response = await get(port, LOCALE_PATH);
            assert.equal(response.code, 200);
            assert.equal(response.headers['content-length'], String(length), 'locale Content-Length');
            assert.ok(Buffer.from(response.data).equals(Buffer.from(expected)), 'byte-exact locale response');
            assert.deepEqual(JSON.parse(response.data), JSON.parse(expected));
        }
        await withinDeadline('Locale proxy sockets did not close', () =>
            countDescriptors(api.pid) === active[0] && sockets.size === SSE_CLIENTS);

        const fullLocale = Buffer.from(localeBody(65536));
        const paused = pausedLocale(apiSocket);
        await paused.started;
        await assertResponsive(port, clients, 'while a locale reader paused');
        await withinDeadline('Paused reader did not retain the pending API response', () =>
            countDescriptors(api.pid) === active[0] + 1 && sockets.size === SSE_CLIENTS + 1);
        const received = await paused.finish();
        const headerEnd = received.indexOf('\r\n\r\n');
        assert.ok(headerEnd > 0);
        const headers = received.subarray(0, headerEnd + 2).toString();
        assert.match(headers, /^HTTP\/1\.1 200 OK\r\n/);
        assert.match(headers, /\r\nContent-Length: 65536\r\n/);
        const body = received.subarray(headerEnd + 4);
        assert.equal(body.length, fullLocale.length, 'paused reader received the declared body length');
        assert.ok(body.equals(fullLocale), 'byte-exact locale response after resuming');

        const abandoned = pausedLocale(apiSocket);
        await abandoned.started;
        abandoned.disconnect();
        await withinDeadline('Paused responses did not release their sockets', () =>
            countDescriptors(api.pid) === active[0] && sockets.size === SSE_CLIENTS);

        for (const client of clients) client.close();
        await withinDeadline('Client, API, or lighttpd descriptors did not return to idle', () =>
            sockets.size === 0 &&
            [process.pid, api.pid, proxy.pid].every((pid, index) => countDescriptors(pid) === baseline[index]));

        armExtraction();
        const terminating = startUpload(port, archive, 'shutdown-finalization.ufp');
        void terminating.finish();
        const terminatingPid = await blockedExtractor();
        api.kill('SIGTERM');
        await withinDeadline('API shutdown left an active extractor', () =>
            children[0].stopped && !fs.existsSync('/proc/' + terminatingPid));
        assert.equal(api.exitCode, 0);
        assert.deepEqual(fs.readdirSync(temp), ['deneb-cluster-print-job.json']);
        assert.deepEqual(fs.readdirSync(spool), ['completed.gcode']);

        fs.writeFileSync(path.join(root, 'web-auth.conf'), 'setup_complete=1\nauth_disabled=0\n');
        startService(apiBinary, [], root);
        await withinDeadline('Authenticated deneb-api socket did not start', () => fs.existsSync(apiSocket));
        const unauthorized = await get(port, '/api/v1/auth/verify');
        assert.equal(unauthorized.code, 401);
        assert.match(unauthorized.headers['www-authenticate'], /^Digest realm="[^"]+", nonce="[a-f0-9]+", qop="auth"$/);
        assert.deepEqual(JSON.parse(unauthorized.data), { message: 'Unauthorized' });
    } catch (error) {
        for (const child of children) process.stderr.write(child.log);
        throw error;
    } finally {
        if (releaseFd !== undefined) {
            fs.writeSync(releaseFd, 'continue\n');
            fs.closeSync(releaseFd);
        }
        for (const socket of sockets) socket.destroy();
        await Promise.all(children.map(stopService));
        fs.rmSync(root, { recursive: true, force: true });
    }
}

// Preview decoding has separate fixtures so the proxy cancellation test keeps
// its original request sizes, connection sequence, and descriptor baseline.
async function testThumbnailUploads() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'du-'));
    const temp = path.join(root, 'tmp');
    const apiSocket = path.join(root, 'api.sock');
    const thumbnailPath = path.join(root, 'deneb-active-thumb.rgb565');
    const pendingJob = path.join(temp, 'deneb-cluster-print-job.json');
    let api;
    try {
        fs.mkdirSync(temp);
        fs.mkdirSync(path.join(root, '3D'));
        fs.mkdirSync(path.join(root, 'Metadata'));
        const gcode = ';TIME:12\nG28\n';
        fs.writeFileSync(path.join(root, '3D/model.gcode'), gcode);
        fs.writeFileSync(path.join(root, 'web-auth.conf'), 'setup_complete=1\nauth_disabled=1\n');

        function makeArchive(...images) {
            const thumbnails = images.map((image, index) => {
                const name = 'Metadata/thumbnail-' + index + '.png';
                fs.writeFileSync(path.join(root, name), image);
                return name;
            });
            execFileSync(cmake, ['-E', 'tar', 'cf', 'job.ufp', '--format=zip',
                '3D/model.gcode', ...thumbnails], { cwd: root });
            return fs.readFileSync(path.join(root, 'job.ufp'));
        }

        api = startService(apiBinary, [], root, {
            ...process.env, PATH: path.dirname(unzip) + path.delimiter + process.env.PATH
        });
        await withinDeadline('deneb-api preview socket did not start', () => fs.existsSync(apiSocket));

        const overBudget = upFilteredRgbaPng(1024, 1025, [0xff000080]);
        const skipped = await halfClosedUpload(apiSocket, makeArchive(overBudget), 'large-preview.ufp');
        assert.match(skipped, /^HTTP\/1\.1 201 Created\r\n/);
        assert.equal(fs.existsSync(thumbnailPath), false);
        assert.deepEqual(fs.readdirSync(temp), ['deneb-cluster-print-job.json']);
        assert.equal(fs.readFileSync(path.join(root, 'spool/large-preview.gcode'), 'utf8'), gcode);
        fs.rmSync(pendingJob);

        // The 2048 x 512 image ranks below the ineligible 1024 x 1025 image.
        const eligible = upFilteredRgbaPng(2048, 512, [0xff000080, 0x00ff0080, 0x0000ff80, 0xffff0080]);
        const mixed = await halfClosedUpload(apiSocket, makeArchive(overBudget, eligible), 'mixed-preview.ufp');
        assert.match(mixed, /^HTTP\/1\.1 201 Created\r\n/);
        assert.equal(fs.readFileSync(path.join(root, 'spool/mixed-preview.gcode'), 'utf8'), gcode);
        const expectedPreview = Buffer.alloc(116 * 116 * 2);
        const rowColors = [0x8021, 0x0421, 0x0031, 0x8421];
        for (let pixel = 0; pixel < 116 * 116; pixel++) {
            const y = Math.floor(pixel / 116);
            // The 4:1 source produces 29 image rows centered in the output.
            const color = y >= 43 && y < 72
                ? rowColors[Math.floor((y - 43) * 512 / 29) % rowColors.length] : 0x0843;
            expectedPreview.writeUInt16LE(color, pixel * 2);
        }
        assert.deepEqual(fs.readFileSync(thumbnailPath), expectedPreview);
        assert.deepEqual(fs.readdirSync(temp), ['deneb-cluster-print-job.json']);
    } catch (error) {
        if (api) process.stderr.write(api.log);
        throw error;
    } finally {
        for (const socket of sockets) socket.destroy();
        if (api) await stopService(api);
        fs.rmSync(root, { recursive: true, force: true });
    }
}

async function testMultipartFileRange() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'du-'));
    const temp = path.join(root, 'tmp');
    const apiSocket = path.join(root, 'api.sock');
    const contentType = 'multipart/form-data; boundary=' + boundary;
    let api;
    try {
        fs.mkdirSync(temp);
        fs.writeFileSync(path.join(root, 'web-auth.conf'), 'setup_complete=1\nauth_disabled=1\n');
        api = startService(apiBinary, [], root, process.env);
        await withinDeadline('deneb-api multipart socket did not start', () => fs.existsSync(apiSocket));

        const field = (name, value, padding = '') => '--' + boundary + padding +
            '\r\nContent-Disposition: form-data; name="' + name + '"\r\n\r\n' + value + '\r\n';
        const headers = Buffer.from(field('cloud_job_id', 'cloud-fixture', ' \t') + '--' + boundary +
            '\r\nContent-Disposition: form-data; name="file"; filename="range.gcode"\r\n\r\n');
        const text = Buffer.from(';TIME:12\n; file bytes\0\r\n--' + boundary + '-text\r\n;');
        // The real delimiter starts at the end of the first parser buffer.
        const gcode = Buffer.concat([text, Buffer.alloc(8190 - headers.length - text.length, 0x61)]);
        const uuid = '12345678-1234-4567-89ab-1234567890ab';
        const suffix = Buffer.from('\r\n' + field('job_instance_uuid', uuid, '\t' + ' '.repeat(8192)) +
            field('job_instance_uuid', 'aaaaaaaa-bbbb-4ccc-adef-1234567890ab') + field('owner', 'Cura') +
            '--' + boundary + '-- \t');

        const truncated = await startSocketPost(apiSocket, UPLOAD_PATH, contentType, headers, gcode).finish();
        assert.match(truncated, /^HTTP\/1\.1 400 Bad Request\r\n/);
        assert.deepEqual(fs.readdirSync(temp), []);

        const uploaded = await startSocketPost(apiSocket, UPLOAD_PATH, contentType,
            Buffer.concat([headers, gcode]), suffix).finish();
        assert.match(uploaded, /^HTTP\/1\.1 201 Created\r\n/);
        assert.deepEqual(fs.readFileSync(path.join(root, 'spool/range.gcode')), gcode);
        const pending = JSON.parse(fs.readFileSync(path.join(temp, 'deneb-cluster-print-job.json'), 'utf8'));
        assert.equal(pending[0].uuid, uuid);
        assert.equal(pending[0].cloud_job_id, 'cloud-fixture');
        assert.equal(pending[0].owner, 'Cura');
    } catch (error) {
        if (api) process.stderr.write(api.log);
        throw error;
    } finally {
        for (const socket of sockets) socket.destroy();
        if (api) await stopService(api);
        fs.rmSync(root, { recursive: true, force: true });
    }
}

main().then(testThumbnailUploads).then(testMultipartFileRange).catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
