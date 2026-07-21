#!/usr/bin/env bash
#
# gdsio_thread_sweep.sh

set -u

# ---------------- config ----------------
GDSIO="/usr/local/cuda-13.1/gds/tools/gdsio"
FILE="/scratch/gdsio_test.data"
GPU=0
SIZE="4G"
IOSIZE="128M"
IO_TYPE=1          # 0=read  1=write  2=randread  3=randwrite
TRIALS=3
MODES="0 1 2 3 4 5 6"
THREAD_COUNTS="1 2 4 8 16 32"
# ----------------------------------------

# optional command line overrides
[[ $# -ge 1 ]] && MODES="$1"
[[ $# -ge 2 ]] && THREAD_COUNTS="$2"
[[ $# -ge 3 ]] && TRIALS="$3"

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
echo "io type:  $(io_name "$IO_TYPE")   size(total): $SIZE   io size: $IOSIZE   trials: $TRIALS"
echo "modes:    $MODES"
echo "threads:  $THREAD_COUNTS"
df -h /scratch 2>/dev/null | awk 'NR==1||/scratch/{print "scratch:  "$0}'
echo

declare -A BASE   # per-mode throughput at the first thread count, for speedup

for W in $THREAD_COUNTS; do
  best_tp="-1"; best_mode="-"

  echo "==========================================================================================="
  echo "threads = $W"
  printf "%-3s %-14s %9s %8s %8s %10s %8s %8s %13s\n" \
    "x" "mode" "tput" "+/-sd" "spdup" "lat_us" "+/-sd" "time_s" "wrote/tg_GiB"
  printf '%.0s-' {1..89}; echo

  for X in $MODES; do
    tput_vals=(); lat_vals=(); time_vals=(); wrote_vals=(); targ_kib=""; xname=""; ok=1

    for t in $(seq 1 "$TRIALS"); do
      out=$("$GDSIO" -x "$X" -d "$GPU" -s "$SIZE" -i "$IOSIZE" -f "$FILE" -I "$IO_TYPE" -w "$W" 2>&1)
      line=$(echo "$out" | grep -i "Throughput:" | tail -1)
      if [[ -z "$line" ]]; then ok=0; break; fi
      [[ -z "$xname" ]] && xname=$(echo "$line" | sed -n 's/.*XferType: \([A-Za-z0-9_]*\).*/\1/p')
      tp=$(echo "$line" | sed -n 's/.*Throughput: \([0-9.]*\).*/\1/p')
      la=$(echo "$line" | sed -n 's/.*Avg_Latency: \([0-9.]*\).*/\1/p')
      tm=$(echo "$line" | sed -n 's/.*total_time \([0-9.]*\).*/\1/p')
      wr=$(echo "$line" | sed -n 's/.*DataSetSize: \([0-9]*\)\/[0-9]*(KiB).*/\1/p')
      tg=$(echo "$line" | sed -n 's/.*DataSetSize: [0-9]*\/\([0-9]*\)(KiB).*/\1/p')
      tput_vals+=("$tp"); lat_vals+=("$la"); time_vals+=("$tm")
      wrote_vals+=("$wr"); [[ -z "$targ_kib" ]] && targ_kib="$tg"
      sync
    done

    if [[ $ok -eq 0 ]]; then
      printf "%-3s %-14s %9s\n" "$X" "${xname:-x$X}" "FAILED"
      continue
    fi

    read tp_m tp_s < <(printf "%s\n" "${tput_vals[@]}" | stats)
    read la_m la_s < <(printf "%s\n" "${lat_vals[@]}"  | stats)
    read tm_m tm_s < <(printf "%s\n" "${time_vals[@]}" | stats)
    read wr_m wr_s < <(printf "%s\n" "${wrote_vals[@]}" | stats)

    data_str=$(awk -v w="$wr_m" -v t="$targ_kib" \
      'BEGIN{printf "%.2f/%.2f", w/1048576, (t>0)?t/1048576:0}')

    # capture baseline throughput for this mode at the first thread count
    [[ -z "${BASE[$X]+set}" ]] && BASE[$X]="$tp_m"
    spd=$(awk -v m="$tp_m" -v b="${BASE[$X]}" 'BEGIN{printf "%.2f", (b>0)?m/b:0}')

    if gt "$tp_m" "$best_tp"; then best_tp="$tp_m"; best_mode="${xname:-x$X}"; fi

    printf "%-3s %-14s %9.4f %8.4f %7.2fx %10.2f %8.2f %8.4f %13s\n" \
      "$X" "${xname:-x$X}" "$tp_m" "$tp_s" "$spd" "$la_m" "$la_s" "$tm_m" "$data_str"
  done

  rm -f "$FILE"   # reclaim disk before the next thread count

  if [[ "$best_mode" != "-" ]]; then
    echo "-> fastest at $W thread(s): ${best_mode} (${best_tp} GiB/s)"
  fi
  echo
done

echo "tput in GiB/sec, lat in usec, time in sec. spdup is tput vs the first thread count."
echo "+/-sd is sample standard deviation over $TRIALS trials. wrote/tg is data in GiB."
