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
# THIS COPY HAS DIVERGED and the other four need the same edit.  cpmdroid no
# longer pins an ioscpm release tag: it fetches romwbw_disks' index-v0.json and
# takes every URL out of the documents it names.  So `pin_of` finds no vX.Y.Z
# in its source, and until this change the cpmdroid row printed NO PIN FOUND and
# exited 1 for a port that was working correctly - a gate that cries wolf is a
# gate that stops being read, which is the exact failure this script was written
# after.  Migrated ports are now checked differently (kind "index-v0" in the
# table below), and the question changes with them: not "does the pin name the
# newest tag" but "does the source still name the v0 index, is the legacy pin
# really gone, and does that index still publish the RomWBW release this build's
# bundled ROM declares".
#
# That last one still matters after cpmdroid started fetching ROMs from the
# catalog (1.28), but it means something narrower than it did.  A user can now
# select a published release and download its ROM, so an unpublished bundled
# release no longer strands them - it strands the OFFLINE first launch, which is
# the one path with no catalog in reach.  That is still a failure worth exiting
# 1 for, and the message says which one it is.
#
# ioscpm and z80cpmw HAVE now migrated and moved to that row, on 2026-09-06.
# Both were still listed as `tag` here, so both printed NO PIN FOUND and this
# script exited 1 for three ports that were all working correctly - the whole
# family failing a gate for doing the right thing, which is worse than the gate
# not existing. Note z80cpmw's file: the v0 index URL is in CatalogV0.cpp, NOT
# in DiskCatalog.cpp, which is where the old pin lived and where this table
# still pointed.
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
# port | file relative to its checkout | grep pattern for the line | kind
#
# kind "tag"      - still pins an ioscpm release tag and downloads disks.xml.
# kind "index-v0" - migrated to romwbw_disks' two-level catalog; there is no tag
#                   in its source to compare, and looking for one is how this
#                   script would silently stop covering it.
ports='ioscpm|iOSCPM/Views/EmulatorViewModel.swift|indexURL[[:space:]]*=|index-v0
cpmdroid|app/src/main/java/com/awohl/cpmdroid/data/DiskCatalogRepository.kt|INDEX_URL[[:space:]]*=|index-v0
z80cpmw|z80cpmw/CatalogV0.cpp|INDEX_URL[[:space:]]*=|index-v0'

pin_of() { # $1 = port dir, $2 = file, $3 = pattern -> prints vX.Y.Z
    f="$1/$2"
    [ -f "$f" ] || return 1
    grep -E "$3" "$f" 2>/dev/null |
        grep -v '^[[:space:]]*[/*#]' |
        sed -n 's/.*"\(v[0-9][0-9.]*\)".*/\1/p' | head -1
}

# --- migrated ports -----------------------------------------------------------
# The whole file rather than one line: the URL is a multi-line constant in
# Kotlin, so grepping the line that names it finds the name and not the value.
# Comment lines are dropped for both of these, because the file that replaced
# the pin explains in prose what it replaced - "RELEASE_TAG = \"v1.4.12\"" is
# still written there, and matching it would report a leftover pin forever.
index_url_of() { # $1 = port dir, $2 = file -> prints the v0 index URL
    f="$1/$2"
    [ -f "$f" ] || return 1
    grep -v '^[[:space:]]*[/*#]' "$f" 2>/dev/null |
        sed -n 's|.*"\(https://[^"]*index-v0\.json\)".*|\1|p' | head -1
}

legacy_pin_in() { # $1 = port dir, $2 = file -> prints a vX.Y.Z still in the source
    f="$1/$2"
    [ -f "$f" ] || return 1
    grep -v '^[[:space:]]*[/*#]' "$f" 2>/dev/null |
        sed -n 's/.*"\(v[0-9]\{1,\}\.[0-9]\{1,\}\.[0-9]\{1,\}\)".*/\1/p' | head -1
}

