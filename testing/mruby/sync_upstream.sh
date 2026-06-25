#!/usr/bin/env bash
#
# Help keep the mruby-port branch in sync with upstream love2d/love.
#
#   testing/mruby/sync_upstream.sh status        show drift vs upstream (default)
#   testing/mruby/sync_upstream.sh start [REF]   create a sync branch and merge REF
#   testing/mruby/sync_upstream.sh verify        build the harness + run the .rb suite
#
# REF defaults to origin/main; pass an upstream tag (e.g. 12.0) to sync at a
# release point instead (recommended — see SYNC.md).
#
# Conventions (see the auto-push-fork memory / SYNC.md):
#   origin = love2d/love   (read-only upstream; never push)
#   fork   = aldumas/love  (your fork; mruby-port lives here)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PORT_BRANCH="mruby-port"
cd "$ROOT"

cmd="${1:-status}"
ref="${2:-origin/main}"

status() {
	echo "Fetching upstream (origin) + tags..."
	git fetch --quiet origin --tags
	local ahead behind base
	base="$(git merge-base "$PORT_BRANCH" origin/main)"
	ahead="$(git rev-list --count "origin/main..$PORT_BRANCH")"
	behind="$(git rev-list --count "$PORT_BRANCH..origin/main")"
	echo
	echo "merge-base:        $(git log -1 --format='%h %ci %s' "$base")"
	echo "origin/main HEAD:  $(git log -1 --format='%h %ci %s' origin/main)"
	echo "$PORT_BRANCH:      ahead $ahead / behind $behind  (vs origin/main)"
	echo
	if [ "$behind" -eq 0 ]; then
		echo "Up to date with origin/main. Nothing to sync."
	else
		echo "Upstream has $behind new commit(s). Recent ones:"
		git log --oneline --no-decorate "$PORT_BRANCH..origin/main" | head -20
		echo
		echo "Newest upstream tags:"
		git tag --sort=-creatordate | head -5
		echo
		echo "To sync at a release tag (recommended):"
		echo "    $0 start <tag>"
		echo "or at origin/main:    $0 start"
	fi
}

start() {
	git fetch --quiet origin --tags
	local syncbranch="sync/$(date +%Y%m%d)"
	echo "Creating $syncbranch from $PORT_BRANCH and merging $ref ..."
	git switch -c "$syncbranch" "$PORT_BRANCH"
	if git merge --no-edit "$ref"; then
		echo
		echo "Merge clean. Now verify:    $0 verify"
		echo "Then fold back:             git switch $PORT_BRANCH && git merge $syncbranch && git push"
	else
		echo
		echo "Merge has conflicts. Resolve them (PORTING.md maps guarded regions to"
		echo "their tags), then 'git merge --continue', then:    $0 verify"
	fi
}

verify() {
	echo "== ledger check =="
	bash "$HERE/check_porting_ledger.sh"
	echo "== build harness (Makefile) =="
	make -C "$HERE"
	echo "== run the .rb test suite =="
	local fail=0
	for f in "$HERE"/*_test.rb; do
		if ! timeout 120 "$HERE/love_mrb_harness" "$f" >/dev/null 2>&1; then
			echo "  FAIL $(basename "$f")"
			fail=1
		fi
	done
	# filesystem_mount_test.rb has a known fixture failure (ext/hello.txt); ignore.
	if [ "$fail" -eq 0 ]; then
		echo "All tests pass."
	else
		echo "Some tests failed (note: filesystem_mount_test.rb fails on a missing"
		echo "fixture regardless of upstream — compare against a pre-merge run)."
		return 1
	fi
}

case "$cmd" in
	status)  status ;;
	start)   start ;;
	verify)  verify ;;
	*) echo "usage: $0 {status|start [REF]|verify}" >&2; exit 2 ;;
esac
