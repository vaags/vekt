#!/bin/zsh

vekt_require_fswatch()
{
    command -v fswatch >/dev/null || {
        print -u2 "fswatch is required for watch scripts. Install it with: brew install fswatch"
        return 69
    }
}

vekt_run_watch_build()
{
    if "$VEKT_WATCH_BUILD_COMMAND[@]"; then
        :
    else
        local build_result=$?
        printf 'Build failed with exit code %d; continuing to watch.\n' "$build_result"
    fi
}

vekt_watch()
{
    local message=$1
    shift

    local -a watch_paths
    watch_paths=("$@")

    while true; do
        fswatch --one-per-batch --latency 0.25 --recursive "${watch_paths[@]}" >/dev/null
        print
        print "$message"
        vekt_run_watch_build
    done
}