#!/bin/sh
# check-shipped-disks.sh - does the disk image users actually download carry the
# current R8/W8?
#
# WHY THIS EXISTS.  On 2026-09-03 the R8 wildcard-erasure bug was fixed in
# romwbw_emu/src/r8.asm, built into a published ioscpm release (v1.4.12), and
# reached NO USER OF ANY PORT.  Every repository was telling the truth about its
# own layer - the source was fixed, the image was published, and each port's
# catalog pin still named an older release - and no check spanned the layers, so
# the same "it is fixed" / "no it is not" exchange ran five times.
#
# The specific failure this is built to catch happened here, in z80cpmw: the pin
# was bumped in the tree, the changelog and the commit both said in as many words
# that the packages predated it, and 1.0.23 shipped with the old pin anyway.  A
# note that has to be read at the right moment is not a gate.  So this checks the
# BUILT ARTIFACT as well as the tree, because that is the gap that won.
#
# Copy: this script is identical in cpmemu, romwbw_emu, cpmdroid, ioscpm and
# z80cpmw and answers the same question in all five.  It checks every port, not
# just the one it is sitting in, so no repository can report "fixed" while its
# neighbour's users are on an old pin.  Edit one, copy to the rest.
#
#   sh check-shipped-disks.sh              tree pins + any artifacts found
#   sh check-shipped-disks.sh --tree-only  skip artifact scanning
#
# Exit 0 = every port's pin names the newest published release, and every
#          artifact found agrees with its own tree.
# Exit 1 = a port is behind, or a built artifact disagrees with its tree.
# Exit 2 = could not verify (no network, no parser).  A gate that cannot verify
#          must not say yes.

set -u

CATALOG_REPO="avwohl/ioscpm"
API="https://api.github.com/repos/$CATALOG_REPO/releases"
DL="https://github.com/$CATALOG_REPO/releases/download"

TREE_ONLY=0
[ "${1:-}" = "--tree-only" ] && TREE_ONLY=1

fail=0
tmp=$(mktemp -d 2>/dev/null || mktemp -d -t pins)
trap 'rm -rf "$tmp"' EXIT INT TERM

# --- where the sibling checkouts are ------------------------------------------
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here" && git rev-parse --show-toplevel 2>/dev/null) || root="$here"
SRC=$(dirname "$root")

# --- fetching -----------------------------------------------------------------
get() { # $1 url, $2 dest
    if command -v curl >/dev/null 2>&1; then
        curl -sSfL -o "$2" "$1" 2>/dev/null
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1" 2>/dev/null
    else
        return 127
    fi
}

# --- parsing (awk, not GNU sed: these repos are read on Windows and macOS too) --
combo_sha() { # $1 = disks.xml
    tr -d '\r\n' < "$1" | awk '{
        n = split($0, part, "<disk>")
        for (i = 1; i <= n; i++)
            if (part[i] ~ /hd1k_combo\.img/ &&
                match(part[i], /<sha256>[0-9a-f]+<\/sha256>/)) {
                print substr(part[i], RSTART + 8, RLENGTH - 17); exit
            }
    }'
}

highest_version() { # tags on stdin
    awk '
    function vnum(s,   a, n, i, r) {
        sub(/^v/, "", s); n = split(s, a, "."); r = 0
        for (i = 1; i <= 4; i++) r = r * 1000 + (i <= n ? a[i] + 0 : 0)
        return r
    }
    { v = vnum($0); if (v > best) { best = v; bt = $0 } }
    END { if (bt != "") print bt }'
}

# --- the ports that pin a catalog ---------------------------------------------
# port | file relative to its checkout | grep pattern for the line
ports='ioscpm|iOSCPM/Views/EmulatorViewModel.swift|releaseTag[[:space:]]*=
cpmdroid|app/src/main/java/com/awohl/cpmdroid/data/DiskCatalogRepository.kt|RELEASE_TAG[[:space:]]*=
z80cpmw|z80cpmw/DiskCatalog.cpp|RELEASE_TAG[[:space:]]*='

