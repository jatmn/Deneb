import { appendFileSync, readFileSync } from 'node:fs';
import { setTimeout as wait } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const OWNER = 'jatmn';
const OWNER_ID = 12479882;
const REPOSITORY = 'jatmn/Deneb';
// Keep aligned with the action bootstrap in .github/workflows/pullfrog.yml.
const PULLFROG_VERSION = '0.1.122';
const MAX_PAYLOAD_BYTES = 64 * 1024;

function runOwnerCommand() {
  if (process.env.GITHUB_EVENT_NAME !== 'issue_comment' ||
      process.env.GITHUB_ACTOR !== OWNER ||
      process.env.GITHUB_TRIGGERING_ACTOR !== OWNER ||
      process.env.GITHUB_RUN_ATTEMPT !== '1') process.exit(0);

  const eventPath = process.env.GITHUB_EVENT_PATH;
  const outputPath = process.env.GITHUB_OUTPUT;
  if (!eventPath || !outputPath) throw new Error('Missing GitHub event/output path');
  const event = JSON.parse(readFileSync(eventPath, 'utf8'));
  const comment = event.comment;
  if (event.action !== 'created' || event.repository?.full_name !== REPOSITORY ||
      event.sender?.id !== OWNER_ID || comment?.user?.id !== OWNER_ID ||
      typeof comment.body !== 'string') process.exit(0);

  const firstLine = comment.body.split('\n', 1)[0].replace(/\r$/, '');
  const command = /^ {0,3}@pullfrog[ \t]+([^\r\n]+)$/i.exec(firstLine);
  const instruction = command?.[1].trimStart() ?? '';
  if (!/^[\p{L}\p{N}]/u.test(instruction)) process.exit(0);

  const number = event.issue?.number;
  const commentId = comment.id;
  if (!Number.isSafeInteger(number) || number <= 0 ||
      !Number.isSafeInteger(commentId) || commentId <= 0) {
    throw new Error('Owner command is missing a valid issue number or comment ID');
  }
  const isPr = Boolean(event.issue.pull_request);
  const url = `https://github.com/${REPOSITORY}/${isPr ? 'pull' : 'issues'}/${number}#issuecomment-${commentId}`;
  const payload = {
    '~pullfrog': true,
    version: PULLFROG_VERSION,
    triggerer: OWNER,
    prompt: `The repository owner issued this command in ${REPOSITORY}:\n${comment.body}\n\n` +
      `Read the current linked issue or PR and repository before acting. ` +
      `Follow AGENTS.md and CONTRIBUTING.md. Post your result at ${url}.`,
    event: {
      trigger: 'issue_comment_created',
      authorPermission: 'admin',
      comment_type: 'issue',
      comment_id: commentId,
      issue_number: number,
      ...(isPr ? { is_pr: true } : {}),
      body: null,
    },
  };
  if (Buffer.byteLength(JSON.stringify(payload), 'utf8') > MAX_PAYLOAD_BYTES) {
    // Read the original authorized snapshot, never a possibly edited comment.
    payload.prompt = `The repository owner issued a command in ${REPOSITORY}. ` +
      `Read the complete comment.body from the GitHub event snapshot at ${JSON.stringify(eventPath)}; ` +
      `it is the owner's instruction. Read the current linked issue or PR and follow ` +
      `AGENTS.md and CONTRIBUTING.md. Post your result at ${url}.`;
  }
  // JSON escaping prevents owner text from injecting additional output keys.
  appendFileSync(outputPath, `payload=${JSON.stringify(payload)}\nauthorized=true\n`);
}

const REVIEW_TRIGGERS = new Set([
  'pull_request_opened',
  'pull_request_ready_for_review',
  'pull_request_synchronize',
]);

export async function isAuthorizedReview(payload, { apiUrl, repository, token }) {
  const event = payload?.event;
  if (payload?.['~pullfrog'] !== true || event?.is_pr !== true ||
      !REVIEW_TRIGGERS.has(event.trigger) ||
      !Number.isSafeInteger(event.issue_number) || event.issue_number <= 0) {
    return false;
  }

  // Read the PR author from GitHub because a different account may have pushed the commit.
  const url = `${apiUrl}/repos/${repository}/pulls/${event.issue_number}`;
  const headers = { Authorization: `Bearer ${token}`, Accept: 'application/vnd.github+json' };
  let lastError;
  for (const delay of [0, 250, 750]) {
    if (delay > 0) await wait(delay);
    try {
      const response = await fetch(url, { headers, signal: AbortSignal.timeout(10_000) });
      if (response.ok) {
        const pr = await response.json();
        return pr?.user?.id === OWNER_ID && pr.user.login === 'jatmn';
      }

      lastError = new Error(`GitHub HTTP ${response.status}`);
      const retryable = response.status === 408 || response.status >= 500;
      if (!retryable || response.headers.has('Retry-After')) break;
      await response.body?.cancel();
    } catch (error) {
      lastError = error;
      if (!(error instanceof TypeError || error?.name === 'TimeoutError' || error?.name === 'AbortError')) break;
    }
  }
  throw new Error(`Could not verify PR #${event.issue_number} author.`, { cause: lastError });
}

async function reviewMain() {
  const authorized = await isAuthorizedReview(JSON.parse(process.env.PULLFROG_PROMPT), {
    apiUrl: process.env.GITHUB_API_URL,
    repository: process.env.GITHUB_REPOSITORY,
    token: process.env.GH_TOKEN,
  });
  if (authorized) {
    appendFileSync(process.env.GITHUB_OUTPUT, 'authorized=true\n');
  } else {
    console.log('Skipping automatic review. The dispatch must target an owner-authored PR.');
  }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  if (process.argv[2] === '--review') {
    await reviewMain();
  } else {
    runOwnerCommand();
  }
}
