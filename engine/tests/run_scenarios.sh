#!/bin/sh
# Replays every scenario of tests/scenarios: the "# run:" lines of each input
# script, and the lines of runs.txt. Runs with --relaxed use lockstep (the
# original ROM in the reference emulator), the others use shadow (each lifted
# routine checked against its translation). Prints the failing runs.
#
# By default the shadow runs check the routines called directly by each frame
# (fast: a few minutes in all); FULL=1 keeps each scenario's --depth, which also
# checks the nested calls, and takes hours.
#
# usage (from engine/): [FULL=1] tests/run_scenarios.sh [ROM] [JOBS]

# One run (called by xargs below): --one ROM "OPTIONS" LOGDIR
if [ "$1" = "--one" ]; then
    case "$3" in *--relaxed*) bin=build/lockstep ;; *) bin=build/shadow ;; esac
    log="$4/$(echo "$3" | cksum | cut -d' ' -f1).log"
    opts=$3
    [ -z "$FULL" ] && opts=$(echo "$opts" | sed 's/--depth [0-9]*//')
    # shellcheck disable=SC2086
    if ! "$bin" "$2" $opts > "$log" 2>&1; then
        echo "FAIL: $bin $2 $3"
        tail -3 "$log" | sed 's/^/    /'
    fi
    exit 0
fi

ROM=${1:-../original.sms}
JOBS=${2:-8}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

for f in $(find tests/scenarios -name '*.txt' ! -name runs.txt | sort); do
    sed -n "s|^# run: *\(.*\)|--script $f \1|p" "$f"
done > "$OUT/list"
grep -v '^#' tests/scenarios/runs.txt | grep . >> "$OUT/list"

total=$(wc -l < "$OUT/list" | tr -d ' ')
echo "$total runs, $JOBS at a time${FULL:+ (full depth)}..."
tr '\n' '\0' < "$OUT/list" | xargs -0 -P "$JOBS" -I{} "$0" --one "$ROM" {} "$OUT" > "$OUT/fails"
if [ -s "$OUT/fails" ]; then
    cat "$OUT/fails"
    echo "$(grep -c '^FAIL' "$OUT/fails") of $total runs failed"
    exit 1
fi
echo "OK: $total runs"
