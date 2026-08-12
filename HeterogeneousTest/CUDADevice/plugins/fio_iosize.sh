#!/usr/bin/env bash
# Usage:
#   ./fio_chunk_sweep.sh                          # default sizes, 4 jobs, 3 trials
#   ./fio_chunk_sweep.sh "4K 64K 1M 16M"          # custom chunk list
#   ./fio_chunk_sweep.sh "" 1                     # single job curve
#   ./fio_chunk_sweep.sh "" 4 5                   # 4 jobs, 5 trials
#
# Args (optional, positional): CHUNK_SIZES  NUMJOBS  TRIALS
# Pass "" to keep a default while overriding a later arg.

set -u

# ---------------- config ----------------
FILE="/scratch/test.data"
TOTAL="1G"          # total dataset, split across the jobs (matches gdsio -s)
NUMJOBS=4           # HELD CONSTANT across the sweep (like gdsio -w)
IODEPTH=1           # 1 = synchronous per job; raise for a deeper single-job queue
ENGINE="libaio"     # swap to io_uring or psync if libaio is not built
RW="write"          # write | read | randwrite | randread
TRIALS=3
CHUNK_SIZES="4K 8K 16K 32K 64K 128K 256K 512K 1M 2M 4M 8M 16M"
SHORT_FRAC=0.95     # flag '!' if a run wrote < this fraction of target
# ----------------------------------------

