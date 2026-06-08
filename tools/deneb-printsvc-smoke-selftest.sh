#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Shell-only synthetic coverage for deneb-printsvc-smoke summary verifiers.
# This does not replace live hardware smoke runs; it proves the evidence
# checkers accept the required full native/resource shape without Python.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP_DIR="${TMPDIR:-/tmp}/deneb-printsvc-smoke-selftest.$$"
STOCK_SUMMARY="$TMP_DIR/stock.summary"
NATIVE_SUMMARY="$TMP_DIR/native.summary"
VERIFY="${SCRIPT_DIR}/deneb-printsvc-smoke-verify.sh"
COMPARE="${SCRIPT_DIR}/deneb-printsvc-smoke-compare.sh"

if [ ! -f "$VERIFY" ]; then
    VERIFY="${SCRIPT_DIR}/deneb-printsvc-smoke-verify"
fi
if [ ! -f "$COMPARE" ]; then
    COMPARE="${SCRIPT_DIR}/deneb-printsvc-smoke-compare"
fi

expect_failure() {
    label="$1"
    shift
    if "$@" > "$TMP_DIR/$label.out" 2>&1; then
        cat "$TMP_DIR/$label.out"
        echo "FAIL: expected command to fail: $label" >&2
        exit 1
    fi
    echo "PASS: expected failure: $label"
}

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$TMP_DIR"

cat > "$STOCK_SUMMARY" <<'EOF'
2026-06-08T00:00:00Z sample=initial mem_total_kb=250000 mem_used_kb=120000
2026-06-08T00:00:00Z sample=initial uptime_seconds=100
2026-06-08T00:00:00Z sample=initial cpu_total_jiffies=1000
2026-06-08T00:00:00Z sample=initial load1=0.20
2026-06-08T00:00:00Z sample=initial pid=111 vmsize_kb=33000 vmrss_kb=12000 command="/usr/bin/python3 /home/cygnus/marlindriver/print_service.py"
2026-06-08T00:00:10Z phase=boot-sync-ready elapsed_seconds=10 uptime_delta_seconds=10 route_body=_print_backend:native_native_only_route:true status=idle rc=0
2026-06-08T00:01:00Z phase=job-throughput path=/home/3D/stock.gcode bytes=10000 elapsed_seconds=20 bytes_per_second=500 rc=0
2026-06-08T00:02:00Z sample=final mem_total_kb=250000 mem_used_kb=122000
2026-06-08T00:02:00Z sample=final uptime_seconds=220
2026-06-08T00:02:00Z sample=final cpu_total_jiffies=1500
2026-06-08T00:02:00Z sample=final load1=0.30
2026-06-08T00:02:00Z sample=final pid=111 vmsize_kb=33000 vmrss_kb=12100 command="/usr/bin/python3 /home/cygnus/marlindriver/print_service.py"
EOF

