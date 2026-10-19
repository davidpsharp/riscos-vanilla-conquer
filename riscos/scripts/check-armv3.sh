#!/usr/bin/env bash
# Fail if a RISC OS binary has instructions an ARM610, ARM710 or ARM7500 can't run:
# long multiplies (UMULL, SMULL, UMLAL, SMLAL: ARMv3M) and halfword or signed-byte
# transfers (LDRH, STRH, LDRSH, LDRSB: ARMv4, and unsafe on a real Risc PC anyway).
#   check-armv3.sh <elf> ...      e.g. check-armv3.sh build/riscos/vanillatd build/riscos-tools/vcprep
# Takes the ELF files (not the ,ff8 AIF copies), inside the repository: absolute, or
# relative to its top. UnixLib's __profiler_interrupt_handler has a UMULL, but only runs
# when profiling, so it's left out.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -gt 0 ] || { echo "usage: check-armv3.sh <elf> ..." >&2; exit 2; }
bad=0
for f in "$@"; do
    rel=${f#"$ROOT"/} # the container has the repository at /work
    out=$("$ROOT/riscos/scripts/sdk.sh" arm-unknown-riscos-objdump -d "$rel" | awk '
        /^[0-9a-f]+ <.*>:$/ { fn = $2; next }
        fn == "<__profiler_interrupt_handler>:" { next }
        /\t(u|s)(mull|mlal)/ { print fn, $0; next }
        /\t(ldr|str)(s?h|sb)(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?\t/ { print fn, $0 }')
    if [ -n "$out" ]; then
        echo "$f: $(printf '%s\n' "$out" | wc -l | tr -d ' ') instructions an ARMv3 can't run, e.g.:"
        printf '%s\n' "$out" | head -5
        bad=1
    else
        echo "$f: ARMv3-clean"
    fi
done
exit $bad
