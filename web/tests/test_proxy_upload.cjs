// SPDX-License-Identifier: MPL-2.0
const assert = require('node:assert/strict');
const { spawn, execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');

const [apiBinary, lighttpd, packagedConfig, cmake] = process.argv.slice(2);
const DEADLINE_MS = 2000;
const SSE_CLIENTS = 4;
const STATUS_PATH = '/api/v1/printer/status';
const UPLOAD_PATH = '/cluster-api/v1/print_jobs';
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

function startService(binary, args, cwd) {
    const process = spawn(binary, args, { cwd, stdio: ['ignore', 'ignore', 'pipe'] });
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
    req.on('socket', (socket) => {
        sockets.add(socket);
        socket.once('close', () => sockets.delete(socket));
    });
    return req;
}

function collect(response, resolve, reject) {
    const chunks = [];
    response.on('data', (chunk) => chunks.push(chunk));
    response.once('error', reject);
    response.once('end', () => resolve({
        code: response.statusCode, data: Buffer.concat(chunks).toString()
    }));
}

function status(port) {
    return new Promise((resolve, reject) => {
        const req = request(port, STATUS_PATH, {}, (response) => collect(response, resolve, reject));
        req.once('error', reject);
        req.setTimeout(DEADLINE_MS, () => req.destroy(new Error('REST status deadline exceeded')));
        req.end();
    });
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

function startUpload(port, body, filename, { chunked = false, declaredLength } = {}) {
    const payload = Buffer.concat([
        Buffer.from('--' + boundary + '\r\nContent-Disposition: form-data; name="owner"\r\n\r\nCura\r\n' +
            '--' + boundary + '\r\nContent-Disposition: form-data; name="file"; filename="' + filename +
            '"\r\nContent-Type: application/octet-stream\r\n\r\n'),
        body,
        Buffer.from('\r\n--' + boundary + '--\r\n')
    ]);
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
        cancel: () => req.destroy()
    };
}

function countDescriptors(pid) {
    return fs.readdirSync('/proc/' + pid + '/fd').length;
}

async function assertResponsive(port, clients, phase) {
    const before = clients.map((client) => client.events);
    const freshEvents = withinDeadline('Four fresh SSE updates exceeded the deadline ' + phase, () =>
        clients.every((client, index) => {
            if (client.error) throw client.error;
            return client.events > before[index];
        }));
    const replies = Promise.all([status(port), status(port)]).then((results) => {
        for (const result of results) {
            assert.equal(result.code, 200);
            assert.equal(JSON.parse(result.data), 'idle');
        }
    });
    await Promise.all([replies, freshEvents]);
}

async function main() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'du-'));
    try {
        const temp = path.join(root, 'tmp');
        const pendingJob = path.join(temp, 'deneb-cluster-print-job.json');
        const spool = path.join(root, 'spool');
        const apiSocket = path.join(root, 'api.sock');
        fs.mkdirSync(temp);
        fs.mkdirSync(path.join(root, '3D'));
        fs.writeFileSync(path.join(root, 'web-auth.conf'), 'setup_complete=1\nauth_disabled=1\n');
        // Keep the compressed upload larger than the API's receive chunk.
        const comments = Array.from({ length: 512 }, (_, index) =>
            ';' + createHash('sha256').update(String(index)).digest('hex') + '\n');
        const gcode = ';TIME:12\nG28\n' + comments.join('');
        fs.writeFileSync(path.join(root, '3D', 'model.gcode'), gcode);
        execFileSync(cmake, ['-E', 'tar', 'cf', 'job.ufp', '--format=zip', '3D/model.gcode'], { cwd: root });
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

        const api = startService(apiBinary, [], root).process;
        await withinDeadline('deneb-api socket did not start', () => fs.existsSync(apiSocket));
        const proxy = startService(lighttpd, ['-D', '-f', config], root).process;
        await withinDeadline('lighttpd did not start listening', () => children[1].log.includes('server started'));
        assert.equal((await status(port)).code, 200);
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

        const complete = startUpload(port, archive, 'completed.ufp');
        await withinDeadline('Second partial upload did not reach the API', uploadedBodyExists);
        await assertResponsive(port, clients, 'during the subsequent upload');
        const result = await complete.finish();
        assert.equal(result.code, 201, result.data);
        const response = JSON.parse(result.data);
        assert.equal(response.message, 'Print job accepted');
        assert.equal(response.name, 'completed.gcode');
        assert.equal(fs.readFileSync(path.join(spool, 'completed.gcode'), 'utf8'), gcode);
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

        for (const client of clients) client.close();
        await withinDeadline('Client, API, or lighttpd descriptors did not return to idle', () =>
            sockets.size === 0 &&
            [process.pid, api.pid, proxy.pid].every((pid, index) => countDescriptors(pid) === baseline[index]));
    } catch (error) {
        for (const child of children) process.stderr.write(child.log);
        throw error;
    } finally {
        for (const socket of sockets) socket.destroy();
        await Promise.all(children.map(stopService));
        fs.rmSync(root, { recursive: true, force: true });
    }
}

main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
