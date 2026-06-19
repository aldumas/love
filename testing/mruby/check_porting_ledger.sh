#!/usr/bin/env bash
#
# Reconcile the mruby-port deferral ledger against inline code markers.
#
# Each tagged item in testing/mruby/PORTING.md carries a stable tag like
# (#win-icon); the same tag must appear in a `// TODO(mruby) #win-icon: …`
# comment at the deferral site under src/. This script enforces the link and is
# wired up as the pre-commit hook (install with: make install-hooks). Run it
# directly any time to reconcile:
#
#   testing/mruby/check_porting_ledger.sh
#
# Rules (all must hold):
#   1. every #tag in a code marker exists in the ledger      (no orphan markers)
#   2. every OPEN ([ ]/[~]) ledger tag has >=1 code marker   (no untracked stub)
#   3. every DONE ([x]) ledger tag has NO code marker left   (clean up on finish)
#
# Untagged ledger items (e.g. §C/§D, no code yet) are ignored. Bypass in an
# emergency with `git commit --no-verify`.

set -euo pipefail

root="$(git rev-parse --show-toplevel)"
ledger="$root/testing/mruby/PORTING.md"
srcdir="$root/src"

# No ledger (e.g. a branch without the port) -> nothing to check.
[ -f "$ledger" ] || exit 0

# Sorted-unique tag lists. Code markers carry the tag after `TODO(mruby) `;
# ledger declarations carry it in parenthesized `(#tag)` form, so prose mentions
# of a tag (in backticks, no parens) are ignored.
code_uniq()   { grep -oE '#[a-z0-9-]+' | sort -u; }
ledger_uniq() { grep -oE '\(#[a-z0-9-]+\)' | tr -d '()' | sort -u; }

# Tags referenced by code markers (tag must sit on the TODO(mruby) line).
code_tags="$(grep -rhoE 'TODO\(mruby\) #[a-z0-9-]+' "$srcdir" 2>/dev/null | code_uniq || true)"

# All ledger tags, and the subset on done ([x]) lines.
all_ledger="$(ledger_uniq < "$ledger" || true)"
done_ledger="$(grep -E '^\s*-?\s*\[[xX]\]' "$ledger" | ledger_uniq || true)"
# Open ledger tags = all minus done.
open_ledger="$(comm -23 <(printf '%s\n' "$all_ledger" | sed '/^$/d') \
                        <(printf '%s\n' "$done_ledger" | sed '/^$/d'))"

cmp_only() { # lines in $1 not in $2
	comm -23 <(printf '%s\n' "$1" | sed '/^$/d') \
	         <(printf '%s\n' "$2" | sed '/^$/d')
}

orphans="$(cmp_only "$code_tags" "$all_ledger")"
missing="$(cmp_only "$open_ledger" "$code_tags")"
stale="$(comm -12 <(printf '%s\n' "$done_ledger" | sed '/^$/d') \
                  <(printf '%s\n' "$code_tags"   | sed '/^$/d'))"

fail=0
report() { # $1=message $2=tag-list
	[ -n "$2" ] || return 0
	fail=1
	echo "porting-ledger: $1" >&2
	printf '  %s\n' $2 >&2
}

report "code marker(s) with no matching ledger entry (add to PORTING.md):" "$orphans"
report "open ledger item(s) with no code marker (add a // TODO(mruby) #tag):" "$missing"
report "done [x] ledger item(s) still marked in code (delete the marker):" "$stale"

if [ "$fail" -ne 0 ]; then
	echo "" >&2
	echo "porting-ledger: ledger and code markers are out of sync (see above)." >&2
	echo "Reconcile testing/mruby/PORTING.md with: grep -rn 'TODO(mruby)' src/" >&2
	echo "Bypass once with: git commit --no-verify" >&2
	exit 1
fi

exit 0
