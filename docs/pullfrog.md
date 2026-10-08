# Pullfrog owner commands

Pullfrog runs only after **jatmn** creates a new top-level issue or PR comment
whose first line begins with `@pullfrog` and a visible instruction:

```text
@pullfrog review this PR and report actionable defects
```

Other commenters, passive or quoted mentions, bare mentions, edited comments,
inline review comments and workflow reruns cannot run the agent. Post a new
command to retry. GitHub supplies the actor and commenter identity; the workflow
also checks jatmn's account ID, `12479882`, and checks out the trusted default
branch. The Node.js helper verifies the event before the agent step receives
credentials. This follows the owner-command configuration in
[yuoki-quinityn](https://github.com/jatmn/yuoki-quinityn/blob/main/docs/pullfrog.md).

The workflow has no PR, push, schedule or `workflow_dispatch` trigger. Opening
a PR does not request a review, and Pullfrog's dashboard and managed mention
dispatcher cannot launch this workflow. Do not replace it with the console's
generated workflow. Existing CI stays independent.

## Account and repository setup

1. Give the Pullfrog GitHub App access to `jatmn/Deneb` and select the
   repository in the [console](https://pullfrog.com/console/jatmn?repo=Deneb).
   Use BYOK billing. Leave **Add workflow directly** untouched; this repository
   supplies its own workflow.
2. Connect the ChatGPT subscription with `npx pullfrog auth codex --org jatmn`.
   Complete the browser device sign-in. Pullfrog stores and refreshes the
   credential for the personal account; no credential belongs in Git or a
   GitHub Actions secret. See [subscription setup](https://docs.pullfrog.com/codex-auth).
3. Turn off managed mentions, automatic PR reviews and re-reviews, review
   responses, issue enrichment and labels, CI autofix, merge-conflict fixes,
   Quick Links, automatic approval and auto-merge. Keep non-collaborator
   triggers off. The workflow's event and owner checks remain the execution
   boundary even if a dashboard setting changes.
4. Merge the reviewed workflow PR to the default branch, then click
   **Verify manual installation** in the repository console. Comment workflows
   load from the default branch. Create a new owner command to verify a live
   run; local tests do not prove provider access.

The workflow explicitly selects `openai/gpt-sol` (Pullfrog's GPT Sol alias),
using the connected ChatGPT subscription. No GitHub provider API keys are passed to this workflow. A matching API
key stored in Pullfrog can still be a fallback when subscription quota is
exhausted; omit that fallback if API billing is unwanted.

The action can push feature branches, but cannot push the default branch,
delete branches or push tags. Its shell stays restricted. The legacy
`status_checks` input enables native PR status/verdict checks for an explicitly
requested PR command; it does not enable automatic reviews or approving reviews.

## Maintenance and checks

The action bootstrap is pinned to Pullfrog v0.1.97; its npm runtime accepts
compatible 0.1.x updates. Keep `PULLFROG_VERSION` in
`scripts/pullfrog-command-check.mjs` aligned with the bootstrap, and verify the upstream
structured payload and check-reporting contracts when updating it. Large
commands are read from the original GitHub event snapshot, never a comment
that may have been edited after authorization.

```sh
node scripts/pullfrog-command-check.test.mjs
actionlint
git diff --check
```
