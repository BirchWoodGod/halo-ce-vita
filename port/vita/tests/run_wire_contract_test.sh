#!/bin/sh
# The wire contract test (tools/wire_contract.py): this tree's network wire
# against a pinned OpenCE checkout (docs/crossplay.md), and the legacy Vita
# wire (network version 18, betas 1-4) against the tree it was released
# from. Builds small probes against each tree with the game's flags (the
# Bungie message table's encoded bytes, the game settings record's layout,
# bytes and decoding, enum and limit values) and prints a pass/fail table of
# the baseline (what must stay the same) and of each gap of section 3.2
# (G1..G16). It passes when the baseline holds and the gaps still open are
# exactly the expected ones (EXPECTED_OPEN in the script, or
# WIRE_CONTRACT_EXPECTED_OPEN=G2,G3,...): a gate as the crossplay tasks land.
#   OPENCE_DIR   a clone of OpenCE holding the pinned commit (read only:
#                exported with git archive), default
#                ~/halo-test-out/xpv29.M6zO/OpenCE (8102e711)
#   HALO_TEST_OUT, HALO_TEST_KEEP   the work folder (test_out.sh)
# Needs the Linux build configured in this tree (build.ninja) and git.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${HALO_TEST_OUT:-${TMPDIR:-/tmp}/wire_contract_test.$$}
. "$here/test_out.sh"
test_out_begin "$out"
mkdir -p "$out"
set -- --work "$out"
[ -n "${WIRE_CONTRACT_EXPECTED_OPEN+set}" ] && set -- "$@" --expected-open "$WIRE_CONTRACT_EXPECTED_OPEN"
python3 "$root/tools/wire_contract.py" "$@"
status=$?
test_out_done $status
exit $status
