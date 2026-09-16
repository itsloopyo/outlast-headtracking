# Vendored MinHook

This directory is a copy of MinHook, taken from upstream and committed here so
that `pixi run package` builds on a clean checkout with no network access. It is
the authority on what is on disk; `THIRD-PARTY-NOTICES.md` at the repo root is a
copy of what this file says.

- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Release:** `v1.3.4`
- **Commit:** `c3fcafdc10146beb5919319d0683e44e3c30d537` (2025-03-28)
- **Licence:** BSD-2-Clause, reproduced verbatim in `LICENSE.txt` beside this
  file. `AUTHORS.txt` is upstream's, unmodified.

Only the files CMakeLists.txt compiles, plus `include/MinHook.h`, `AUTHORS.txt`
and `LICENSE.txt`, were taken. Upstream's own build files, DLL resources and
project files were not.

## Local change

One file differs from the release above. BSD-2-Clause does not require a
modification to be marked, but a vendored tree that silently diverges from the
tag it names is a tree nobody can audit, so it is recorded here.

`src/hook.c` allocates MinHook's internal bookkeeping from the process heap
rather than from a private heap of its own, and correspondingly does not destroy
that heap on uninitialise. Every other file matches `v1.3.4` byte for byte once
line endings are normalised, which is checked by diffing this tree against the
tag.
