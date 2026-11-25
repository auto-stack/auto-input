# AutoInput manager — phrases, deploy, and the AV runtime contract (PLAN-072)

User-level phrase manager for AutoIME. The product surface is a command-line
tool (`autoime-config`) that lists, adds, removes, imports and exports explicit
phrases in the existing `custom_phrase.txt`, then applies them through the
existing `autoime_deploy.exe` deployment path.

## Layout

```
manager/
  pac.at                  product manifest (name/version/entry; not a UI app)
  src/main.at             CLI surface: argv, file I/O, diagnostics, staging
  src/phrases.at          pure phrase model: parse/validate/upsert/remove/
                          serialize/export
  probe/                  T-01 acceptance probe (not shipped)
  tests/phrases.cases.json shared case table: inputs + independent expectations
  native/                 thin ACE AV launcher (Rust)
    Cargo.toml.in         template; Cargo.toml is rendered by prepare.ps1
    src/main.rs           discover sources -> assemble sys stdlib -> compile ->
                          run on AV -> console passthrough -> exit status
  scripts/prepare.ps1     resolve ACE root, render manifest, build, copy to bin/
  scripts/run.ps1         run the prepared launcher against in-repo sources
  scripts/test.ps1        execute the case table on the prepared launcher
  bin/                    built launcher (git-ignored)
```

The launcher is deliberately thin: it owns source discovery, the ACE
assembly/compile call, the per-run capability grant, and process exit
semantics. Phrase parsing, validation, merging and command interpretation are
Auto source, so later front ends (GUI) consume the same module.

## Commands

```text
autoime-config phrases list   [--user-data-dir D]
autoime-config phrases add    --code C --text T [--weight W] [--user-data-dir D]
autoime-config phrases remove --code C --text T [--user-data-dir D]
autoime-config phrases import --file F [--user-data-dir D]
autoime-config phrases export --file F [--user-data-dir D]
autoime-config terms   list   [--user-data-dir D]
autoime-config terms   add    --code C --text T [--weight W] [--user-data-dir D]
autoime-config terms   remove --code C --text T [--user-data-dir D]
autoime-config terms   import --file F [--user-data-dir D]
autoime-config terms   export --file F [--user-data-dir D]
autoime-config deploy         [--user-data-dir D]
```

- Data file: `<user-data-dir>\custom_phrase.txt` (default
  `%LOCALAPPDATA%\AutoIME\rime`, `AUTOIME_RIME_USER_DATA_DIR` override).
  `--user-data-dir` isolates tests and alternate profiles.
- `add` defaults `--weight` to 100. `(code,text)` is one entry: re-adding
  updates the weight in place (idempotent when unchanged); the same code with
  a different text is a separate entry. Duplicate rows of the same key are
  merged into the first one when that key is touched.
- `remove` matches the full key; removing an absent key succeeds with no
  write.
- `import` applies a whole TSV batch transactionally: every row is validated
  first, and **any** invalid row refuses the batch with its line number and
  never touches the data file.
- `export` writes the live entries as TSV (`text<TAB>code<TAB>weight`, one LF
  after each row) through the same staging+atomic-replace path.
- `deploy` runs the packaged `autoime_deploy.exe` (env `AUTOIME_DEPLOY_EXE`, or
  `AUTOIME_INSTALL_DIR` next to the launcher) for manually edited files: it
  settles any pending transaction, refreshes the compiled phrase table and
  validates the schema. Status 0 ok, 5/2/3 (fallback/engine) and any other
  non-zero map to exit 6.

Exit status: `0` ok (including idempotent no-ops), `2` usage/argument,
`3` invalid data (line diagnostics), `4` I/O (missing/unreadable/not UTF-8,
write failure), `5` stale-source conflict (another writer changed the file —
re-read and retry), `6` deploy/verification failure (the previous phrase file
was restored), `7` deployment busy (retry). Diagnostics go to stderr and never
echo phrase text or codes — only line numbers, kinds and counts.

## Personal English terms (T-05)

A second personal layer, separate from explicit phrases: case-sensitive English
trigger codes.

- Data file: `<user-data-dir>\autoime_user_terms.dict.yaml` — a Rime table
  dictionary the manager owns. The installed package ships an empty template;
  deployment copies it only when missing and never overwrites user content.
- Rows are `text<TAB>code<TAB>weight`: `code` is 2..=32 ASCII letters and is
  matched **character by character including case** (`AS` != `as`); `text`
  keeps its own case/spaces; `weight` is 1..=1000000 (`add` defaults to 1000).
  The same code may carry several texts; `(code,text)` is one entry.