cat > "$NATIVE_SUMMARY" <<'EOF'
2026-06-08T00:00:00Z start api=http://127.0.0.1/api/v1 cluster_api=http://127.0.0.1 native=1 heat=1 motion=1 macro=1 local_job=1 job=1 cura_job=1 preheat_abort=1 complete_job=1 pause_resume=1 restart=1 boot_sync=1
2026-06-08T00:00:00Z phase=printsvc-self-test rc=0
2026-06-08T00:00:00Z snapshot=initial
2026-06-08T00:00:00Z phase=route-initial kind=api method=GET path=/api/v1/deneb/print_backend rc=0 body=_print_backend:native_native_only_route:true
2026-06-08T00:00:00Z phase=status-initial kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:00Z phase=printer-initial kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:00Z sample=initial mem_total_kb=250000 mem_used_kb=100000
2026-06-08T00:00:00Z sample=initial uptime_seconds=100
2026-06-08T00:00:00Z sample=initial cpu_total_jiffies=1000
2026-06-08T00:00:00Z sample=initial load1=0.10
2026-06-08T00:00:00Z sample=initial pid=222 vmsize_kb=9000 vmrss_kb=3000 command="/usr/bin/deneb-printsvc"
2026-06-08T00:00:01Z phase=native-route-enabled previous=native-only
2026-06-08T00:00:01Z phase=native-driver-process kind=process deneb_printsvc=1 print_service_py=0 rc=0
2026-06-08T00:00:01Z snapshot=native-enabled
2026-06-08T00:00:01Z phase=route-native-enabled kind=api method=GET path=/api/v1/deneb/print_backend rc=0 body=_print_backend:native_native_only_route:true
2026-06-08T00:00:01Z phase=status-native-enabled kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:01Z phase=printer-native-enabled kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:02Z phase=boot-sync-ready elapsed_seconds=2 uptime_delta_seconds=2 route_body=_print_backend:native_native_only_route:true status=idle rc=0
2026-06-08T00:00:03Z phase=bed-low-heat kind=api rc=0
2026-06-08T00:00:03Z phase=nozzle-low-heat kind=api rc=0
2026-06-08T00:00:03Z snapshot=heating
2026-06-08T00:00:03Z phase=status-heating kind=api method=GET path=/printer/status rc=0 status=printing body={status:printing,native_only_route:true}
2026-06-08T00:00:03Z phase=printer-heating kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:04Z phase=bed-cooldown kind=api rc=0
2026-06-08T00:00:04Z phase=nozzle-cooldown kind=api rc=0
2026-06-08T00:00:04Z snapshot=cooldown
2026-06-08T00:00:04Z phase=status-cooldown kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:04Z phase=printer-cooldown kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:05Z phase=z-home kind=api rc=0
2026-06-08T00:00:05Z snapshot=motion
2026-06-08T00:00:05Z phase=status-motion kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:05Z phase=printer-motion kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:06Z phase=macro-home kind=api rc=0
2026-06-08T00:00:06Z snapshot=macro
2026-06-08T00:00:06Z phase=status-macro kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:06Z phase=printer-macro kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:07Z phase=local-job-native kind=printsvc-cli path=/media/usb/local.gcode rc=0
2026-06-08T00:00:07Z phase=local-job-start path=/media/usb/local.gcode source=USB rc=0
2026-06-08T00:00:07Z phase=status-local-job-active status=printing rc=0
2026-06-08T00:00:07Z phase=local-job-abort rc=0
2026-06-08T00:00:07Z phase=status-local-job-aborted status=idle rc=0
2026-06-08T00:00:08Z phase=job-start kind=multipart path=/tmp/job.gcode rc=0
2026-06-08T00:00:08Z snapshot=job-running
2026-06-08T00:00:08Z phase=status-job-running kind=api method=GET path=/printer/status rc=0 status=printing body={status:printing,native_only_route:true}
2026-06-08T00:00:08Z phase=printer-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:09Z phase=job-pause kind=api rc=0
2026-06-08T00:00:09Z snapshot=job-paused
2026-06-08T00:00:09Z phase=status-job-paused kind=api method=GET path=/printer/status rc=0 status=paused body={status:paused,native_only_route:true}
2026-06-08T00:00:09Z phase=printer-job-paused kind=api method=GET path=/printer rc=0 body={status:paused,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:10Z phase=job-resume kind=api rc=0
2026-06-08T00:00:10Z snapshot=job-resumed
2026-06-08T00:00:10Z phase=status-job-resumed kind=api method=GET path=/printer/status rc=0 status=printing body={status:printing,native_only_route:true}
2026-06-08T00:00:10Z phase=printer-job-resumed kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:11Z phase=job-abort kind=api rc=0
2026-06-08T00:00:11Z snapshot=job-aborted
2026-06-08T00:00:11Z phase=status-job-aborted kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:11Z phase=printer-job-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:12Z phase=cura-job-start kind=cluster-multipart path=/tmp/cura.gcode rc=0
2026-06-08T00:00:12Z snapshot=cura-job-running
2026-06-08T00:00:12Z phase=status-cura-job-running kind=api method=GET path=/printer/status rc=0 status=printing body={status:printing,native_only_route:true}
2026-06-08T00:00:12Z phase=printer-cura-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:13Z phase=cura-job-abort kind=cluster-api method=DELETE path=/print_jobs/current rc=0
2026-06-08T00:00:13Z snapshot=cura-job-aborted
2026-06-08T00:00:13Z phase=status-cura-job-aborted kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:13Z phase=printer-cura-job-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:14Z phase=preheat-abort-start kind=multipart path=/tmp/preheat.gcode rc=0
2026-06-08T00:00:14Z snapshot=preheat-abort-active
2026-06-08T00:00:14Z phase=status-preheat-abort-active kind=api method=GET path=/printer/status rc=0 status=printing body={status:printing,native_only_route:true}
2026-06-08T00:00:14Z phase=printer-preheat-abort-active kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:15Z phase=preheat-abort kind=api rc=0
2026-06-08T00:00:15Z snapshot=preheat-aborted
2026-06-08T00:00:15Z phase=status-preheat-aborted kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:15Z phase=printer-preheat-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:16Z phase=complete-job-start kind=multipart path=/tmp/complete.gcode rc=0
2026-06-08T00:00:16Z snapshot=complete-job-running
2026-06-08T00:00:16Z phase=printer-complete-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:36Z phase=job-completion-wait elapsed=20 rc=0
2026-06-08T00:00:36Z phase=job-throughput path=/tmp/complete.gcode bytes=9000 elapsed_seconds=18 bytes_per_second=500 rc=0
2026-06-08T00:00:36Z snapshot=job-completed
2026-06-08T00:00:36Z phase=status-job-completed kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:36Z phase=printer-job-completed kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:37Z phase=service-restart kind=service-restart rc=0
2026-06-08T00:00:37Z snapshot=service-restarted
2026-06-08T00:00:37Z phase=route-service-restarted kind=api method=GET path=/api/v1/deneb/print_backend rc=0 body=_print_backend:native_native_only_route:true
2026-06-08T00:00:37Z phase=status-service-restarted kind=api method=GET path=/printer/status rc=0 status=idle body={status:idle,native_only_route:true}
2026-06-08T00:00:37Z phase=printer-service-restarted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:01:00Z sample=final mem_total_kb=250000 mem_used_kb=101000
2026-06-08T00:01:00Z sample=final uptime_seconds=160
2026-06-08T00:01:00Z sample=final cpu_total_jiffies=1300
2026-06-08T00:01:00Z sample=final load1=0.15
2026-06-08T00:01:00Z sample=final pid=222 vmsize_kb=9000 vmrss_kb=3100 command="/usr/bin/deneb-printsvc"
2026-06-08T00:01:00Z snapshot=final
EOF

sh "$VERIFY" --full "$NATIVE_SUMMARY"
sh "$COMPARE" "$STOCK_SUMMARY" "$NATIVE_SUMMARY"

sed 's/native_stop_allowed:true/native_stop_allowed:false/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-stop.summary"
expect_failure verify_rejects_missing_active_stop \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-stop.summary"
expect_failure compare_rejects_missing_active_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-stop.summary"

sed 's/ body={status:[^}]*native_only_route:true}//g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-status-route.summary"
expect_failure verify_rejects_missing_status_native_route \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-status-route.summary"
expect_failure compare_rejects_missing_status_native_route \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-status-route.summary"

sed '/phase=status-job-running /s/ body={status:[^}]*native_only_route:true}//' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-one-status-route.summary"
expect_failure compare_rejects_missing_one_status_native_route \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-one-status-route.summary"

sed 's/native_only_route:true/native_only_route:false/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-route-not-exclusive.summary"
expect_failure verify_rejects_non_native_only_route \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-route-not-exclusive.summary"
expect_failure compare_rejects_non_native_only_route \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-route-not-exclusive.summary"

awk '
    { print }
    /snapshot=service-restarted/ {
        print "2026-06-08T00:00:38Z sample=service-restarted pid=333 vmsize_kb=33000 vmrss_kb=12100 command=\"/usr/bin/python3 /home/cygnus/marlindriver/print_service.py\""
    }
' "$NATIVE_SUMMARY" > "$TMP_DIR/native-stock-driver-returned.summary"
expect_failure verify_rejects_returned_stock_driver \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-stock-driver-returned.summary"
expect_failure compare_rejects_returned_stock_driver \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-stock-driver-returned.summary"

grep -v 'print_service.py' "$STOCK_SUMMARY" > "$TMP_DIR/stock-missing-python.summary"
expect_failure compare_rejects_missing_stock_python_baseline \
    sh "$COMPARE" \
    "$TMP_DIR/stock-missing-python.summary" "$NATIVE_SUMMARY"

sed 's/bytes_per_second=500/bytes_per_second=0/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-zero-throughput.summary"
expect_failure verify_rejects_zero_throughput \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-zero-throughput.summary"
expect_failure compare_rejects_zero_throughput \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-zero-throughput.summary"

echo "deneb-printsvc smoke verifier selftest passed"
