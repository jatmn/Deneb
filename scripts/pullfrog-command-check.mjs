import { appendFileSync, readFileSync } from 'node:fs';

const OWNER = 'jatmn';
const OWNER_ID = 12479882;
const REPOSITORY = 'jatmn/Deneb';
// Keep aligned with the action bootstrap in .github/workflows/pullfrog.yml.
const PULLFROG_VERSION = '0.1.97';
const MAX_PAYLOAD_BYTES = 64 * 1024;

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
