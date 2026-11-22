# Pullfrog owner commands and reviews

For manual commands, **jatmn** creates a new top-level issue or PR comment
whose first line begins with `@pullfrog` and a visible instruction:

```text
@pullfrog review this PR and report actionable defects
```

Other commenters, passive or quoted mentions, bare mentions, edited comments,
inline review comments and workflow reruns cannot run the agent. Post a new
command to retry. GitHub supplies the actor and commenter identity; the workflow
also checks jatmn's account ID, `12479882`, and checks out the trusted default
branch. The Node.js helper verifies the event before the agent step receives
credentials.

## Automatic reviews

Pullfrog reviews new PRs, drafts, and updated diffs in this public repository
when GitHub identifies `jatmn` as the PR author. The person pushing an update
can be a different account. The GitHub App dispatches the workflow on the
default branch and the author check must pass before the agent starts.
In Pullfrog settings, enable PR reviews, draft reviews, and re-reviews on new
commits, and disable managed mentions and external-contributor reviews.
Use a fresh owner-authored draft and push an update after the workflow is
merged to verify live automatic dispatch.

## Model and permissions

The manual command job explicitly selects `openai/gpt-sol` (Pullfrog's GPT Sol alias),
using the connected ChatGPT subscription. No GitHub provider API keys are passed to this workflow. A matching API
key stored in Pullfrog can still be a fallback when subscription quota is
exhausted; omit that fallback if API billing is unwanted.

The action can push feature branches, but cannot push the default branch,
delete branches or push tags. Its shell stays restricted. The legacy
`status_checks` input enables native PR status/verdict checks for an explicitly
requested PR command; it does not enable automatic reviews or approving reviews.

## Maintenance and checks

The action bootstrap is pinned to Pullfrog v0.1.122; its npm runtime accepts
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

GitHub loads this `issue_comment` workflow from the default branch. After a
change to the workflow, Pullfrog version, or provider access, have **jatmn**
post a new top-level `@pullfrog` command once any workflow changes are on the
default branch. In GitHub Actions, confirm that **Run Pullfrog** succeeds and
check that Pullfrog posts the requested response.

The local checks cover command authorization, workflow syntax, and whitespace.
The live run verifies that Pullfrog can use its stored credentials to reach the
configured model provider.