# The RomWBW release a bundled ROM declares, read the way the emulator reads it:
# marker 'W' 0xA8 at 0x103/0x104, then ver and upd at 0x105/0x106, where
# ver = major<<4|minor and upd = update<<4|patch.  Asking the binary is the
# point - a build whose ROM was swapped without its catalog selection moving is
# exactly the drift this script exists to catch, and no constant in the source
# can report it.
rom_release_of() { # $1 = rom file -> prints e.g. 3.5.1
    [ -f "$1" ] || return 1
    command -v od >/dev/null 2>&1 || return 1
    marker=$(od -An -tx1 -j 259 -N 2 "$1" 2>/dev/null | tr -d ' \n')
    [ "$marker" = "57a8" ] || return 1
    # hex() by hand rather than strtonum(): that is a gawk extension and this
    # runs under whatever awk the machine has, mawk and BSD awk included.
    od -An -tx1 -j 261 -N 2 "$1" 2>/dev/null | tr -d '\n' |
        awk 'function hex(h,   i, r) { r = 0
                 for (i = 1; i <= length(h); i++)
                     r = r * 16 + index("0123456789abcdef", tolower(substr(h, i, 1))) - 1
                 return r }
             { v = hex($1); u = hex($2)
               major = int(v / 16); minor = v % 16
               update = int(u / 16); patch = u % 16
               if (patch == 0) printf "%d.%d.%d\n", major, minor, update
               else printf "%d.%d.%d.%d\n", major, minor, update, patch }'
}

# Which ROM a migrated port bundles.  One line per port so that adding the next
# one is an edit here rather than a new function.
bundled_rom_of() { # $1 = port name, $2 = checkout -> prints a path
    case "$1" in
        cpmdroid) echo "$2/app/src/main/assets/emu_avw.rom" ;;
        ioscpm)   echo "$2/iOSCPM/Resources/emu_avw.rom" ;;
        z80cpmw)  echo "$2/roms/emu_avw.rom" ;;
    esac
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

# Does a built artifact contain this pattern anywhere inside it?  For a migrated
# port the evidence is presence, not equality: a build made before the repoint
# carries the old tag and none of the new URL, so "does not name index-v0.json"
# is what identifies a stale package.  The variable is spat, not pat, because sh
# has no locals and pat belongs to the caller's read loop.
scan_artifact_for() { # $1 = file, $2 = ERE -> exit 0 when found
    spat="$2"
    case "$1" in
        *.msix|*.apk|*.zip|*.aab)
            command -v unzip >/dev/null 2>&1 || return 1
            rm -rf "$tmp/y"
            mkdir -p "$tmp/y"
            unzip -o -qq "$1" -d "$tmp/y" 2>/dev/null || { rm -rf "$tmp/y"; return 1; }
            rm -f "$tmp/y.hit"
            find "$tmp/y" -type f 2>/dev/null | while read -r m; do
                if grep -a -qE "$spat" "$m" 2>/dev/null ||
                   tr -d '\000' < "$m" 2>/dev/null | grep -a -qE "$spat" 2>/dev/null; then
                    : > "$tmp/y.hit"
                fi
            done
            rm -rf "$tmp/y"
            [ -f "$tmp/y.hit" ] ;;
        *)
            grep -a -qE "$spat" "$1" 2>/dev/null && return 0
            tr -d '\000' < "$1" 2>/dev/null | grep -a -qE "$spat" 2>/dev/null ;;
    esac
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
echo "$ports" | while IFS='|' read -r port file pat kind; do
    [ -n "$port" ] || continue
    dir="$SRC/$port"

    if [ ! -d "$dir" ]; then
        printf '%-10s NOT CHECKED OUT beside this repo - cannot verify its pin\n' "$port"
        echo 1 > "$tmp/fail"
        continue
    fi

    # --- migrated ports ---------------------------------------------------------
    # A different question, asked because the old one has no answer here.  All
    # four checks fail loudly; none of them can pass by finding nothing, which is
    # what a grep for a deleted constant does.
    if [ "$kind" = "index-v0" ]; then
        idx=$(index_url_of "$dir" "$file")
        if [ -z "$idx" ]; then
            printf '%-10s NO v0 INDEX URL in %s - repointed back, or renamed?\n' "$port" "$file"
            echo 1 > "$tmp/fail"
            continue
        fi

        leftover=$(legacy_pin_in "$dir" "$file")
        if [ -n "$leftover" ]; then
            printf '%-10s LEFTOVER PIN %s in %s alongside the v0 index\n' "$port" "$leftover" "$file"
            echo 1 > "$tmp/fail"
            continue
        fi

        rom=$(bundled_rom_of "$port" "$dir")
        romver=$(rom_release_of "$rom")
        if [ -z "$romver" ]; then
            printf '%-10s CANNOT READ the RomWBW release out of %s\n' "$port" "$rom"
            echo 1 > "$tmp/fail"
            continue
        fi

        if ! get "$idx" "$tmp/$port-index.json"; then
            printf '%-10s v0 INDEX UNREACHABLE: %s\n' "$port" "$idx"
            echo 1 > "$tmp/fail"
            continue
        fi

        # Whitespace stripped first so this does not depend on how the generator
        # happens to indent.  One field, not a pair, so it does not depend on
        # field order either.
        if tr -d ' \n' < "$tmp/$port-index.json" |
                grep -q "\"romwbw_version\":\"$romver\""; then
            printf '%-10s v0 index, bundled ROM RomWBW %s is published\n' "$port" "$romver"
        else
            printf '%-10s BUNDLED ROM IS RomWBW %s, WHICH THE v0 INDEX NO LONGER PUBLISHES\n' \
                   "$port" "$romver"
            printf '%-10s   a first launch with no network boots that ROM and can then\n' ""
            printf '%-10s   download nothing that matches it\n' ""
            echo 1 > "$tmp/fail"
            continue
        fi

        # And what shipped?  A build made before the migration still carries the
        # old tag and none of the new URL, so the presence of the index URL is
        # the thing to look for - an artifact that does not name it is stale.
        [ "$TREE_ONLY" = "1" ] && continue
        artifacts_for "$port" "$dir" 2>/dev/null | while read -r a; do
            [ -f "$a" ] || continue
            echo x >> "$tmp/scanned"
            if scan_artifact_for "$a" 'index-v0\.json'; then
                printf '%-10s   artifact %s names the v0 index, agrees with the tree\n' \
                       "" "$(basename "$a")"
            else
                printf '%-10s   ARTIFACT PREDATES THE MIGRATION: %s does not name index-v0.json\n' \
                       "" "$(basename "$a")"
                printf '%-10s   that artifact was built before the repoint. Rebuild before shipping.\n' ""
                echo 1 > "$tmp/fail"
            fi
        done
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
        echo x >> "$tmp/scanned"
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