- `add`/`remove`/`import` write the file canonically (fixed Rime header + one
  row per entry) through staging+atomic replace, then run `autoime_deploy.exe`
  to recompile the table (a running engine owner performs the reload). A failed
  deployment restores the previous file and exits 6.
- `list` prints `text<TAB>code<TAB>weight<TAB>personal|builtin`: personal
  entries first, then built-in aliases whose code is not overridden; the
  built-in table is read from `AUTOIME_RESOURCES_DIR` (or
  `AUTOIME_INSTALL_DIR\resources`).
- `export` writes data rows only (source order, LF after each row); `import`
  validates the whole batch first and refuses it atomically.
- Personal entries are queried before the built-in alias table, so a personal
  code overrides a built-in trigger.
- The shipped template ends with one inert placeholder row (code of 32 `z`,
  text `AutoIME`, weight 1): librime cannot compile a table with zero entries.
  `list`/`export` hide it, `add`/`remove` refuse its code, and removing the last
  personal term re-emits it automatically.
- Every mutation runs a deployment whose identity covers the terms file bytes,
  so a change invalidates the "resources unchanged" fast path and recompiles the
  table; without that, an added term would not take effect in later sessions.

Exit status is the same as phrases: 0 ok (including idempotent no-ops), 2 usage,
3 invalid data, 4 I/O, 5 conflict, 6 deploy failure. Diagnostics never echo
term text.

Verification: `tests/phrases.cases.json` (`T-05-*`, six CLI cases) and
`../windows/ime/tests/run-t05.ps1` (CLI → deploy → real engine recall).

## Phrase transaction (T-03)

`add`/`remove`/`import` never rewrite the data file directly. They stage a
complete new file, the exact source bytes they parsed, and a verification
request, then call the native tool:

```text
autoime_deploy.exe phrases apply --request <user-dir>/.autoime-apply-<token>.req
```

The request is a UTF-8 `key=value` file (no shell quoting, no phrase text on
the command line; the token mixes a content hash with a per-run time nonce,
so two managers always get their own staging files — identical retries may
still conflict on the source check, which is the intended retry signal). Under the deployment lock the native tool:

1. recovers a transaction a previous process left pending (restores the old
   phrase file, drops the compiled output, removes the journal);
2. re-checks the source: byte-exact comparison against the snapshot (or the
   "file was missing" marker) — a mismatch is exit `5`, nothing is written;
3. backs the current file up and journals `published`;
4. publishes the staged file atomically;
5. runs Rime maintenance (resource deploy + dictionary compile) and verifies
   the expected candidate in a **new Rime session** (present for add/import,
   absent for remove);
6. commits (journal + backup removed) or rolls back on any failure
   (previous file restored, compiled output dropped, exit `6`).

Fallback (Luna), disabled and verification failures are failures, never
success. The learning database (`autoime_user.userdb`) is never touched. An
interrupted transaction is recovered by the next phrase transaction *or* plain
`deploy`; recovery records only the transaction phase and the version
identities, never phrase content.

## File contract

- UTF-8 (a leading BOM is accepted and dropped on write); LF and CRLF input
  are both accepted. Writes normalize line endings to LF.
- `#` comment lines (after optional leading whitespace) and blank lines are
  preserved verbatim, in order. Untouched entry lines keep their content too.
- Entry line: `<text><TAB><code><TAB><weight>`. `code` is 1..=64 lowercase
  ASCII letters; `text` is 1..=1024 scalars without TAB/CR/LF/NUL; `weight`
  is an integer in 1..=1000000 (the fixed supported range, SD-01).
- Writes are staged next to the target (`.autoime-staged`), identity-checked
  (FNV-1a over the staged bytes vs the computed content), then replaced
  atomically; a failed write leaves the previous file untouched. `read_bytes`
  (Option) distinguishes a missing file from an empty file and from an
  unreadable/invalid-UTF-8 one — an error is never treated as empty data.

## Preparation and run (development)

```powershell
pwsh -NoProfile -File experimental/autoime/manager/scripts/prepare.ps1
pwsh -NoProfile -File experimental/autoime/manager/scripts/run.ps1 phrases list
pwsh -NoProfile -File experimental/autoime/manager/scripts/test.ps1
```

`prepare.ps1` resolves the ACE source root in the repository-mandated order:

1. `$AUTO_LANG_ROOT`
2. group sibling `auto-lang` (worktree layout: `<group>/auto-input` + `<group>/auto-lang`)
3. `D:/autostack/auto-lang`

