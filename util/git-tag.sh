#!/bin/sh
# Print the current revision, including tracked or untracked working changes.
revision=$(git rev-parse --short HEAD) || exit 1
if [ -n "$(git status --porcelain)" ]; then
    revision="$revision-dirty"
fi
printf '%s' "$revision"
