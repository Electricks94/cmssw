#!/usr/bin/env bash
#
# Usage:
#   ./fio_thread_sweep.sh                    # threads "1 2 4 8 16 32", 3 trials
#   ./fio_thread_sweep.sh "1 2 4 8"          # custom thread list
#   ./fio_thread_sweep.sh "1 2 4 8 16 32" 5  # 5 trials

set -u

# ---------------- config ----------------
FILE="/scratch/test.data"
TOTAL="4G"          # total dataset, split across the jobs (matches gdsio -s)
BS="128M"           # block size (matches gdsio -i)
IODEPTH=1           # 1 = synchronous per thread, the gdsio worker equivalent
ENGINE="libaio"     # swap to io_uring or psync if libaio is not built
RW="write"          # write | read | randwrite | randread
TRIALS=3
THREAD_COUNTS="1 2 4 8 16 32"
SHORT_FRAC=0.95     # flag '!' if a run wrote < this fraction of target
# ----------------------------------------

[[ $# -ge 1 && -n "$1" ]] && THREAD_COUNTS="$1"
[[ $# -ge 2 && -n "$2" ]] && TRIALS="$2"

# total in MiB, for splitting across jobs
TOTAL_MIB=$(awk -v s="$TOTAL" 'BEGIN{
  u=substr(s,length(s),1); n=substr(s,1,length(s)-1);
  if(u=="G"||u=="g") print n*1024;
  else if(u=="M"||u=="m") print n;
  else print s/1048576 }')

command -v python3 >/dev/null 2>&1 || { echo "python3 not found; needed for JSON parsing"; exit 1; }
command -v fio     >/dev/null 2>&1 || { echo "fio not found"; exit 1; }

# which fio stat block to read (write vs read)
case "$RW" in
  *read*)  RWKEY="read" ;;
  *)       RWKEY="write" ;;
esac
export RWKEY

# JSON parser: prints "tput_GiB lat_us time_s io_GiB util%" or ERR
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
    print("%.6f %.4f %.6f %.6f %.4f" % (
        w["bw_bytes"] / 1073741824.0,
        w["clat_ns"]["mean"] / 1000.0,
        w["runtime"] / 1000.0,
        w["io_bytes"] / 1073741824.0,
        util))
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

target_gib=$(awk -v t="$TOTAL_MIB" 'BEGIN{printf "%.2f", t/1024}')

echo "fio:      $(command -v fio)   engine: $ENGINE"
echo "rw:       $RW   total: $TOTAL   bs: $BS   iodepth: $IODEPTH   trials: $TRIALS"
echo "file:     $FILE   (total split across jobs, matching gdsio -s)"
df -h /scratch 2>/dev/null | awk 'NR==1||/scratch/{print "scratch:  "$0}'
echo

printf "%-7s %9s %8s %8s %11s %8s %7s %12s\n" \
  "threads" "tput" "+/-sd" "spdup" "lat_us" "time_s" "util%" "wrote/tg"
printf '%.0s-' {1..74}; echo

base_tp=""; peak_tp="-1"; peak_w="-"

for W in $THREAD_COUNTS; do
  per_job=$(awk -v t="$TOTAL_MIB" -v w="$W" 'BEGIN{printf "%d", t/w}')
  tput_vals=(); lat_vals=(); time_vals=(); io_vals=(); util_vals=(); ok=1

  for t in $(seq 1 "$TRIALS"); do
    js=$(fio --name=w --filename="$FILE" --rw="$RW" --bs="$BS" \
             --size="${per_job}M" --offset_increment="${per_job}M" \
             --direct=1 --ioengine="$ENGINE" --iodepth="$IODEPTH" \
             --numjobs="$W" --thread --group_reporting \
             --output-format=json 2>/dev/null)
    parsed=$(printf '%s' "$js" | python3 "$PARSER" 2>/dev/null)
    if [[ -z "$parsed" || "$parsed" == "ERR" ]]; then ok=0; break; fi
    read tp la tm io ut <<<"$parsed"
    tput_vals+=("$tp"); lat_vals+=("$la"); time_vals+=("$tm")
    io_vals+=("$io"); util_vals+=("$ut")
    sync
  done

  rm -f "$FILE"

  if [[ $ok -eq 0 ]]; then
    printf "%-7s %9s\n" "$W" "FAILED"
    continue
  fi

  read tp_m tp_s < <(printf "%s\n" "${tput_vals[@]}" | stats)
  read la_m la_s < <(printf "%s\n" "${lat_vals[@]}"  | stats)
  read tm_m tm_s < <(printf "%s\n" "${time_vals[@]}" | stats)
  read io_m io_s < <(printf "%s\n" "${io_vals[@]}"   | stats)
  read ut_m ut_s < <(printf "%s\n" "${util_vals[@]}" | stats)

  frac=$(awk -v w="$io_m" -v t="$target_gib" 'BEGIN{printf "%.4f",(t>0)?w/t:1}')
  mark=""; awk -v f="$frac" -v s="$SHORT_FRAC" 'BEGIN{exit !(f<s)}' && mark="!"
  data_str=$(awk -v w="$io_m" -v t="$target_gib" 'BEGIN{printf "%.2f/%.2f", w, t}')"$mark"

  [[ -z "$base_tp" ]] && base_tp="$tp_m"
  spd=$(awk -v m="$tp_m" -v b="$base_tp" 'BEGIN{printf "%.2f",(b>0)?m/b:0}')
  if gt "$tp_m" "$peak_tp"; then peak_tp="$tp_m"; peak_w="$W"; fi

  printf "%-7s %9.4f %8.4f %7.2fx %11.2f %8.4f %6.1f%% %12s\n" \
    "$W" "$tp_m" "$tp_s" "$spd" "$la_m" "$tm_m" "$ut_m" "$data_str"
done

if [[ "$peak_w" != "-" ]]; then
  echo
  echo "-> peak ${peak_tp} GiB/s at ${peak_w} thread(s)"
fi
echo
echo "tput in GiB/sec, lat = mean completion latency in usec, time in sec, util = disk busy %."
echo "spdup is tput vs the first thread count. wrote/tg is GiB; trailing '!' means short write (< $SHORT_FRAC of target)."
echo "+/-sd is sample standard deviation over $TRIALS trials."
