# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **Reference docs** (reading order: `CLAUDE.md`):
  - `docs/internals.md`: architecture, GE-specific RSP deltas, phased plan (§1–§10).
  - `docs/dev/findings/`: the finding log, one file per finding (`YYYY-MM-DD-slug.md`; older ones `YYYY-MM-DD-Dxx_slug.md`). A finding is a trap, not a feature log: `docs/dev/findings/README.md` says what qualifies. **Look findings up in the untracked `docs/dev/findings-index.csv` (build it with `tools_pc/gen_findings_index.py`), then read only that one file. Never read the directory.**
  - `docs/porting-notes.md`: the recurring N64→PC bug classes. Skim the headers and read what is relevant.
- **Open work / known issues:** the GitHub issues of this fork (sub-issues per phase). Never start a backlog file.
- **Current status:** the README "Status" section. Current task and environment: `docs/HANDOFF.md` (rolling and local; may be absent).
- **Dispatching subagents?** `docs/dev-process.md` (budgets, file partitioning, brief template). Every investigation subagent reads `docs/porting-notes.md` first and appends to it.

## Non-negotiables

1. **No N64 build (withdrawn 2026-11-27).** The goal is a modern engine that runs the GoldenEye campaign, not a matching N64 ROM. The N64 build is removed (2026-11-28): the `Makefile`, `ld/`, the N64 tools and extraction scripts, the libultra I/O and OS sources and the boot code that only it compiled. What the PC build or its tools read stays. The removed files are in git history (`git log --diff-filter=D --stat`) and in the `upstream` remote.
2. **`src/game` may be changed (rule withdrawn 2026-11-27).** The decompiled code is being moved out and removed, so it is edited or deleted like any other code. Diagnosis comes first: name the exact cause before the fix. A change in behaviour is stated in the PR (what it did before, what it does now).
3. **Region macros belong to the PC build.** `CMakeLists.txt` `REGION_DEFS` no longer has to follow the N64 Makefile (finding A1 is history).
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles `libultra/audio`, `libultrare/audio`, `libultra/gu` and `libultrare/io/vitbl.c` only. Every other `io/` and `os/` file is excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** for the GBI commands GE emits and its custom CC/RM modes. `port/fast3d/` replaces it on PC.
6. **A report or a question is not a go.** When the maintainer reports a symptom ("it flickers", "this doesn't line up") or asks a question, look first: read what they sent (the screenshot, the log) closely, check it against the code, then answer with what you see and the likely cause, and stop. Change code only once they ask for the fix. Never start edits on a hunch from one sentence.
7. **Clean room for porting logic.** The goal is to remove the legally grey code (the decompiled game, libultra, the N64-specific code) step by step. Logic or a format taken from it is ported in two separate steps, by two separate agents:
   - **Spec agent** (may read the decomp): writes the behaviour or format as facts in prose in `docs/specs/` (layouts, fields, constants, rules, measured values), never code pasted or paraphrased line by line. `docs/specs/README.md` has the rules.
   - **Implementation agent** (may NOT read the decomp): writes the code from `docs/specs/` only. Its brief lists what it must not open: `src/`, `include/`, `rsp/`, `PR/gbi.h`, libultra, `port/fast3d/` and any tool derived from them. A question the spec does not answer goes back to the spec agent, who amends the spec; the implementation agent never looks it up itself.
   - The lead never pastes decomp code or names decomp functions into an implementation brief. New code does not depend on `src/game`, libultra or fast3d: it reads the exports.
   - **Wiring is allowed.** An implementation agent that makes the engine read an asset from the asset packs may open `src/game` to find and change the place where the engine asks for that asset (the call site), and keeps the old ROM path behind a `GE_ROM_<KIND>` flag until the kind is validated. Formats, layouts and game logic still come only from `docs/specs/`.

## Tree hygiene & release gate

The repo is public, MIT-licensed and forked, so these apply to anything tracked or pushed:

