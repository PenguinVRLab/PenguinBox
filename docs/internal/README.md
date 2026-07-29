# docs/internal/ — PRIVATE. Never merged to the public repo.

Standing owner rule (2026-07-27): everything in this directory is internal
specification work for the fork's maintainers, in the private working repo
(`patrickfcarey/xemu_vr`).

- These files are **never added to the allowlist** for merging content over
  to the public repo (`penguinvrlab/penguinbox`).
- Anyone preparing a public-bound merge or PR must exclude `docs/internal/`
  **entirely** — no exceptions, no partial files, no excerpts.

**Not `docs/specs/`.** That directory is QEMU's — 35 inherited `.rst`
hardware specs that are legitimately public and ship in the public repo.
Internal specs live here, under a name that is unambiguously ours.

## Enforcement (install this in every clone, including the rig)

```sh
ln -sf ../../tools/git-hooks/pre-push .git/hooks/pre-push
```

Git hooks are not carried by clone, so this is a per-checkout step. The hook
hard-fails any push to `penguinvrlab/penguinbox` whose commit tree contains
`docs/internal/`; pushes to the private `origin` are unrestricted. To publish,
use `tools/make-public-export.sh <ref>` — it cuts a throwaway branch with the
private paths removed (and leaves upstream's `docs/specs/*.rst` intact); push
that branch.

Verify the guard is live in a checkout:

```sh
printf 'r %s r %s\n' "$(git rev-parse HEAD)" "$(printf '0%.0s' {1..40})" \
  | .git/hooks/pre-push penguinbox https://github.com/penguinvrlab/penguinbox.git
# exit 1 + "BLOCKED" when HEAD carries docs/internal/ — that is the guard working
```

Start here: [00-specification-plan.md](00-specification-plan.md).