A missing root, missing `rustc`/`cargo`, or a failed build is a named
preparation failure. There is no fallback runtime: the manager runs on the new
ACE AV only. `CARGO_TARGET_DIR` may redirect the build cache (daily builds
reuse a prepared ACE target).

`run.ps1` sets `AUTOIME_MANAGER_DIR` to `manager/src` or `manager/probe` and
passes every remaining argument to the Auto program as its argv.

`test.ps1` executes `tests/phrases.cases.json` against the prepared launcher in
isolated temporary user-data directories, prints per-case wall time, and fails
on any exit/stdout/stderr/file mismatch. It never builds. Mutating cases need
the real engine and deploy tool (named preparation):

```powershell
pwsh -NoProfile -File experimental/autoime/manager/scripts/test.ps1 `
  -DeployExe experimental/autoime/windows/ime/build/autoime_deploy.exe `
  -Resources experimental/autoime/shared/resources
```

`tests/txn.ps1` is the phrase-transaction integration suite: source
conflict/appeared-file races, two writers, lock busy, crash recovery at both
recovery points, verification present/absent, rollback, Unicode user-data
paths, learning-db preservation, broken-resource failure, and an independent
`rime_probe.exe` process that must see the added phrase (and the frozen 071
baseline cases must still pass):

```powershell
pwsh -NoProfile -File experimental/autoime/manager/tests/txn.ps1 `
  -Exe experimental/autoime/windows/ime/build/autoime_deploy.exe `
  -Resources experimental/autoime/shared/resources `
  -ManagerExe experimental/autoime/manager/bin/autoime-config.exe `
  -Probe experimental/autoime/windows/probe/build/rime_probe.exe `
  -BaselineCases experimental/autoime/shared/tests/cases.json
```

Both suites refuse to run without their prerequisites (no silent skips).

Host acceptance (`tests/host-textbox.ps1`, V-07) drives a real application window through the TSF service: it types the code, selects candidate 1 and asserts the committed text, cycling the input switcher when needed. Windows 11 Notepad is a packaged app whose window is not reachable by class name, so the probe hosts a plain WinForms text box — an ordinary TSF client on the same input path. Run it deliberately (it types into the desktop):

```powershell
pwsh -NoProfile -File experimental/autoime/manager/tests/host-textbox.ps1 -Code vhost -Text 安装包验收词
```

## Runtime contract (T-01 verified)

- **argv** is the real process command line (`std.process.args`); quoting,
  Chinese, spaces and emoji survive unchanged.
- **Data files** are read/written with `std.file`; writes use a same-directory
  staging file plus `replace_atomic`.
- **Deploy** runs the existing native `autoime_deploy.exe` through
  `std.process.spawn5`; its exit status is the observed result.
- **stdout/stderr** go through the AV console lane and are re-emitted
  byte-exactly by the launcher.
- **Exit status** is the program's own `int` result (`return n`) or
  `std.process.exit(n)`.
- **Stack**: ACE's front end recurses over module sources; the launcher is
  linked with a 32 MiB stack (`/STACK:33554432`, same as the auto-os desktop
  host). A default-stack build overflows during binding.
- **Wall budget**: 180 s per run (the CLI is short-lived; a first cold deploy
  may compile dictionaries for tens of seconds).

## Named upstream defect (auto-lang owner)

At the pinned SDK, the frozen assembled 972 stdlib set cannot parse
`stdlib/ace/asm/list.at`: its `xs[i]` index expression is rejected by the
packages-modules front door (`source.unsupported` "`fn` is not part of this
source profile"). The repository's own
`sys_d01_d06_1064::v02_d01_d02_file_text_range_and_dir_names_av` fails for the
same reason. The manager never imports `std.list`, so the launcher drops that
one module from the assembled input set. The minimal reproducer and the handoff
are recorded in `docs/plans/072-autoime-ace-phrase-manager.md` (§9/§10); the
filter must be re-verified when auto-lang lands the parse fix.

## AV language constraints that shape the Auto source

- A `for` over a list and `get(i)` yield *views*: only `.len()` is consumable
  on this profile. Owned elements come from `remove(i)`/`remove(0)`; a moved
  local is dead, so reuses need `.clone()`.
- `Str` parameters are owned: forwarding one to another function moves it —
  pass `.clone()` when the original is still needed.
- Multi-module projects use item imports: `use app.phrases.<fn>`; `type`
  declarations shared across modules need `pub`.
- `is` matches a bound local, not a call expression; arms returning a record
  need an initialised fallback local (record results have no fabricable
  filler).
