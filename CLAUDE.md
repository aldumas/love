# CLAUDE.md

This checkout is the **mruby-port** branch of LÖVE: the Lua/LuaJIT scripting
layer is being replaced with [mruby](https://mruby.org/), and the API
convention changes from positional parameters to **keyword arguments** under a
top-level `Love` namespace (snake_case methods, `?`-suffixed predicates).

## Before continuing any port work, read the deferral ledger

**`testing/mruby/PORTING.md`** is the source of truth for everything skipped,
stubbed, or temporarily implemented during the port. Each item with a code site
carries a stable tag (e.g. `#win-icon`) that also appears in a
`// TODO(mruby) #win-icon: …` marker at the deferral site. A pre-commit hook
(`testing/mruby/check_porting_ledger.sh`, install with `make install-hooks`)
fails commits when the ledger and markers drift. When you finish a deferred
item, flip its box to `[x]` in PORTING.md **and** delete its inline marker(s)
in the same commit.

Orientation and build instructions for the ported slice live in
`testing/mruby/README.md`.