# FOUND NOTHING AND CHECKED NOTHING MUST NOT READ THE SAME.  The whole stated
# reason this script exists is checking the built artifact, and artifacts_for()
# globs local build paths only - dist/, bin/Release/, app/build/outputs, build/,
# DerivedData.  On a machine that cannot build a given port, and that is every
# machine for at least two of the three, those paths are simply absent: the loop
# runs zero times, prints nothing, and the run used to end on "every artifact
# found agrees with its own tree".  True, and it reads as though the packages
# were inspected and passed.  The count below is what tells the two apart.
scanned=0
[ -f "$tmp/scanned" ] && scanned=$(wc -l < "$tmp/scanned" | tr -d ' ')

echo "Every port's tree names the current catalog."
if [ "$TREE_ONLY" = "1" ]; then
    echo
    echo "NO PACKAGE WAS INSPECTED: --tree-only was given, so the half of this"
    echo "check that looks at what users actually got did not run.  The trees"
    echo "being right is not the same as the artifacts being right - that is the"
    echo "gap z80cpmw 1.0.23 went out through."
elif [ "$scanned" -gt 0 ]; then
    echo "All $scanned artifact(s) found agree with their own tree."
else
    echo
    echo "NO PACKAGE WAS INSPECTED, and this is NOT a pass of the artifact half."
    echo "artifacts_for() found nothing under dist/, bin/Release/,"
    echo "app/build/outputs, build/ or DerivedData in any port, which is the"
    echo "normal state on a machine that cannot build them - MSVC, the Android"
    echo "SDK and Xcode are three different machines.  Nothing here has looked"
    echo "at a byte any user will run.  Re-run this where a package was just"
    echo "built, or unpack a published one by hand."
fi
exit 0
