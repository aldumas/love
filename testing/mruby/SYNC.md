# Keeping `mruby-port` in sync with upstream love2d/love

The mruby port is a long-lived branch on top of upstream LÖVE. Upstream keeps
moving (new modules, engine fixes, CMake edits). This is the process for pulling
those changes in with the least pain.

## Remotes (see the `auto-push-fork` memory)
- `origin` = `love2d/love` — **read-only upstream**; never push here.
- `fork` = `aldumas/love` — your fork; `mruby-port` tracks `fork/mruby-port`.

## Principles

1. **Merge, never rebase.** `mruby-port` is published (pushed to `fork`).
   Rebasing onto upstream rewrites shared history. Always merge upstream *into*
   the port.
2. **Sync at upstream release tags, not every commit.** Merge when love2d cuts a
   12.x beta/release, so you resolve conflicts against a tested upstream state.
   `sync_upstream.sh status` lists the newest tags.
3. **Keep the port additive — that's what keeps merges cheap.**
   - New files (`wrap_*_mrb.cpp`, `common/mrb_runtime.cpp`, the `_mrb` boot,
     `testing/mruby/*`) **never** conflict.
   - Shared engine `.cpp` edits live behind `#ifndef LOVE_MRUBY` guards;
     they conflict **only** when upstream edits the same lines. `PORTING.md`
     maps each guarded region to its tag, which guides resolution.
   - **`CMakeLists.txt` is the highest-conflict file.** The migration therefore
     uses a *parallel* mruby CMake path (a separate `cmake/LoveMruby.cmake`
     invoked by a tiny guarded hook) rather than editing the Lua build lines in
     place, so upstream CMake churn rarely conflicts. The tradeoff: when upstream
     **adds** a source file to a module, add it to the mruby source list too — a
     small mechanical edit that fails the build loudly, not a merge conflict.

## The routine

```sh
# 1. See what's new upstream (fetches origin + tags).
testing/mruby/sync_upstream.sh status

# 2. Start a sync branch and merge an upstream tag (or origin/main).
testing/mruby/sync_upstream.sh start 12.0     # or: start   (= origin/main)

# 3. Resolve any conflicts. They cluster in:
#    - shared engine .cpp where upstream touched a #ifndef LOVE_MRUBY region
#      (consult PORTING.md for the governing tag),
#    - the mruby source list, if upstream added/removed files in a ported module.

# 4. Verify: ledger + build + run the .rb suite.
testing/mruby/sync_upstream.sh verify

# 5. Fold the sync branch back into mruby-port and push.
git switch mruby-port && git merge sync/<date> && git push
```

If upstream added a **new module or object-type feature**, that is new port work:
add a `wrap_<X>_mrb.cpp`, append it to the source lists (harness + the mruby CMake
path), and record any deferrals in `PORTING.md` (with tags + markers) per the
ledger rules.

## What to check after every sync
- `bash testing/mruby/check_porting_ledger.sh` passes (ledger ↔ markers in sync).
- The harness builds and the `*_test.rb` suite passes (`verify` does both).
  `filesystem_mount_test.rb` has a known fixture failure (`ext/hello.txt`)
  independent of upstream — compare against a pre-merge run rather than treating
  it as a regression.
- Watch upstream commits that touch **ported areas** — e.g. graphics/font/text
  metrics, Transform, scissor/DPI — since those are where behavior can drift even
  without a textual conflict (the engine `.cpp` changed under a binding you
  reimplemented in a wrapper). Re-run the relevant `*_test.rb`.

## Automation
- **CI gate** (`.github/workflows/mruby.yml`, runs on the fork): builds mruby
  (pinned) + the CMake harness and runs the `.rb` suite headless on every push /
  PR, so a sync that breaks the port is caught immediately.
- **Drift watch** (scheduled job in the same workflow): periodically fetches
  upstream and reports when `origin/main` has advanced, so syncing is proactive.

## mruby pin
mruby is pinned to **4.0.0 @ `9c4e7ea0f`**. The build config is vendored at
`testing/mruby/mruby_love_config.rb` (canonical — the Makefile installs it into
the mruby tree before building). Bump both together when moving mruby.
