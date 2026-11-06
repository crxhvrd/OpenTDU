#!/usr/bin/env bash
set -euo pipefail

output="${1:-nightly_changelog.md}"
previous_tag="$(git describe --tags --abbrev=0 --match 'nightly_*' HEAD^ 2>/dev/null || true)"

{
    if [ -n "$previous_tag" ]; then
        echo "Changes since ${previous_tag}:"
        echo
        git log --no-merges --pretty=format:'- %s' "${previous_tag}..HEAD"
    else
        git log --no-merges --pretty=format:'- %s' -n 1 HEAD
    fi
    echo
} > "$output"

cat "$output"
