#!/usr/bin/env bash
# The version of this build, worked out from git and the VERSION file (the one place the
# number lives). Meant to be sourced by deploy-td.sh, deploy-ra.sh and make-release.sh; run
# on its own it prints what it found.
#
# A *release* build is made from a clean tree whose HEAD is exactly the commit that the
# annotated tag riscos-v<VERSION> points at; its strings say just "0.1.0". Anything else (another
# commit, uncommitted changes, no tag yet) is a dev build, and says "0.1.0-dev" with the
# `git describe` text, so it can never be mistaken for the release.
#
# Sets:
#   VC_VERSION_BASE      the VERSION file, e.g. 0.1.0
#   VC_RELEASE           1 for a release build, else 0
#   VC_RELEASE_WHY       why it isn't a release (when VC_RELEASE is 0)
#   VC_VERSION           0.1.0, or 0.1.0-dev
#   VC_VERSION_DATE      the commit's date, e.g. 20 Oct 2026 (so a rebuild says the same)
#   VC_VERSION_DESCRIBE  git describe text for a dev build (riscos-v0.1.0-3-gabc1234-dirty), else empty
#   VC_VERSION_FULL      what !Help and the log say after the game's name, e.g. "0.1.0 (20 Oct 2026)"
#   VC_CMAKE_VERSION     the -D options that give the game these (as a word list; no spaces in them)
# and defines vc_stamp_help <file>, which fills @VERSION@ in a !Help copy.
VC_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
VC_VERSION_BASE=$(tr -d ' \r\n' <"$VC_ROOT/VERSION")
if ! [[ $VC_VERSION_BASE =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: $VC_ROOT/VERSION must hold a semantic version like 0.1.0, not '$VC_VERSION_BASE'" >&2
    return 1 2>/dev/null || exit 1
fi

VC_RELEASE=0
VC_RELEASE_WHY=
# The port's tags are riscos-v<VERSION>: upstream Vanilla Conquer has its own v0.1.0 (and similar) tags.
VC_TAG=riscos-v$VC_VERSION_BASE
vc_dirty=$(git -C "$VC_ROOT" status --porcelain 2>/dev/null | head -1)
VC_VERSION_DESCRIBE=$(git -C "$VC_ROOT" describe --tags --always --match 'riscos-v*' 2>/dev/null || echo unknown)
[ -n "$vc_dirty" ] && VC_VERSION_DESCRIBE=$VC_VERSION_DESCRIBE-dirty
VC_VERSION_DATE=$(LC_ALL=C git -C "$VC_ROOT" log -1 --format=%cd --date='format:%-d %b %Y' HEAD 2>/dev/null || date '+%-d %b %Y')

if [ -n "$vc_dirty" ]; then
    VC_RELEASE_WHY="the working tree has uncommitted or untracked files (git status --porcelain shows them)"
elif [ "$(git -C "$VC_ROOT" cat-file -t "refs/tags/$VC_TAG" 2>/dev/null)" != tag ]; then
    VC_RELEASE_WHY="there is no annotated tag $VC_TAG (create it with: git tag -a $VC_TAG -m 'Vanilla Conquer RISC OS $VC_VERSION_BASE')"
elif [ "$(git -C "$VC_ROOT" rev-parse "$VC_TAG^{commit}")" != "$(git -C "$VC_ROOT" rev-parse HEAD)" ]; then
    VC_RELEASE_WHY="HEAD ($(git -C "$VC_ROOT" rev-parse --short HEAD)) is not the commit tagged $VC_TAG ($(git -C "$VC_ROOT" rev-parse --short "$VC_TAG^{commit}"))"
else
    VC_RELEASE=1
fi

if [ "$VC_RELEASE" = 1 ]; then
    VC_VERSION=$VC_VERSION_BASE
    VC_VERSION_DESCRIBE=
    VC_VERSION_FULL="$VC_VERSION ($VC_VERSION_DATE)"
else
    VC_VERSION=$VC_VERSION_BASE-dev
    VC_VERSION_FULL="$VC_VERSION $VC_VERSION_DESCRIBE ($VC_VERSION_DATE)"
fi
# Spaces are kept out of the options (they pass through a shell in the build container).
VC_CMAKE_VERSION="-DVC_VERSION=$VC_VERSION -DVC_VERSION_DATE=${VC_VERSION_DATE// /_} -DVC_VERSION_DESCRIBE=${VC_VERSION_DESCRIBE:-none}"

# Puts the version on the first line of a !Help copy (the source has @VERSION@ there).
vc_stamp_help() {
    LC_ALL=C sed -i.bak "s|@VERSION@|$VC_VERSION_FULL|" "$1" && rm -f "$1.bak"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "VERSION file:  $VC_VERSION_BASE"
    echo "release build: $VC_RELEASE ${VC_RELEASE_WHY:+($VC_RELEASE_WHY)}"
    echo "version:       $VC_VERSION"
    echo "date:          $VC_VERSION_DATE"
    echo "describe:      ${VC_VERSION_DESCRIBE:-(none)}"
    echo "full:          $VC_VERSION_FULL"
fi
