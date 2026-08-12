# Runtime Integration Fixes Design

## Problem

The installed runner exposes connection results and the creation action, but
two integration boundaries do not match Plasma's runtime behavior:

- the user-local KCM is outside Qt's configured plugin search paths, so the
  runner metadata names a plugin that Plasma cannot discover;
- profile activation is gated by an in-memory set populated by the latest
  `Match` call, even though KRunner may end the match session before invoking a
  retained result.

The creation action is unaffected because it has a fixed action identifier and
does not use the offered-profile set.

## Configuration-module discovery

The bundle remains relocatable and user-local. During installation, the
installer will rewrite `X-KDE-ConfigModule` in the installed KRunner desktop
metadata to the exact absolute path of the installed KCM, just as it already
rewrites the D-Bus service `Exec` path to the installed runner executable.

The bundle's source metadata continues to use the conventional relative KDE
plugin namespace. Direct system packages can therefore place the KCM in the
configured Qt plugin tree, while the user-local installer does not require a
global `QT_PLUGIN_PATH`, a logout, or root access.

Desktop-entry escaping must round-trip every install prefix already accepted
by the installer, including spaces, quotes, and backslashes. Installation
remains a four-file atomic transaction: the transformed desktop metadata is
staged before any destination replacement.

## Profile activation

`Run` will no longer depend on state retained from `Match`. It will accept only
lowercase 64-character hexadecimal profile identifiers, which is the opaque ID
format produced by the repository. Reserved action and error identifiers stay
separate.

The launcher then performs the authoritative checks already provided by the
system:

1. the registry must still have one selected installation;
2. catalog resolution is scoped to that selected instance;
3. the catalog snapshot and watched directory must still be clean;
4. the source must still be a regular profile with the same canonical opaque
   identity;
5. only the catalog-supplied absolute launch path reaches Remmina.

Consequently, arbitrary, stale, cross-instance, removed, retargeted, and
malformed identifiers remain non-actionable without relying on match-session
memory. `Teardown` may release catalog session resources without invalidating a
result that Plasma is about to run.

## Verification

All builds and tests run through the Podman wrapper. Regression coverage will
include:

- `Match`, `Teardown`, then `Run` for a valid profile;
- malformed and unknown opaque identifiers remaining rejected;
- replacement by a later match no longer being an authorization boundary;
- installed desktop metadata resolving the exact installed KCM from a hostile
  user-local prefix without `QT_PLUGIN_PATH`;
- an isolated private-D-Bus activation test that launches a fake Remmina with
  the exact `--connect` profile path;
- focused tests, the complete check suite, sanitizers, and a fresh source
  bundle build.

No profile names, servers, labels, paths, or activation tokens are logged by
production code or test diagnostics.