[[ $# -ge 1 && -n "$1" ]] && CHUNK_SIZES="$1"
[[ $# -ge 2 && -n "$2" ]] && NUMJOBS="$2"
[[ $# -ge 3 && -n "$3" ]] && TRIALS="$3"

command -v python3 >/dev/null 2>&1 || { echo "python3 not found; needed for JSON parsing"; exit 1; }
command -v fio     >/dev/null 2>&1 || { echo "fio not found"; exit 1; }

# total in MiB, and per job slice
TOTAL_MIB=$(awk -v s="$TOTAL" 'BEGIN{
  u=substr(s,length(s),1); n=substr(s,1,length(s)-1);
  if(u=="G"||u=="g") print n*1024;
  else if(u=="M"||u=="m") print n;
  else print s/1048576 }')
PER_JOB=$(awk -v t="$TOTAL_MIB" -v w="$NUMJOBS" 'BEGIN{printf "%d", t/w}')
target_gib=$(awk -v t="$TOTAL_MIB" 'BEGIN{printf "%.2f", t/1024}')

# which fio stat block to read (write vs read)
case "$RW" in
  *read*)  RWKEY="read" ;;
  *)       RWKEY="write" ;;
esac
export RWKEY

# JSON parser: prints "tput_GiB lat_us time_s io_GiB util% iops" or ERR
PARSER=$(mktemp)
trap 'rm -f "$PARSER" "$FILE"' EXIT
cat > "$PARSER" <<'PYEOF'
import json, os, sys
key = os.environ.get("RWKEY", "write")
try:
    d = json.load(sys.stdin)
    w = d["jobs"][0][key]
    du = d.get("disk_util", [])
    util = max((float(str(x.get("util", 0)).rstrip("%")) for x in du), default=0.0)
    print("%.6f %.4f %.6f %.6f %.4f %.2f" % (
        w["bw_bytes"] / 1073741824.0,
        w["clat_ns"]["mean"] / 1000.0,
        w["runtime"] / 1000.0,
        w["io_bytes"] / 1073741824.0,
        util,
        w["iops"]))
except Exception:
    print("ERR")
PYEOF

stats() {
  awk '{s+=$1; sq+=$1*$1; n++}
       END{ if(n==0){print "0 0"; exit}
            m=s/n; v=(n>1)?(sq-s*s/n)/(n-1):0; if(v<0)v=0;
            printf "%.4f %.4f", m, sqrt(v) }'
}
gt() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a>b)}'; }

echo "fio:      $(command -v fio)   engine: $ENGINE"
echo "rw:       $RW   total: $TOTAL   numjobs: $NUMJOBS (constant)   iodepth: $IODEPTH   trials: $TRIALS"
echo "file:     $FILE   (total split across jobs: ${PER_JOB}M each)"
df -h /scratch 2>/dev/null | awk 'NR==1||/scratch/{print "scratch:  "$0}'
echo

printf "%-8s %9s %8s %9s %10s %8s %7s %11s\n" \
  "chunk" "tput" "+/-sd" "kIOPS" "lat_us" "time_s" "util%" "wrote/tg"
printf '%.0s-' {1..73}; echo

peak_tp="-1"; peak_bs="-"

for BS in $CHUNK_SIZES; do
  tput_vals=(); lat_vals=(); time_vals=(); io_vals=(); util_vals=(); iops_vals=(); ok=1

  for t in $(seq 1 "$TRIALS"); do
    js=$(fio --name=w --filename="$FILE" --rw="$RW" --bs="$BS" \
             --size="${PER_JOB}M" --offset_increment="${PER_JOB}M" \
             --direct=1 --ioengine="$ENGINE" --iodepth="$IODEPTH" \
             --numjobs="$NUMJOBS" --thread --group_reporting \
             --output-format=json 2>/dev/null)
    parsed=$(printf '%s' "$js" | python3 "$PARSER" 2>/dev/null)
    if [[ -z "$parsed" || "$parsed" == "ERR" ]]; then ok=0; break; fi
    read tp la tm io ut iops <<<"$parsed"
    tput_vals+=("$tp"); lat_vals+=("$la"); time_vals+=("$tm")
    io_vals+=("$io"); util_vals+=("$ut"); iops_vals+=("$iops")
    sync
  done

  rm -f "$FILE"

  if [[ $ok -eq 0 ]]; then
    printf "%-8s %9s\n" "$BS" "FAILED"
    continue
  fi

  read tp_m tp_s < <(printf "%s\n" "${tput_vals[@]}" | stats)
  read la_m la_s < <(printf "%s\n" "${lat_vals[@]}"  | stats)
  read tm_m tm_s < <(printf "%s\n" "${time_vals[@]}" | stats)
  read io_m io_s < <(printf "%s\n" "${io_vals[@]}"   | stats)
  read ut_m ut_s < <(printf "%s\n" "${util_vals[@]}" | stats)
  read ip_m ip_s < <(printf "%s\n" "${iops_vals[@]}" | stats)

  kiops=$(awk -v i="$ip_m" 'BEGIN{printf "%.2f", i/1000}')
  frac=$(awk -v w="$io_m" -v t="$target_gib" 'BEGIN{printf "%.4f",(t>0)?w/t:1}')
  mark=""; awk -v f="$frac" -v s="$SHORT_FRAC" 'BEGIN{exit !(f<s)}' && mark="!"
  data_str=$(awk -v w="$io_m" -v t="$target_gib" 'BEGIN{printf "%.2f/%.2f", w, t}')"$mark"

  if gt "$tp_m" "$peak_tp"; then peak_tp="$tp_m"; peak_bs="$BS"; fi

  printf "%-8s %9.4f %8.4f %9.2f %10.2f %8.4f %6.1f%% %11s\n" \
    "$BS" "$tp_m" "$tp_s" "$kiops" "$la_m" "$tm_m" "$ut_m" "$data_str"
done

if [[ "$peak_bs" != "-" ]]; then
  echo
  echo "-> peak ${peak_tp} GiB/s at chunk ${peak_bs}"
fi
echo
echo "tput in GiB/sec, kIOPS = thousands of ops/sec, lat = mean completion latency usec, time sec, util = disk busy %."
echo "wrote/tg in GiB; trailing '!' means short write (< $SHORT_FRAC of target). +/-sd over $TRIALS trials."
