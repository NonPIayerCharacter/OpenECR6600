#!/usr/bin/env bash
set -eu
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
build=$(mktemp -d /tmp/matter-customer-lfs.XXXXXX)
trap 'rm -rf "$build"' EXIT
cc=${CC:-gcc}
"$cc" -std=c99 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -DCONFIG_MATTER_CUSTOMER_LFS -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
    -I"$repo/components/nv/tests/customer_lfs" -I"$repo/include/components/nv" -I"$repo/include/components/fs/lfs" \
    "$repo/components/nv/tests/customer_lfs/test.c" "$repo/components/nv/src/customer_lfs.c" \
    "$repo/components/fs/littlefs/lfs_port.c" "$repo/components/fs/littlefs/lfs.c" \
    "$repo/components/fs/littlefs/lfs_util.c" -o "$build/test"
"$build/test"