1. **No local machine paths or usernames.** Use the placeholders `<repo>`, `<repos>`, `<games>`, `<videos>`, `<python>`, `<temp>` (legend at the top of `docs/dev/HANDOFF-ARCHIVE.md`). Tools take paths as arguments and never default to a machine-specific one.
2. **No contributor emails in prose.** Credit by name plus PR/issue number. Commit trailers use noreply addresses only.
3. **Every vendored third-party component is declared in `NOTICE`** with its license and holder. GPL sources ship their license text in-tree. Never relicense inherited code or the repo.
4. **Screenshots are game content.** Keep repo imagery documentary and small. Never commit bulk ROM-derived captures (the capture dirs are gitignored). The only bulk imagery is `tools_pc/golden/**/*.png`, which `verify.sh` consumes.
5. **Batch, don't spam.** One commit per session for approved doc/tree fixes; no per-item branches.
6. **Releases, squashes and history rebuilds** (PII and legal reports, contributor record merges, force-push backups): `docs/dev/RELEASE-GATE.md` §Release and history rules.

## Options overlay

Adding an option or writing its help text: `docs/wiki/options-menu.md`.

## Critical files

| File | Role |
|---|---|
| `CMakeLists.txt` | The build: source list and `REGION_DEFS`. |
| `port/src/` | Shims: `libultra.c` (OS API, scheduler leaf calls, runs the software RSP inline; the real `src/sched.c` is compiled), `n64stubs.c`, `random.c`, `ucode.c`, `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). |
| `rsp/graphics/gmain.s` | GE's RSP ucode: ground truth for GBI/CC/RM. |
| `reference/mouse-injector/README.md` | Stub (the GPL source was removed 2026-09-19). The mouse-aim model is in `port/src/input.c`. |
| A local **Perfect Dark PC port** checkout ([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)) | Standing reference whenever a work item has a PD analogue. Port-layer files only; same family does not mean the same format, so validate per field. Audit: `docs/internals.md` §2.4. |

## Build

All three targets build in Docker. No ROM is needed to build: the ROM-asset symbols are pre-generated and the game reads the user's ROM at run time.

```sh
docker compose pull                     # once: the published build image (devkitA64 + patched uam + Linux host libs)
docker compose run --rm switch          # -> build-switch/ge007.aarch64.nro (what CI ships)
docker compose run --rm linux           # -> build-linux/ge007.x86_64
docker compose build windows            # once: Dockerfile.windows (MinGW-w64)
docker compose run --rm windows         # -> build-win/ge007.x86_64.exe + DLLs
```

The build image is `Dockerfile.build`, published to GHCR and pinned by digest in `ci.yml`, `compose.yaml`, `tools/dev.py` and `tools_pc/fxview/fxview.py`. To change it: edit `Dockerfile.build` (test with `docker compose build image-local`), push a branch (`.github/workflows/build-image.yml` publishes it and prints the digest), then `python tools/check_build_image.py --set <ref>`.

The Switch build is the reference: `.github/workflows/ci.yml`, the only workflow on this fork, builds it in the same pinned devkitA64 image, plus the Windows zip, the Linux unit tests and the generated-file checks. SD-card layout, nxlink test builds and release packaging: `docs/building.md` §Nintendo Switch.

A native Windows build (`./build-pc.sh ntsc-final` from the MSYS2 MINGW64 shell, upstream's reference) still works. Its three recurring failures, to check in this order:

1. **`Cannot create temporary file in C:\Windows\`** at link: TMP is lost in a non-login shell. `build-pc.sh` re-runs itself under PowerShell to fix this. By hand, run cmake/ninja from cmd or PowerShell with `C:\msys64\mingw64\bin` on PATH.
2. **`cannot open output file ge007.x86_64.exe: Permission denied`**: the game is running. Never kill the user's game process to make a link succeed.
3. **`cc1.exe: ... libmpfr-6.dll`**: `C:\msys64\mingw64\bin` is missing from PATH.

Details and the history of the re-run fixes (D515 and earlier): `docs/building.md` §Windows build environment troubleshooting.

## Verification ritual (after any build-affecting change)

Run `/linkcheck`. Every symbol the compiled set references (`CMakeLists.txt` `SRC_*` lists) is defined exactly once, in the compiled set or in `port/`. Symbols from excluded files (`libultra/io|os`, `libultrare/io|os` except `vitbl.c`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) come from `port/src/libultra.c`, `n64stubs.c`, `random.c` or `ucode.c`. No duplicates (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers). Every touched file parses; `docker compose run --rm switch` (plus `linux` / `windows` when shared PC code changed) is the final word. If the work taught a trap (what qualifies: `docs/dev/findings/README.md`), record it as one new file `docs/dev/findings/YYYY-MM-DD-slug.md`. Feature status and build or measurement logs go in the PR, not in a finding.

## Phase status

Not kept here. Open work: the GitHub issues. Release state: the README "Status" section.
