#!/bin/bash
# The PC builds the crossplay modes play against (docs/crossplay.md, task T8
# and section 10): OpenCE, ChupathingyCE or DamnationCE (all CC0) built from
# a pinned commit of a checkout, as each project's release Linux build
# (configure.py --release --pgo=off --lto=off, ninja linux), 32-bit, with our
# clang-sdl32 compiler (a clang that finds a 32-bit SDL 3: --linux-cc).
# The checkout is only read (git archive of the commit): nothing is built in
# it, nothing fetched into it.
#
#   build_pc_crossplay.sh PROJECT CHECKOUT COMMIT [OUT]
#
#   PROJECT   opence | chupa | damnation
#   CHECKOUT  the project's git checkout (any branch: the commit is what counts)
#   COMMIT    the commit to build (a hash, tag or branch name; resolved and
#             recorded as a full hash)
#   OUT       an empty or new folder for the build (default: a new
#             mktemp -d ~/halo-test-out/xppc-PROJECT.XXXX)
#
# Writes OUT/src (the commit's tree, built there), and OUT/BUILD_INFO: the
# project, the full commit, the checkout, the compiler, configure's
# arguments, the targets, every binary (path, sha256) and the patches applied
# (none: a patch here would mean we no longer test what PC players run).
# The binaries: OUT/src/build/linux/halo (every project), and ChupathingyCE's
# dedicated server OUT/src/build/server-x86/chupathingyce-server (ninja
# server-x86). DamnationCE's dedicated server is its game binary with
# HALO_DEDICATED=<playlist> (server/README.md); OpenCE has none.
#
#   XPPC_CC     the compiler (default ../clang-sdl32 beside this repository's
#               worktrees: ~/github/halo-ce-vita-spike/clang-sdl32)
#   XPPC_SDL3   the 32-bit SDL 3 install whose headers ChupathingyCE's server
#               build uses instead of downloading SDL's release (default
#               ~/github/halo-ce-vita-spike/sdl3-i686; it must be SDL 3.4.16,
#               the release that build pins)
#   XPPC_JOBS   ninja's jobs (default: the CPUs)
#
# Exit status 0 built, 1 a build failed, 2 bad arguments.
set -eu
project=${1:-}
checkout=${2:-}
commit=${3:-}
out=${4:-}
spike=${XPPC_SPIKE:-$HOME/github/halo-ce-vita-spike}
cc=${XPPC_CC:-$spike/clang-sdl32}
sdl3=${XPPC_SDL3:-$spike/sdl3-i686}
jobs=${XPPC_JOBS:-$(nproc)}

usage() { sed -n '/^#   build_pc_crossplay.sh/,/^#   OUT/p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
case $project in opence|chupa|damnation) ;; *) usage ;; esac
[ -n "$checkout" ] && [ -n "$commit" ] || usage
[ -d "$checkout" ] || { echo "no checkout at $checkout" >&2; exit 2; }
full=$(git -C "$checkout" rev-parse --verify --quiet "$commit^{commit}") ||
	{ echo "$checkout has no commit $commit" >&2; exit 2; }
[ -x "$cc" ] || { echo "no compiler at $cc (XPPC_CC)" >&2; exit 2; }
if [ -z "$out" ]; then
	mkdir -p "$HOME/halo-test-out"
	out=$(mktemp -d "$HOME/halo-test-out/xppc-$project.XXXX")
fi
mkdir -p "$out"
out=$(cd "$out" && pwd)
[ -e "$out/src" ] && { echo "$out/src is there already: give a new folder" >&2; exit 2; }

echo "building $project $full from $checkout in $out"
mkdir "$out/src"
git -C "$checkout" archive --format=tar "$full" | tar -x -C "$out/src"
targets=linux
[ "$project" = chupa ] && targets="linux server-x86"
if [ "$project" = chupa ]; then
	# (its server build fetches SDL 3.4.16's release once, for the headers
	# alone: tools/server_build.py; ours are that release's, put where it
	# looks, so the build reaches nothing)
	version=$(sed -n 's/^SDL_VERSION = "\(.*\)"/\1/p' "$out/src/tools/server_build.py")
	have=$(awk '/#define SDL_(MAJOR|MINOR|MICRO)_VERSION/ { printf "%s%s", sep, $3; sep = "." }' \
		"$sdl3/include/SDL3/SDL_version.h" 2>/dev/null || true)
	if [ -n "$version" ] && [ "$version" = "$have" ]; then
		headers=$out/src/build/server/third_party/SDL3-$version
		mkdir -p "$headers"
		cp -r "$sdl3/include" "$headers/include"
		printf 'SDL %s headers, copied from %s (zlib licence)\n' "$version" "$sdl3" > "$headers/LICENSE.txt"
	else
		echo "warning: the server wants SDL $version's headers, $sdl3 has $have: it will try to download them" >&2
	fi
fi
configure="--release --pgo=off --lto=off --linux-cc $cc"
status=0
(cd "$out/src" && python3 configure.py $configure) > "$out/configure.log" 2>&1 || status=$?
if [ $status = 0 ]; then
	(cd "$out/src" && ninja -j "$jobs" $targets) > "$out/build.log" 2>&1 || status=$?
fi
binaries="build/linux/halo"
[ "$project" = chupa ] && binaries="$binaries build/server-x86/chupathingyce-server"
{
	echo "project: $project"
	echo "commit: $full"
	echo "describe: $(git -C "$checkout" describe --always --tags "$full" 2>/dev/null || echo "$full")"
	echo "subject: $(git -C "$checkout" log -1 --format=%s "$full")"
	echo "checkout: $(cd "$checkout" && pwd)"
	echo "compiler: $cc ($("$cc" --version 2>/dev/null | head -1))"
	echo "configure: python3 configure.py $configure"
	echo "targets: ninja $targets"
	echo "patches: none"
	echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "status: $([ $status = 0 ] && echo ok || echo "failed ($status)")"
	for b in $binaries; do
		if [ -x "$out/src/$b" ]; then
			echo "binary: $out/src/$b $(sha256sum "$out/src/$b" | cut -d' ' -f1)"
		else
			echo "binary: $out/src/$b missing"
		fi
	done
} > "$out/BUILD_INFO"
cat "$out/BUILD_INFO"
if [ $status != 0 ]; then
	echo "the build failed: $out/configure.log, $out/build.log" >&2
	tail -20 "$out/build.log" 2>/dev/null || tail -20 "$out/configure.log" >&2
	exit 1
fi
for b in $binaries; do [ -x "$out/src/$b" ] || { echo "$b was not built" >&2; exit 1; }; done
file "$out/src/build/linux/halo" | grep -q "ELF 32-bit" || { echo "build/linux/halo is not a 32-bit build" >&2; exit 1; }
echo "built: $out"
