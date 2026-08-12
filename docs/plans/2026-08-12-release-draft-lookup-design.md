# Release Draft Lookup Fix Design

## Problem

The v0.1.0 release workflow created its draft and uploaded both verified assets,
then failed when the publisher tried to read the new draft through
`GET /repos/{owner}/{repo}/releases/tags/{tag}`. GitHub returns 404 from that
endpoint for drafts, even to the authenticated workflow token. The publisher's
fake GitHub implementation incorrectly exposed drafts through the tag endpoint,
so tests did not reproduce the production behavior.

The published v0.1.0 tag and assets remain immutable. The failed historical run
cannot be made green because a rerun checks out the same tagged publisher.

## Approaches considered

1. **Enumerate authenticated releases and select the exact tag (chosen).** The
   releases collection includes drafts, supports pagination, and supplies the
   immutable release ID needed by all later checks. Bash validates every record
   and refuses duplicate exact-tag matches.
2. **Parse `gh release list`.** This is concise, but its human-oriented output
   and field behavior are less stable than the REST contract.
3. **Capture draft identity from `gh release create` output.** This handles the
   first creation only and cannot safely recover an interrupted workflow that
   already has a draft.

## Lookup and publication behavior

Before creating or mutating anything, the publisher will page through
`GET /repos/{owner}/{repo}/releases?per_page=100`, parse numeric release IDs,
boolean draft state, and exact tag names, and select only the requested
canonical tag. Zero matches permits draft creation; one match enters the
existing release path; more than one match is an ambiguity error.

After creating a draft, the same collection lookup captures its ID. All
subsequent identity, draft-state, tag-target, asset-name, and byte checks remain
ID-based and unchanged. A published exact-tag release remains immutable and is
refused by the publisher.

## Verification

The fake GitHub boundary will match production: the tag endpoint returns 404
for drafts, while the paginated collection contains them. Regression cases will
cover first creation, interrupted-run draft discovery, pagination, duplicate
matches, malformed records, and published-release refusal. Focused tests, the
complete Podman check, sanitizer suite, and release build will run before the
follow-up PR is opened.
