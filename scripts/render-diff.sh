#!/bin/zsh
set -uo pipefail

# Usage: scripts/render-diff.sh [--preset dev|dev-opt] <base-revision>
#        scripts/render-diff.sh --clean
# Proves a change leaves the sound alone (docs/VERIFICATION_SPEED.md): renders the corpus
# (tests/compat/RenderCorpusTests.cpp) with <base-revision> and with this working tree, uncommitted changes included,
# and compares every case byte for byte. Prints each case as identical or with its worst difference, and exits 0 only
# if all are identical. The base builds in a worktree cached under build/render-diff/<commit> (the first run is a cold
# build; --clean removes the cached worktrees). A base without this corpus gets this one compiled in, so any revision
# whose processors take the same parameter IDs can be compared. Both builds use the same preset (default dev-opt);
# compare in dev too when a change could round differently only without optimisation.
preset=dev-opt
base=
clean=0
while (( $# > 0 )); do
	case $1 in
		--preset) (( $# >= 2 )) || { print -u2 -- "--preset needs dev or dev-opt"; exit 64; }; preset=$2; shift ;;
		--clean) clean=1 ;;
		-h|--help) sed -n '4,13p' "$0"; exit 0 ;;
		-*) print -u2 "Unknown option $1"; exit 64 ;;
		*) base=$1 ;;
	esac
	shift
done

root=${0:A:h:h}
cache=$root/build/render-diff
cd "$root"

if (( clean )); then
	for directory in $cache/*(N/); do
		[[ -e $directory/.git ]] && git worktree remove --force "$directory"
	done
	rm -rf "$cache"
	git worktree prune
	print "Removed $cache"
	exit 0
fi
[[ $preset == dev || $preset == dev-opt ]] || { print -u2 -- "--preset is dev or dev-opt"; exit 64; }
[[ -n $base ]] || { print -u2 "Usage: $0 [--preset dev|dev-opt] <base-revision>"; exit 64; }
commit=$(git rev-parse --verify --quiet "$base^{commit}") || { print -u2 "Unknown revision $base"; exit 64; }

corpus=tests/compat/RenderCorpusTests.cpp
baseTree=$cache/$commit
# A base is ready once its JUCE checkout completed (the marker); anything less is recreated.
if [[ ! -e $baseTree/.render-diff-ready ]]; then
	print "Creating the base worktree for ${commit:0:9} in $baseTree"
	[[ -e $baseTree/.git ]] && git worktree remove --force "$baseTree"
	rm -rf "$baseTree"
	git worktree prune
	mkdir -p "$cache"
	git worktree add --detach "$baseTree" "$commit" > /dev/null || exit 1
	git -C "$baseTree" submodule update --init --reference "$root/external/JUCE" external/JUCE > /dev/null || exit 1
	touch "$baseTree/.render-diff-ready"
fi
# The same corpus on both sides: this tree's, compiled into the base if it lacks it or has another version.
if ! cmp -s "$corpus" "$baseTree/$corpus"; then
	print "Using this tree's corpus in the base"
	cp "$corpus" "$baseTree/$corpus"
fi
grep -q 'RenderCorpusTests.cpp' "$baseTree/tests/CMakeLists.txt" \
	|| print '\ntarget_sources(vekt_dsp_tests PRIVATE compat/RenderCorpusTests.cpp)' >> "$baseTree/tests/CMakeLists.txt"

output=$cache/renders-$preset
rm -rf "$output"
renderTree() # <source tree> <base|head>
{
	print "Building and rendering $( [[ $2 == base ]] && print "the base ${commit:0:9}" || print "this tree" ) ($preset)"
	( cd "$1" && cmake --preset "$preset" > "$output-$2.log" 2>&1 \
		&& cmake --build --preset "$preset" --target vekt_dsp_tests >> "$output-$2.log" 2>&1 ) \
		|| { print -u2 "Build failed, see $output-$2.log"; return 1; }
	VEKT_RENDER_DIR="$output/$2" "$1/build/$preset/tests/vekt_dsp_tests" "[.render-corpus]" >> "$output-$2.log" 2>&1 \
		|| { print -u2 "Rendering failed, see $output-$2.log"; return 1; }
}
mkdir -p "$output"
renderTree "$baseTree" base || exit 1
renderTree "$root" head || exit 1

python3 - "$output/base" "$output/head" <<'EOF'
import array, math, os, sys
base, head = sys.argv[1], sys.argv[2]
names = sorted(set(os.listdir(base)) | set(os.listdir(head)))
if not names:
    print("No renders to compare")
    sys.exit(1)
different = 0
for name in names:
    label = name.removesuffix(".f32")
    if name not in os.listdir(base) or name not in os.listdir(head):
        print(f"MISSING    {label} (only in {'head' if name in os.listdir(head) else 'base'})")
        different += 1
        continue
    a, b = (open(os.path.join(d, name), "rb").read() for d in (base, head))
    if a == b:
        print(f"identical  {label}")
        continue
    different += 1
    x, y = array.array("f", a), array.array("f", b)
    if len(x) != len(y):
        print(f"DIFFERENT  {label}: length {len(x)} vs {len(y)} samples")
        continue
    worst, at = max((abs(p - q), i) for i, (p, q) in enumerate(zip(x, y)))
    frames = len(x) // 2
    print(f"DIFFERENT  {label}: worst {20 * math.log10(max(worst, 1e-30)):.1f} dBFS "
          f"(channel {at // frames}, frame {at % frames})")
print(f"\n{len(names) - different} of {len(names)} cases identical")
sys.exit(1 if different else 0)
EOF
