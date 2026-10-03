#!/bin/zsh
set -uo pipefail

# Usage: scripts/lint-changed.sh [--base <rev>] [path ...]
# The T2 lint (docs/VERIFICATION_SPEED.md): clang-tidy (.clang-tidy, pinned through .pre-commit-config.yaml) on the
# changed framework and plugin sources and on every such source that includes a changed header, since the commit
# hook only sees staged sources; clang-format (report only) on newly added files, which must follow .clang-format.
# Changed files are the given paths, or everything differing from <rev> (default HEAD) including untracked files.
# Needs the dev build's compile database (cmake --preset dev).
base=HEAD
changed=()
while (( $# > 0 )); do
	case $1 in
		--base) base=$2; shift ;;
		-h|--help) sed -n '4,9p' "$0"; exit 0 ;;
		*) changed+=("$1") ;;
	esac
	shift
done

cd "${0:A:h}/.."
if (( ${#changed} == 0 )); then
	changed=(${(f)"$(git diff --name-only "$base"; git ls-files --others --exclude-standard)"})
fi
added=(${(f)"$(git diff --name-only --diff-filter=A "$base"; git ls-files --others --exclude-standard)"})

typeset -U sources
headers=()
for file in $changed; do
	[[ -f $file ]] || continue
	case $file in
		framework/*.cpp|framework/*.mm|plugins/*.cpp|plugins/*.mm) sources+=("$file") ;;
		framework/*.h|plugins/*.h) headers+=("$PWD/$file") ;; # tests and tools are not linted
	esac
done
if (( ${#headers} > 0 )); then
	# Every framework or plugin source whose object depends on a changed header, directly or through other headers,
	# from the dev build's recorded dependencies (ninja -t deps) and compile database. Without them (no dev build yet),
	# or for a header no built object includes yet, every framework and plugin source is linted instead.
	dependents=$(ninja -C build/dev -t deps 2>/dev/null | python3 -c '
import json, os, sys
headers = set(sys.argv[1:])
root = os.getcwd()
objects, current, recorded = set(), None, set()
for line in sys.stdin:
    if line and not line[0].isspace():
        current = os.path.normpath(os.path.join(root, "build/dev", line.split(":")[0].strip()))
    elif current:
        dependency = os.path.normpath(os.path.join(root, "build/dev", line.strip()))
        recorded.add(dependency)
        if dependency in headers:
            objects.add(current)
if not recorded or not headers <= recorded:
    sys.exit(2)
for entry in json.load(open("build/dev/compile_commands.json")):
    source = os.path.relpath(entry["file"], root)
    output = os.path.normpath(os.path.join(entry["directory"], entry.get("output", "")))
    if output in objects and source.startswith(("framework/", "plugins/")):
        print(source)
' $headers) || {
		print "Header dependencies unavailable or incomplete in build/dev; linting every framework and plugin source"
		dependents=$(git ls-files 'framework/*.cpp' 'framework/*.mm' 'plugins/*.cpp' 'plugins/*.mm')
	}
	sources+=(${(f)dependents})
fi

failed=0
if (( ${#sources} > 0 )); then
	print "clang-tidy on ${#sources} source(s)"
	pre-commit run clang-tidy --files $sources || failed=1
else
	print "clang-tidy: no framework or plugin source affected"
fi

typeset -U formatted
for file in $added; do
	[[ -f $file && $file == (framework|plugins|tests|tools)/*.(cpp|h|mm) ]] && formatted+=("$file")
done
if (( ${#formatted} > 0 )); then
	print "clang-format (report only) on ${#formatted} new file(s)"
	pre-commit run --hook-stage manual clang-format --files $formatted || failed=1
fi
exit $failed
