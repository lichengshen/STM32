#!/bin/sh
set -eu

test_bin="${TMPDIR:-/tmp}/usb-audio-player-wav-tests"
cc -std=c11 -Wall -Wextra -Werror \
  -Itests/include -ICore/Inc Core/Src/wav.c tests/test_wav.c \
  -o "$test_bin"
"$test_bin"