pin_of() { # $1 = port dir, $2 = file, $3 = pattern -> prints vX.Y.Z
    f="$1/$2"
    [ -f "$f" ] || return 1
    grep -E "$3" "$f" 2>/dev/null |
        grep -v '^[[:space:]]*[/*#]' |
        sed -n 's/.*"\(v[0-9][0-9.]*\)".*/\1/p' | head -1
}

# --- artifacts: what users actually got ---------------------------------------
# A pin is a wide string on Windows and a UTF-8 one elsewhere, so look for both.
scan_artifact() { # $1 = file -> prints every vN.N.N found
    f="$1"
    case "$f" in
        *.msix|*.apk|*.zip|*.aab)
            command -v unzip >/dev/null 2>&1 || return 1
            unzip -o -qq "$f" -d "$tmp/x" 2>/dev/null || return 1
            find "$tmp/x" -type f 2>/dev/null | while read -r m; do scan_bytes "$m"; done
            rm -rf "$tmp/x" ;;
        *) scan_bytes "$f" ;;
    esac
}

scan_bytes() {
    # Both encodings, always.  z80cpmw's RELEASE_TAG is a std::wstring, so it
    # sits in the binary as UTF-16LE and a plain byte grep does not see it - the
    # first version of this script silently skipped the one artifact that was
    # wrong.  Stripping NULs collapses ASCII UTF-16LE to ASCII, which is enough
    # to find a tag and needs no strings(1).
    {
        grep -a -oE 'v1\.[0-9]+\.[0-9]+' "$1" 2>/dev/null
        tr -d '\000' < "$1" 2>/dev/null | grep -a -oE 'v1\.[0-9]+\.[0-9]+' 2>/dev/null
    }
}

artifacts_for() { # $1 = port name, $2 = checkout
    # Only artifacts for the version the tree currently claims.  An older
    # package SHOULD carry an older pin - it was right when it was built - and
    # failing on those would make this noisy enough to be ignored, which is how
    # the last check stopped being read.
    case "$1" in
        z80cpmw)
            v=$(awk '/^[[:space:]]*#define[[:space:]]+VERSION_(MAJOR|MINOR|PATCH)[[:space:]]/ {print $3}' \
                    "$2/z80cpmw/Version.h" 2>/dev/null | paste -sd. - 2>/dev/null)
            # z80cpmw.msix is the Store package and carries no version in its
            # name, so it is always a candidate: it is whatever was built last.
            ls "$2/dist/z80cpmw.msix" 2>/dev/null
            [ -n "$v" ] && ls "$2/dist/z80cpmw-$v-beta.msix" 2>/dev/null
            ls "$2/bin/Release/z80cpmw.exe" 2>/dev/null ;;
        cpmdroid)
            # build/outputs is overwritten by each build, so what is there is current.
            find "$2/app/build/outputs" \( -name '*.apk' -o -name '*.aab' \) 2>/dev/null ;;
        ioscpm)
            find "$2/build" "$2/DerivedData" -name '*.app' -prune 2>/dev/null ;;
    esac
}

echo "Disk catalog pins vs what $CATALOG_REPO publishes"
echo

# --- newest published release --------------------------------------------------
if ! get "$API?per_page=100" "$tmp/rel.json"; then
    echo "CANNOT VERIFY: no network, or neither curl nor wget is installed."
    echo "This gate does not pass when it cannot check."
    exit 2
fi

newest=$(grep -o '"tag_name"[[:space:]]*:[[:space:]]*"[^"]*"' "$tmp/rel.json" |
         sed 's/.*"\([^"]*\)"$/\1/' | highest_version)

if [ -z "$newest" ]; then
    echo "CANNOT VERIFY: could not read a release tag out of the GitHub API."
    echo "Rate-limited (60/hr unauthenticated), or the response shape changed."
    exit 2
fi

