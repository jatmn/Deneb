import { isAuthorizedReview } from './pullfrog-command-check.mjs';
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
    assert.equal(payload.version, '0.1.122');
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

const reviewOwner = { id: 12479882, login: 'jatmn' };
const api = {
  apiUrl: 'https://api.github.test',
  repository: 'jatmn/JiStats',
  token: 'fixture-token',
};

function review(trigger = 'pull_request_opened', fields = {}) {
  return {
    '~pullfrog': true,
    triggerer: 'another-pusher',
    event: { trigger, is_pr: true, issue_number: 7, ...fields },
  };
}

test('accepts opening a PR, marking it ready, and pushing to it, including drafts', async (t) => {
  const cases = [
    ['pull_request_opened', false],
    ['pull_request_opened', true],
    ['pull_request_ready_for_review', false],
    ['pull_request_synchronize', false],
    ['pull_request_synchronize', true],
  ];
  for (const [trigger, draft] of cases) {
    const fetch = t.mock.method(globalThis, 'fetch', async (url, { headers, signal }) => {
      assert.equal(url, 'https://api.github.test/repos/jatmn/JiStats/pulls/7');
      assert.equal(headers.Authorization, 'Bearer fixture-token');
      assert.ok(signal instanceof AbortSignal);
      return Response.json({ user: reviewOwner, draft });
    });
    try {
      assert.equal(await isAuthorizedReview(review(trigger), api), true, trigger);
      assert.equal(fetch.mock.callCount(), 1);
    } finally {
      fetch.mock.restore();
    }
  }
});

test('rejects invalid and unrelated dispatches before calling GitHub', async (t) => {
  const fetch = t.mock.method(globalThis, 'fetch');
  for (const payload of [
    null, {}, { event: review().event },
    review('issue_comment_created'),
    review(undefined, { is_pr: false }),
    review(undefined, { issue_number: '7' }),
    review(undefined, { issue_number: 0 }),
  ]) {
    assert.equal(await isAuthorizedReview(payload, api), false);
  }
  assert.equal(fetch.mock.callCount(), 0);
});

test('requires both author identifiers, even when the owner pushes', async (t) => {
  const authors = [null, { ...reviewOwner, id: 1 }, { ...reviewOwner, login: 'another-author' }];
  t.mock.method(globalThis, 'fetch', async () => Response.json({ user: authors.shift() }));
  const payload = { ...review('pull_request_synchronize'), triggerer: reviewOwner.login };
  for (let i = 0; i < 3; i++) {
    assert.equal(await isAuthorizedReview(payload, api), false);
  }
});

test('retries a timed-out response body and an upstream failure', async (t) => {
  const timeout = t.mock.method(AbortSignal, 'timeout', () => AbortSignal.abort(new DOMException('timed out', 'TimeoutError')));
  const responses = [
    async (signal) => ({ ok: true, json: async () => signal.throwIfAborted() }),
    async () => new Response(null, { status: 503 }),
    async () => Response.json({ user: reviewOwner }),
  ];
  const fetch = t.mock.method(globalThis, 'fetch', async (_url, { signal }) => responses.shift()(signal));
  assert.equal(await isAuthorizedReview(review(), api), true);
  assert.equal(fetch.mock.callCount(), 3);
  assert.deepEqual(timeout.mock.calls.map((call) => call.arguments), [[10_000], [10_000], [10_000]]);
});

test('fails after three transient errors', async (t) => {
  const fetch = t.mock.method(globalThis, 'fetch', async () => {
    throw new TypeError('fetch failed');
  });
  await assert.rejects(isAuthorizedReview(review(), api), /Could not verify PR #7 author/);
  assert.equal(fetch.mock.callCount(), 3);
});

test('fails immediately for access errors and rate limits', async (t) => {
  const errors = [
    { status: 403 },
    { status: 429 },
    { status: 503, headers: { 'Retry-After': '60' } },
  ];
  const fetch = t.mock.method(globalThis, 'fetch', async () => new Response(null, errors.shift()));
  for (const [index, status] of [403, 429, 503].entries()) {
    await assert.rejects(isAuthorizedReview(review(), api), (error) => {
      assert.equal(error.cause.message, `GitHub HTTP ${status}`);
      return true;
    });
    assert.equal(fetch.mock.callCount(), index + 1);
  }
});

test('requires verified author on the automatic review step', () => {
  const workflow = readFileSync(new URL('../.github/workflows/pullfrog.yml', import.meta.url), 'utf8');
  const [, automatic] = workflow.split('\n  auto-review:\n');
  assert.ok(automatic, 'Expected the automatic review job');
  const [, step] = automatic.split('\n      - name: Review the pull request\n');
  assert.ok(step, 'Expected the automatic Pullfrog review step');
  assert.equal(step.split('\n')[0].trim(), "if: steps.authorize.outputs.authorized == 'true'");
  assert.ok(step.includes('prompt: ${{ inputs.prompt }}'));
});
