#!/bin/bash
# Runs the early self-tests in batches (THUG2_SELFTESTS), so one test that kills the process cannot hide the rest
# (a crash the guard misses ends thug2_check silently). Usage: bash tools/run_selftests_batched.sh [<thug2_check.exe>]
# Writes out/batched_results.txt: FAIL / CRASH lines, and each silent process death with the last test traced.
cd "$(dirname "$0")/.."
exe=${1:-out/build/verify/thug2_check.exe}
mkdir -p out; grep -rhoE "SELFTEST(Early, *[A-Za-z0-9_]+" src/hook | sed -E "s/.*, *//" | sort -u > out/early_tests.txt
# (each test named in the log as it starts: [selftests] trace=1 in the ini next to the exe)
printf "[selftests]
trace=1
" > "$(dirname "$exe")/thug2-decomp.ini"
out=out/batched_results.txt; : > $out
run() { # $1 = comma list
  THUG2_SELFTESTS="$1" timeout 600 $exe > out/batch_log.txt 2>&1; local code=$?
  grep -E "SELFTEST FAIL|SELFTEST CRASH|self-tests: [0-9]+ FAILED" out/batch_log.txt >> $out
  if ! grep -qE "early self-tests: (all passed|[0-9]+ FAILED)" out/batch_log.txt; then
    echo "DIED (exit $code) after: $(grep '^  Test' out/batch_log.txt | tail -1) [batch $1]" >> $out
    return 1
  fi
}
mapfile -t tests < out/early_tests.txt
batch=""; n=0
for t in "${tests[@]}"; do
  if [ $(( ${#batch} + ${#t} + 1 )) -gt 500 ]; then
    run "$batch" || { IFS=',' read -ra one <<< "$batch"; for x in "${one[@]}"; do run "$x"; done; }
    batch=""
  fi
  batch="${batch:+$batch,}$t"
done
[ -n "$batch" ] && { run "$batch" || { IFS=',' read -ra one <<< "$batch"; for x in "${one[@]}"; do run "$x"; done; }; }
echo DONE >> $out
