#!/bin/sh
# unreleased.sh - what is finished here but not yet in anybody's hands?
#
# WHY THIS EXISTS.  This repository has no store.  Its channel is GitHub
# releases, and its OTHER channel is not a channel at all: z80cpmw's vcxproj
# compiles sources straight out of a sibling checkout of this tree, and every
# repository in the family calls util/cpm_disk.py out of this one.  So a commit
# under src/ or util/ reaches its readers on their next build with no release
# of anything, while packaging changes reach nobody until a release is cut.
# Those two travel at different speeds and the difference is what this reports.
#
# THIS IS ONE OF SIX AND THEY ARE DELIBERATELY DIFFERENT.  Every repository in
# the family ships on its own channel, so each unreleased.sh is written for its
# own rather than copied.  check-shipped-disks.sh was "one file in five repos",
# diverged into four that no two of which agreed, and "the check passed" came
# to mean four different things.  This repository deleted its copy of that
# script on 2026-09-10 for having nothing to check; do not recreate the pattern.
#
#   sh util/unreleased.sh
#
# HOW THIS RUNS: BY HAND, AND IT MUST STAY THAT WAY.  Not wired to any
# workflow, and the exit codes are shaped so it cannot usefully become one.
#
# Exit 0 = it measured, INCLUDING when the answer is "six commits unreleased".
#          That is the normal state of a working repository.  Four jobs in this
#          family went red daily for the normal state and all four were deleted
#          on 2026-09-13; do not rebuild one out of this.
# Exit 2 = could not measure.  Nothing is asserted when nothing was read.
#
# There is no exit 1.

set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here" && git rev-parse --show-toplevel 2>/dev/null) || {
    echo "CANNOT MEASURE: $here is not inside a git checkout."; exit 2; }

echo "cpmemu - GitHub releases, and the ports that compile this tree in place"
echo

if ! command -v gh >/dev/null 2>&1; then
    echo "  CANNOT MEASURE: gh is not installed."
    exit 2
fi

published=$(gh release list --repo avwohl/cpmemu --limit 1 \
                --json tagName,isLatest --jq '.[] | select(.isLatest) | .tagName' 2>/dev/null)
if [ -z "${published:-}" ]; then
    echo "  CANNOT MEASURE: no release marked Latest, or the API is unreachable."
    exit 2
fi
assets=$(gh release view "$published" --repo avwohl/cpmemu \
             --json assets --jq '.assets | length' 2>/dev/null)

echo "  newest release    $published  (${assets:-0} asset(s))"
[ "${assets:-0}" = "0" ] &&
    echo "                    ^ a release with no assets installs nothing."

if ! git -C "$root" rev-parse --verify --quiet "$published^{commit}" >/dev/null 2>&1; then
    echo "  $published is not a tag in this checkout - fetch, then re-run."
    exit 2
fi
echo

n=$(git -C "$root" rev-list --count "$published..HEAD" 2>/dev/null)
if [ "${n:-0}" = "0" ]; then
    echo "  Nothing since $published.  The tree is the release."
else
    echo "  $n commit(s) in the tree and not in $published:"
    echo
    git -C "$root" log --format='    %h  %ad  %s' --date=short "$published..HEAD"
    echo
    src_n=$(git -C "$root" rev-list --count "$published..HEAD" -- src/ util/ 2>/dev/null)
    echo "  Of those, $src_n touch src/ or util/."
    if [ "${src_n:-0}" != "0" ]; then
        git -C "$root" log --format='      %h  %s' "$published..HEAD" -- src/ util/
        echo
        echo "  Those do not travel by release.  z80cpmw's vcxproj compiles this"
        echo "  tree in place from a sibling checkout, and util/cpm_disk.py is"
        echo "  the family's CP/M image tool that every repository here calls"
        echo "  out of this one - so they reach their readers on the next build,"
        echo "  tag or no tag."
    fi
fi
echo

exit 0
