#!/bin/sh
set -eu
ci_test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ci_src_dir=$(dirname -- "$ci_test_dir")
ci_repo_dir=$(CDPATH= cd -- "$ci_src_dir/../../../.." && pwd)
ci_test_out=$(mktemp -d /tmp/ci1302-tests.XXXXXX)
cc -std=c99 -Wall -Wextra -Werror -I"$ci_src_dir/protocol" \
    "$ci_test_dir/test_ci1302_protocol.c" "$ci_src_dir/protocol/ci1302_protocol.c" \
    -o "$ci_test_out/protocol"
"$ci_test_out/protocol"
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$ci_test_dir/mocks" -I"$ci_src_dir" -I"$ci_src_dir/porting" \
    -I"$ci_src_dir/protocol" -I"$ci_src_dir/audio_adapt" \
    -I"$ci_repo_dir/include/components/voice_chip" -DCONFIG_CI1302=1 \
    "$ci_test_dir/test_ci1302_audio.c" "$ci_src_dir/audio_adapt/ci1302_audio.c" \
    "$ci_src_dir/voice_ci1302_service.c" \
    "$ci_src_dir/../../core/voice_chip_service.c" \
    "$ci_src_dir/protocol/ci1302_protocol.c" -o "$ci_test_out/audio"
"$ci_test_out/audio"
