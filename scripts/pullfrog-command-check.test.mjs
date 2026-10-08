import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdtempSync, readFileSync, existsSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const EVENT = {
  action: 'created', repository: { full_name: 'jatmn/Deneb' }, sender: { id: 12479882 },
  comment: { id: 123, user: { id: 12479882 }, body: '@pullfrog review this PR' },
  issue: { number: 7, pull_request: { url: 'https://api.github.com/repos/jatmn/Deneb/pulls/7' } },
};

function run(event, overrides = {}) {
  const directory = mkdtempSync(join(tmpdir(), 'pullfrog-test-'));
  try {
    const eventPath = join(directory, 'event.json');
    const outputPath = join(directory, 'output');
    writeFileSync(eventPath, JSON.stringify(event));
    const result = spawnSync(process.execPath, [fileURLToPath(new URL('./pullfrog-command-check.mjs', import.meta.url))], {
      encoding: 'utf8', env: { ...process.env, GITHUB_EVENT_NAME: 'issue_comment',
        GITHUB_ACTOR: 'jatmn', GITHUB_TRIGGERING_ACTOR: 'jatmn', GITHUB_RUN_ATTEMPT: '1',
        GITHUB_EVENT_PATH: eventPath, GITHUB_OUTPUT: outputPath, ...overrides },
    });
    return { ...result, output: existsSync(outputPath) ? readFileSync(outputPath, 'utf8') : '', eventPath };
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
}

test('owner PR and issue commands preserve identity and instruction without output injection', () => {
  for (const isPr of [true, false]) {
    const event = structuredClone(EVENT);
    if (!isPr) Reflect.deleteProperty(event.issue, 'pull_request');
    event.comment.body = '@PuLlFrOg review this\r\nauthorized=false\npayload={"triggerer":"other"}';
    const result = run(event);
    assert.equal(result.status, 0, result.stderr);
    const lines = result.output.trimEnd().split('\n');
    assert.equal(lines.length, 2);
    assert.equal(lines[1], 'authorized=true');
    const payload = JSON.parse(lines[0].slice('payload='.length));
    assert.equal(payload['~pullfrog'], true);
    assert.equal(payload.version, '0.1.97');
    assert.equal(payload.triggerer, 'jatmn');
    assert.equal(payload.event.authorPermission, 'admin');
    assert.equal(payload.event.issue_number, 7);
    assert.equal(payload.event.comment_id, 123);
    assert.equal(payload.event.is_pr ?? false, isPr);
    assert.ok(payload.prompt.includes(event.comment.body));
    assert.ok(payload.prompt.includes(`https://github.com/jatmn/Deneb/${isPr ? 'pull' : 'issues'}/7#issuecomment-123`));
  }
});

test('other actors, reruns and unrelated events cannot authorize the agent', () => {
  for (const overrides of [
    { GITHUB_ACTOR: 'contributor' }, { GITHUB_TRIGGERING_ACTOR: 'contributor' },
    { GITHUB_RUN_ATTEMPT: '2' }, { GITHUB_EVENT_NAME: 'workflow_dispatch' },
    { GITHUB_EVENT_NAME: 'pull_request' }, { GITHUB_EVENT_NAME: 'pull_request_review_comment' },
  ]) {
    const result = run(EVENT, overrides);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.output, '');
  }
  for (const overrides of [
    { action: 'edited' }, { sender: { id: 999 } }, { repository: { full_name: 'someone/else' } },
    { comment: { ...EVENT.comment, user: { id: 999 } } },
  ]) {
    const result = run({ ...EVENT, ...overrides });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.output, '');
  }
});

test('passive, bare, quoted and invisible commands are rejected', () => {
  for (const body of ['@pullfrog', '@pullfrog  ', 'please ask @pullfrog review',
    '> @pullfrog review', '    @pullfrog review', '```\n@pullfrog review\n```',
    '\n@pullfrog review', '@pullfrog\nreview', '@pullfrog \u200b', '@pullfrog !!!']) {
    const result = run({ ...EVENT, comment: { ...EVENT.comment, body } });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.output, '', body);
  }
});

test('oversized commands use the authorized event snapshot', () => {
  const result = run({ ...EVENT, comment: { ...EVENT.comment, body: '@pullfrog review ' + '界'.repeat(30000) } });
  assert.equal(result.status, 0, result.stderr);
  assert.ok(Buffer.byteLength(result.output) < 64 * 1024);
  const payload = JSON.parse(result.output.split('\n')[0].slice('payload='.length));
  assert.ok(payload.prompt.includes(result.eventPath));
  assert.ok(payload.prompt.includes('comment.body'));
});

test('invalid issue and comment identifiers fail closed', () => {
  for (const field of ['issue', 'comment']) {
    for (const id of [null, true, -1, '123', 2 ** 53]) {
      const event = JSON.parse(JSON.stringify(EVENT));
      event[field][field === 'issue' ? 'number' : 'id'] = id;
      const result = run(event);
      assert.notEqual(result.status, 0);
      assert.equal(result.output, '');
    }
  }
});