if ! get "$DL/$newest/disks.xml" "$tmp/newest.xml"; then
    echo "CANNOT VERIFY: $newest publishes no disks.xml."
    exit 2
fi
newest_sha=$(combo_sha "$tmp/newest.xml")
if [ -z "$newest_sha" ]; then
    echo "CANNOT VERIFY: no hd1k_combo.img sha256 in $newest's disks.xml."
    exit 2
fi

echo "newest published release: $newest"
echo "  hd1k_combo.img          $(echo "$newest_sha" | cut -c1-16)..."
echo

# --- each port -----------------------------------------------------------------
echo "$ports" | while IFS='|' read -r port file pat; do
    [ -n "$port" ] || continue
    dir="$SRC/$port"

    if [ ! -d "$dir" ]; then
        printf '%-10s NOT CHECKED OUT beside this repo - cannot verify its pin\n' "$port"
        echo 1 > "$tmp/fail"
        continue
    fi

    pin=$(pin_of "$dir" "$file" "$pat")
    if [ -z "$pin" ]; then
        printf '%-10s NO PIN FOUND in %s\n' "$port" "$file"
        echo 1 > "$tmp/fail"
        continue
    fi

    # What does that pin actually serve?  Compare the image, not the tag: two
    # tags can carry identical bytes (v1.4.5 and v1.4.11 do), and a port on an
    # older tag serving the same image is not behind in any way a user can feel.
    if get "$DL/$pin/disks.xml" "$tmp/$port.xml"; then
        sha=$(combo_sha "$tmp/$port.xml")
    else
        sha=""
    fi

    if [ -z "$sha" ]; then
        printf '%-10s pin %-9s BUT THAT TAG SERVES NO CATALOG - users get a 404\n' "$port" "$pin"
        echo 1 > "$tmp/fail"
    elif [ "$sha" = "$newest_sha" ]; then
        printf '%-10s pin %-9s current (same hd1k_combo.img as %s)\n' "$port" "$pin" "$newest"
    else
        printf '%-10s pin %-9s BEHIND - serves a different hd1k_combo.img than %s\n' \
               "$port" "$pin" "$newest"
        printf '%-10s   users of this port download %s...\n' "" "$(echo "$sha" | cut -c1-16)"
        printf '%-10s   the current image is        %s...\n' "" "$(echo "$newest_sha" | cut -c1-16)"
        printf '%-10s   fix: set the pin in %s\n' "" "$file"
        echo 1 > "$tmp/fail"
    fi

    # --- and what shipped? ------------------------------------------------------
    # The tree being right is not the same as the artifact being right, which is
    # exactly how z80cpmw 1.0.23 went out with the old pin while its source had
    # the new one.
    [ "$TREE_ONLY" = "1" ] && continue
    artifacts_for "$port" "$dir" 2>/dev/null | while read -r a; do
        [ -f "$a" ] || continue
        found=$(scan_artifact "$a" | sort -u | tr '\n' ' ')
        case " $found " in
            *" $pin "*)
                printf '%-10s   artifact %s carries %s, agrees with the tree\n' \
                       "" "$(basename "$a")" "$pin" ;;
            "  ")
                : ;;  # nothing version-shaped in it; not evidence either way
            *)
                printf '%-10s   ARTIFACT DISAGREES: %s carries [%s], tree says %s\n' \
                       "" "$(basename "$a")" "$(echo "$found" | sed 's/ *$//')" "$pin"
                printf '%-10s   that artifact was built before the pin moved. Rebuild before shipping.\n' ""
                echo 1 > "$tmp/fail" ;;
        esac
    done
done

echo
if [ -f "$tmp/fail" ]; then
    echo "A port serves an image older than the newest published one, or shipped a"
    echo "binary built before its own pin moved."
    echo
    echo "Bumping a pin is not shipping it: the edit reaches users only in a release"
    echo "that carries it. Check this again after building, not only after editing."
    exit 1
fi

echo "Every port serves the current hd1k_combo.img, and every artifact found"
echo "agrees with its own tree."
exit 0
