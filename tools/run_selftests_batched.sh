#!/bin/bash
# Runs the early self-tests in batches (THUG2_SELFTESTS), so one test that kills the process cannot hide the rest
# (a crash the guard misses ends thug2_check silently, without a FAIL line). A batch that dies is run again one
# test at a time. Usage: bash tools/run_selftests_batched.sh [<thug2_check.exe>]
# Writes out/batched_results.txt: FAIL / CRASH lines, and each silent process death with the last test traced.
# A clean run ends with "DONE" and no other line.
cd "$(dirname "$0")/.." || exit 1
exe=${1:-out/build/verify/thug2_check.exe}
mkdir -p out
grep -rhoE 'SELFTEST\(Early, *[A-Za-z0-9_]+' src/hook | sed -E 's/.*, *//' | sort -u > out/early_tests.txt
# (each test named in the log as it starts: [selftests] trace=1 in the ini next to the exe)
printf '[selftests]\ntrace=1\n' > "$(dirname "$exe")/thug2-decomp.ini"
out=out/batched_results.txt
: > "$out"
run() { # $1 = comma-separated test names
    THUG2_SELFTESTS="$1" timeout 900 "$exe" > out/batch_log.txt 2>&1
    local code=$?
    grep -E "SELFTEST FAIL|SELFTEST CRASH" out/batch_log.txt >> "$out"
    if ! grep -qE "early self-tests: (all passed|[0-9]+ FAILED)" out/batch_log.txt; then
        echo "DIED (exit $code) after: $(grep '^  Test' out/batch_log.txt | tail -1) [batch $1]" >> "$out"
        return 1
    fi
}
run_batch() {
    run "$1" && return
    IFS=',' read -ra one <<< "$1"
    for name in "${one[@]}"; do run "$name"; done
}
mapfile -t tests < out/early_tests.txt
batch=""
for t in "${tests[@]}"; do
    if [ $((${#batch} + ${#t} + 1)) -gt 500 ]; then
        run_batch "$batch"
        batch=""
    fi
    batch="${batch:+$batch,}$t"
done
[ -n "$batch" ] && run_batch "$batch"
echo DONE >> "$out"
grep -c "SELFTEST FAIL\|SELFTEST CRASH\|^DIED" "$out" | sed 's/^/problems: /'
