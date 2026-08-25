#!/usr/bin/env bash
#
# gdsio_iosize_sweep.sh
# Hold thread count CONSTANT and sweep IO size (-i) from 4K to 16M.
# Reports throughput, aggregate kIOPS, latency, time, and wrote/target data.
# Purpose: find the IO size knee where the path goes from IOPS bound to
# bandwidth bound.
#
# Usage:
#   ./gdsio_iosize_sweep.sh                         # mode 0, 4 threads, 3 trials
#   ./gdsio_iosize_sweep.sh "0" "4K 64K 1M 16M"     # custom size list
#   ./gdsio_iosize_sweep.sh "0 1 6" "" 8            # 3 modes, 8 threads constant
#   ./gdsio_iosize_sweep.sh "0" "" 1                # single thread curve
#
# Args (all optional, positional): MODES  IO_SIZES  THREADS  TRIALS
# Pass "" to keep a default while overriding a later arg.

set -u

# ---------------- config ----------------
GDSIO="/usr/local/cuda-13.1/gds/tools/gdsio"
FILE="/scratch/gdsio_test.data"
GPU=0
SIZE="1G"          # total dataset per run; modest so the 4K runs finish quickly
THREADS=4          # HELD CONSTANT across the whole sweep
IO_TYPE=1          # 0=read  1=write  2=randread  3=randwrite
TRIALS=3
MODES="0"          # default GPUD (true GDS path); e.g. "0 1 6" to compare
IO_SIZES="4K 8K 16K 32K 64K 128K 256K 512K 1M 2M 4M 8M 16M"
SHORT_FRAC=0.95    # flag a run with '!' if wrote < this fraction of target
# ----------------------------------------

[[ $# -ge 1 && -n "$1" ]] && MODES="$1"
[[ $# -ge 2 && -n "$2" ]] && IO_SIZES="$2"
[[ $# -ge 3 && -n "$3" ]] && THREADS="$3"
[[ $# -ge 4 && -n "$4" ]] && TRIALS="$4"

io_name() {
  case "$1" in
    0) echo "READ" ;; 1) echo "WRITE" ;;
    2) echo "RANDREAD" ;; 3) echo "RANDWRITE" ;;
    *) echo "IO_$1" ;;
  esac
}

# mean + sample stddev (Bessel, n-1) from values on stdin
stats() {
  awk '{s+=$1; sq+=$1*$1; n++}
       END{ if(n==0){print "0 0"; exit}
            m=s/n; v=(n>1)?(sq-s*s/n)/(n-1):0; if(v<0)v=0;
            printf "%.4f %.4f", m, sqrt(v) }'
}

# a > b for floats, exit 0 if true
gt() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a>b)}'; }

echo "gdsio:    $GDSIO"
echo "io type:  $(io_name "$IO_TYPE")   size(total): $SIZE   threads: $THREADS (constant)   trials: $TRIALS"
echo "modes:    $MODES"
echo "io sizes: $IO_SIZES"
df -h /scratch 2>/dev/null | awk 'NR==1||/scratch/{print "scratch:  "$0}'
echo

for X in $MODES; do
  xname=""; peak_tp="-1"; peak_io="-"

  echo "==============================================================================="
  echo "mode -x $X   threads = $THREADS (constant)"
  printf "%-8s %9s %8s %9s %10s %8s %13s\n" \
    "iosize" "tput" "+/-sd" "kIOPS" "lat_us" "time_s" "wrote/tg_GiB"
  printf '%.0s-' {1..71}; echo

  for IO in $IO_SIZES; do
    tput_vals=(); lat_vals=(); time_vals=(); wrote_vals=(); ops_vals=(); targ_kib=""; ok=1

    for t in $(seq 1 "$TRIALS"); do
      out=$("$GDSIO" -x "$X" -d "$GPU" -s "$SIZE" -i "$IO" -f "$FILE" -I "$IO_TYPE" -w "$THREADS" 2>&1)
      line=$(echo "$out" | grep -i "Throughput:" | tail -1)
      if [[ -z "$line" ]]; then ok=0; break; fi
      [[ -z "$xname" ]] && xname=$(echo "$line" | sed -n 's/.*XferType: \([A-Za-z0-9_]*\).*/\1/p')
      tp=$(echo "$line"  | sed -n 's/.*Throughput: \([0-9.]*\).*/\1/p')
      la=$(echo "$line"  | sed -n 's/.*Avg_Latency: \([0-9.]*\).*/\1/p')
      tm=$(echo "$line"  | sed -n 's/.*total_time \([0-9.]*\).*/\1/p')
      op=$(echo "$line"  | sed -n 's/.*ops: \([0-9]*\).*/\1/p')
      wr=$(echo "$line"  | sed -n 's/.*DataSetSize: \([0-9]*\)\/[0-9]*(KiB).*/\1/p')
      tg=$(echo "$line"  | sed -n 's/.*DataSetSize: [0-9]*\/\([0-9]*\)(KiB).*/\1/p')
      tput_vals+=("$tp"); lat_vals+=("$la"); time_vals+=("$tm")
      ops_vals+=("$op"); wrote_vals+=("$wr"); [[ -z "$targ_kib" ]] && targ_kib="$tg"
      sync
    done

    if [[ $ok -eq 0 ]]; then
      printf "%-8s %9s\n" "$IO" "FAILED"
      continue
    fi

    read tp_m tp_s < <(printf "%s\n" "${tput_vals[@]}" | stats)
    read la_m la_s < <(printf "%s\n" "${lat_vals[@]}"  | stats)
    read tm_m tm_s < <(printf "%s\n" "${time_vals[@]}" | stats)
    read op_m op_s < <(printf "%s\n" "${ops_vals[@]}"  | stats)
    read wr_m wr_s < <(printf "%s\n" "${wrote_vals[@]}" | stats)

    kiops=$(awk -v o="$op_m" -v t="$tm_m" 'BEGIN{printf "%.2f", (t>0)?o/t/1000:0}')

    # wrote/target in GiB, with a short-write flag
    frac=$(awk -v w="$wr_m" -v t="$targ_kib" 'BEGIN{printf "%.4f", (t>0)?w/t:1}')
    mark=""; awk -v f="$frac" -v s="$SHORT_FRAC" 'BEGIN{exit !(f<s)}' && mark="!"
    data_str=$(awk -v w="$wr_m" -v t="$targ_kib" \
      'BEGIN{printf "%.2f/%.2f", w/1048576, (t>0)?t/1048576:0}')"$mark"

    if gt "$tp_m" "$peak_tp"; then peak_tp="$tp_m"; peak_io="$IO"; fi

    printf "%-8s %9.4f %8.4f %9.2f %10.2f %8.4f %13s\n" \
      "$IO" "$tp_m" "$tp_s" "$kiops" "$la_m" "$tm_m" "$data_str"
  done

  rm -f "$FILE"

  if [[ "$peak_io" != "-" ]]; then
    echo "-> peak ${peak_tp} GiB/s at io size ${peak_io} [${xname:-x$X}]"
  fi
  echo
done

echo "tput in GiB/sec, kIOPS = thousands of ops/sec (aggregate), lat in usec, time in sec."
echo "wrote/tg is data in GiB; a trailing '!' means the run wrote < ${SHORT_FRAC} of target (short write)."
echo "+/-sd is sample standard deviation over $TRIALS trials."
