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
SMOKE="${SCRIPT_DIR}/deneb-printsvc-smoke.sh"

if [ ! -f "$VERIFY" ]; then
    VERIFY="${SCRIPT_DIR}/deneb-printsvc-smoke-verify"
fi
if [ ! -f "$COMPARE" ]; then
    COMPARE="${SCRIPT_DIR}/deneb-printsvc-smoke-compare"
fi
if [ ! -f "$SMOKE" ]; then
    SMOKE="${SCRIPT_DIR}/deneb-printsvc-smoke"
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
2026-06-08T00:00:10Z phase=boot-sync-ready elapsed_seconds=10 uptime_delta_seconds=10 route_body=_print_backend:native_native_only_route:true status=idle status_body=idle rc=0
2026-06-08T00:01:00Z phase=job-throughput path=/home/3D/stock.gcode bytes=10000 elapsed_seconds=20 bytes_per_second=500 rc=0
2026-06-08T00:02:00Z sample=final mem_total_kb=250000 mem_used_kb=122000
2026-06-08T00:02:00Z sample=final uptime_seconds=220
2026-06-08T00:02:00Z sample=final cpu_total_jiffies=1500
2026-06-08T00:02:00Z sample=final load1=0.30
2026-06-08T00:02:00Z sample=final pid=111 vmsize_kb=33000 vmrss_kb=12100 command="/usr/bin/python3 /home/cygnus/marlindriver/print_service.py"
EOF

cat > "$NATIVE_SUMMARY" <<'EOF'
2026-06-08T00:00:00Z start api=http://127.0.0.1/api/v1 cluster_api=http://127.0.0.1 native=1 heat=1 motion=1 macro=1 local_job=1 job=1 cura_job=1 preheat_abort=1 active_abort=1 active_abort_delay=60 complete_job=1 pause_resume=1 restart=1 boot_sync=1
2026-06-08T00:00:00Z phase=printsvc-self-test rc=0
2026-06-08T00:00:00Z snapshot=initial
2026-06-08T00:00:00Z phase=route-initial kind=api method=GET path=/api/v1/deneb/print_backend rc=0 body=_print_backend:native_native_only_route:true
2026-06-08T00:00:00Z phase=status-initial kind=api method=GET path=/printer/status rc=0 status=idle body=idle
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
2026-06-08T00:00:01Z phase=status-native-enabled kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:01Z phase=printer-native-enabled kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:02Z phase=boot-sync-ready elapsed_seconds=2 uptime_delta_seconds=2 route_body=_print_backend:native_native_only_route:true status=idle status_body=idle rc=0
2026-06-08T00:00:03Z phase=heat-safety kind=physical axes=none required_home=none travel=bed_to_40C_nozzle_to_50C_then_cooldown stop_conditions=temperature_sensor_fault_or_runaway rc=0
2026-06-08T00:00:03Z phase=bed-low-heat kind=api rc=0
2026-06-08T00:00:03Z phase=nozzle-low-heat kind=api rc=0
2026-06-08T00:00:03Z snapshot=heating
2026-06-08T00:00:03Z phase=status-heating kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:03Z phase=printer-heating kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:04Z phase=bed-cooldown kind=api rc=0
2026-06-08T00:00:04Z phase=nozzle-cooldown kind=api rc=0
2026-06-08T00:00:04Z snapshot=cooldown
2026-06-08T00:00:04Z phase=status-cooldown kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:04Z phase=printer-cooldown kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:05Z phase=motion-safety kind=physical axes=Z required_home=Z travel=z_home_to_max_only stop_conditions=z_endstop_or_unexpected_direction rc=0
2026-06-08T00:00:05Z phase=z-home kind=api rc=0
2026-06-08T00:00:05Z snapshot=motion
2026-06-08T00:00:05Z phase=status-motion kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:05Z phase=printer-motion kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:06Z phase=macro-safety kind=physical axes=XYZ required_home=XYZ travel=stock_home_macro stop_conditions=endstop_or_unexpected_motion rc=0
2026-06-08T00:00:06Z phase=macro-home kind=api rc=0
2026-06-08T00:00:06Z snapshot=macro
2026-06-08T00:00:06Z phase=status-macro kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:06Z phase=printer-macro kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:07Z phase=local-job-safety kind=physical axes=job_defined required_home=z_home travel=user_supplied_gcode stop_conditions=endstop_temperature_or_unexpected_motion rc=0
2026-06-08T00:00:07Z phase=local-job-native kind=printsvc-cli path=/media/usb/local.gcode rc=0
2026-06-08T00:00:07Z phase=local-job-start path=/media/usb/local.gcode source=USB rc=0
2026-06-08T00:00:07Z phase=local-job-accepted deneb_state=pre_print native_active=true native_stop_allowed=true source=USB rc=0
2026-06-08T00:00:07Z phase=local-job-abort rc=0
2026-06-08T00:00:07Z phase=local-job-aborted-state deneb_state=idle native_active=false native_stop_allowed=false source=USB rc=0
2026-06-08T00:00:08Z phase=job-safety kind=physical axes=job_defined required_home=z_home travel=user_supplied_gcode_until_abort stop_conditions=endstop_temperature_or_unexpected_motion rc=0
2026-06-08T00:00:08Z phase=job-start kind=multipart path=/tmp/job.gcode rc=0
2026-06-08T00:00:08Z snapshot=job-running
2026-06-08T00:00:08Z phase=status-job-running kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:08Z phase=printer-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:09Z phase=job-pause kind=api rc=0
2026-06-08T00:00:09Z snapshot=job-paused
2026-06-08T00:00:09Z phase=status-job-paused kind=api method=GET path=/printer/status rc=0 status=paused body=paused
2026-06-08T00:00:09Z phase=printer-job-paused kind=api method=GET path=/printer rc=0 body={status:paused,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:10Z phase=job-resume kind=api rc=0
2026-06-08T00:00:10Z snapshot=job-resumed
2026-06-08T00:00:10Z phase=status-job-resumed kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:10Z phase=printer-job-resumed kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:11Z phase=job-abort kind=api rc=0
2026-06-08T00:00:11Z snapshot=job-aborted
2026-06-08T00:00:11Z phase=status-job-aborted kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:11Z phase=printer-job-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:12Z phase=cura-job-safety kind=physical axes=job_defined required_home=z_home travel=cura_uploaded_gcode_until_abort stop_conditions=endstop_temperature_or_unexpected_motion rc=0
2026-06-08T00:00:12Z phase=cura-job-start kind=cluster-multipart path=/tmp/cura.gcode rc=0
2026-06-08T00:00:12Z snapshot=cura-job-running
2026-06-08T00:00:12Z phase=status-cura-job-running kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:12Z phase=printer-cura-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:13Z phase=cura-job-abort kind=cluster-api method=DELETE path=/print_jobs/current rc=0
2026-06-08T00:00:13Z snapshot=cura-job-aborted
2026-06-08T00:00:13Z phase=status-cura-job-aborted kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:13Z phase=printer-cura-job-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:14Z phase=preheat-abort-safety kind=physical axes=job_defined required_home=z_home travel=preheat_until_abort stop_conditions=temperature_sensor_fault_or_unexpected_motion rc=0
2026-06-08T00:00:14Z phase=preheat-abort-start kind=multipart path=/tmp/preheat.gcode rc=0
2026-06-08T00:00:14Z snapshot=preheat-abort-active
2026-06-08T00:00:14Z phase=status-preheat-abort-active kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:14Z phase=printer-preheat-abort-active kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:15Z phase=preheat-abort kind=api rc=0
2026-06-08T00:00:15Z snapshot=preheat-aborted
2026-06-08T00:00:15Z phase=status-preheat-aborted kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:15Z phase=printer-preheat-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:16Z phase=active-abort-safety kind=physical axes=job_defined required_home=z_home travel=user_supplied_gcode_until_active_abort stop_conditions=endstop_temperature_or_unexpected_motion rc=0
2026-06-08T00:00:16Z phase=active-abort-start kind=multipart path=/tmp/active.gcode rc=0
2026-06-08T00:00:16Z snapshot=active-abort-printing
2026-06-08T00:00:16Z phase=status-active-abort-printing kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:16Z phase=printer-active-abort-printing kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:17Z phase=active-abort kind=api rc=0
2026-06-08T00:00:17Z snapshot=active-aborted
2026-06-08T00:00:17Z phase=status-active-aborted kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:17Z phase=printer-active-aborted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:18Z phase=complete-job-fixture-check path=/tmp/complete.gcode rc=0 reason=progress_command
2026-06-08T00:00:18Z phase=complete-job-safety kind=physical axes=Z required_home=z_home travel=bounded_relative_Z_negative_max_96mm stop_conditions=z_endstop_or_unexpected_direction rc=0
2026-06-08T00:00:18Z phase=complete-job-start kind=multipart path=/tmp/complete.gcode rc=0
2026-06-08T00:00:18Z snapshot=complete-job-running
2026-06-08T00:00:18Z phase=status-complete-job-running kind=api method=GET path=/printer/status rc=0 status=printing body=printing
2026-06-08T00:00:18Z phase=printer-complete-job-running kind=api method=GET path=/printer rc=0 body={status:printing,native_active:true,native_stop_allowed:true}
2026-06-08T00:00:38Z phase=job-completion-wait elapsed=20 rc=0
2026-06-08T00:00:38Z phase=job-throughput path=/tmp/complete.gcode bytes=9000 elapsed_seconds=18 bytes_per_second=500 rc=0
2026-06-08T00:00:38Z snapshot=job-completed
2026-06-08T00:00:38Z phase=status-job-completed kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:38Z phase=printer-job-completed kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:00:39Z phase=service-restart kind=service-restart rc=0
2026-06-08T00:00:39Z snapshot=service-restarted
2026-06-08T00:00:39Z phase=route-service-restarted kind=api method=GET path=/api/v1/deneb/print_backend rc=0 body=_print_backend:native_native_only_route:true
2026-06-08T00:00:39Z phase=status-service-restarted kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:39Z phase=printer-service-restarted kind=api method=GET path=/printer rc=0 body={status:idle,native_active:false,native_stop_allowed:false}
2026-06-08T00:01:00Z sample=final mem_total_kb=250000 mem_used_kb=101000
2026-06-08T00:01:00Z sample=final uptime_seconds=160
2026-06-08T00:01:00Z sample=final cpu_total_jiffies=1300
2026-06-08T00:01:00Z sample=final load1=0.15
2026-06-08T00:01:00Z sample=final pid=222 vmsize_kb=9000 vmrss_kb=3100 command="/usr/bin/deneb-printsvc"
2026-06-08T00:01:00Z snapshot=final
EOF

sh "$VERIFY" --full "$NATIVE_SUMMARY"
sh "$COMPARE" "$STOCK_SUMMARY" "$NATIVE_SUMMARY"
sh "$COMPARE" --require-reduction "$STOCK_SUMMARY" "$NATIVE_SUMMARY"

sed '/phase=status-complete-job-running /s/status=printing/status=idle/; /phase=status-complete-job-running /s/body=printing/body=idle/; /phase=printer-complete-job-running /s/status:printing/status:idle/; /phase=printer-complete-job-running /s/native_active:true/native_active:false/; /phase=printer-complete-job-running /s/native_stop_allowed:true/native_stop_allowed:false/; /phase=job-completion-wait /s/elapsed=20/elapsed=0/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-fast-complete.summary"
sh "$VERIFY" --full "$TMP_DIR/native-fast-complete.summary"
sh "$COMPARE" "$STOCK_SUMMARY" "$TMP_DIR/native-fast-complete.summary"
echo "PASS: fast natural completion evidence accepted"

sh "$SMOKE" --summary-parser-selftest \
    --log "$TMP_DIR/parser.log" \
    --summary "$TMP_DIR/parser.summary"

sed 's/native_stop_allowed:true/native_stop_allowed:false/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-stop.summary"
expect_failure verify_rejects_missing_active_stop \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-stop.summary"
expect_failure compare_rejects_missing_active_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-stop.summary"

grep -v 'phase=active-abort-safety ' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-physical-safety.summary"
expect_failure verify_rejects_missing_physical_safety \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-physical-safety.summary"

sed '/phase=status-initial /s/status=idle/status=printing/; /phase=status-initial /s/status:idle/status:printing/; /phase=printer-initial /s/native_active:false/native_active:true/; /phase=printer-initial /s/native_stop_allowed:false/native_stop_allowed:true/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-idle.summary"
expect_failure verify_rejects_missing_initial_idle \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-idle.summary"
expect_failure compare_rejects_missing_initial_idle \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-idle.summary"

sed '/phase=printer-cura-job-running /s/native_stop_allowed:true/native_stop_allowed:false/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-one-active-stop.summary"
expect_failure compare_rejects_missing_one_active_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-one-active-stop.summary"

sed '/phase=printer-job-completed /s/native_stop_allowed:false/native_stop_allowed:true/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-one-inactive-stop.summary"
expect_failure compare_rejects_missing_one_inactive_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-one-inactive-stop.summary"

sed '/phase=printer-service-restarted /s/native_active:false/native_active:true/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-service-restart-active-stuck.summary"
expect_failure verify_rejects_service_restart_active_stuck \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-service-restart-active-stuck.summary"

grep -v 'phase=local-job-start ' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-local-job.summary"
expect_failure compare_rejects_missing_local_job \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-local-job.summary"

sed '/phase=local-job-accepted /s/native_stop_allowed=true/native_stop_allowed=false/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-local-job-missing-stop.summary"
expect_failure verify_rejects_local_job_missing_stop \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-local-job-missing-stop.summary"
expect_failure compare_rejects_local_job_missing_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-local-job-missing-stop.summary"

grep -v 'phase=status-complete-job-running ' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-complete-running-status.summary"
expect_failure verify_rejects_missing_complete_running_status \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-complete-running-status.summary"
expect_failure compare_rejects_missing_complete_running_status \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-complete-running-status.summary"

grep -v 'phase=complete-job-fixture-check ' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-complete-fixture-check.summary"
expect_failure verify_rejects_missing_complete_fixture_check \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-complete-fixture-check.summary"
expect_failure compare_rejects_missing_complete_fixture_check \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-complete-fixture-check.summary"

sed '/phase=status-preheat-abort-active /s/status=printing/status=idle/; /phase=status-preheat-abort-active /s/status:printing/status:idle/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-wrong-one-status.summary"
expect_failure compare_rejects_wrong_one_status \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-wrong-one-status.summary"

grep -v 'phase=status-active-abort-printing ' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-missing-active-abort-status.summary"
expect_failure verify_rejects_missing_active_abort_status \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-missing-active-abort-status.summary"
expect_failure compare_rejects_missing_active_abort_status \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-missing-active-abort-status.summary"

sed '/phase=printer-active-abort-printing /s/native_stop_allowed:true/native_stop_allowed:false/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-active-abort-missing-stop.summary"
expect_failure verify_rejects_active_abort_missing_stop \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-active-abort-missing-stop.summary"
expect_failure compare_rejects_active_abort_missing_stop \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-active-abort-missing-stop.summary"

sed '/phase=printer-active-aborted /s/native_stop_allowed:false/native_stop_allowed:true/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-active-aborted-stop-stuck.summary"
expect_failure verify_rejects_active_aborted_stop_stuck \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-active-aborted-stop-stuck.summary"
expect_failure compare_rejects_active_aborted_stop_stuck \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-active-aborted-stop-stuck.summary"

sed 's/native_only_route:true/native_only_route:false/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-route-not-exclusive.summary"
expect_failure verify_rejects_non_native_only_route \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-route-not-exclusive.summary"
expect_failure compare_rejects_non_native_only_route \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-route-not-exclusive.summary"

sed '/phase=boot-sync-ready /s/status=idle/status={status:idle,native_only_route:true}/' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-boot-sync-json-status.summary"
expect_failure verify_rejects_boot_sync_json_status \
    sh "$VERIFY" --full \
    "$TMP_DIR/native-boot-sync-json-status.summary"
expect_failure compare_rejects_boot_sync_json_status \
    sh "$COMPARE" "$STOCK_SUMMARY" \
    "$TMP_DIR/native-boot-sync-json-status.summary"

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

sed 's/bytes_per_second=500/bytes_per_second=400/g' \
    "$NATIVE_SUMMARY" > "$TMP_DIR/native-throughput-regressed.summary"
expect_failure compare_strict_rejects_lower_throughput \
    sh "$COMPARE" --require-reduction "$STOCK_SUMMARY" \
    "$TMP_DIR/native-throughput-regressed.summary"

expect_failure smoke_requires_physical_ack \
    sh "$SMOKE" --heat \
    --summary "$TMP_DIR/physical-gate.summary" \
    --log "$TMP_DIR/physical-gate.log"
if ! grep -Eq 'phase=physical-safety-gate .*rc=2 .*reason=missing_physical_ok' \
    "$TMP_DIR/physical-gate.summary"; then
    cat "$TMP_DIR/physical-gate.summary" >&2
    echo "FAIL: physical safety gate did not write expected summary" >&2
    exit 1
fi
expect_failure smoke_rejects_physical_bundle \
    sh "$SMOKE" --physical-ok --heat --motion \
    --summary "$TMP_DIR/physical-bundle-gate.summary" \
    --log "$TMP_DIR/physical-bundle-gate.log"
if ! grep -Eq 'phase=physical-bundle-safety-gate .*rc=2 .*reason=missing_physical_bundle_ok .*count=2' \
    "$TMP_DIR/physical-bundle-gate.summary"; then
    cat "$TMP_DIR/physical-bundle-gate.summary" >&2
    echo "FAIL: physical bundle safety gate did not write expected summary" >&2
    exit 1
fi

sh "$SMOKE" --make-complete-fixture "$TMP_DIR/z-complete.gcode" 24 \
    --summary "$TMP_DIR/z-complete.summary" \
    --log "$TMP_DIR/z-complete.log"
if [ "$(grep -c '^G1 Z' "$TMP_DIR/z-complete.gcode")" != "24" ]; then
    cat "$TMP_DIR/z-complete.gcode"
    echo "FAIL: generated completion fixture did not contain 24 Z move commands" >&2
    exit 1
fi
if grep -Eq '^G1 Z0\.20' "$TMP_DIR/z-complete.gcode"; then
    cat "$TMP_DIR/z-complete.gcode"
    echo "FAIL: generated completion fixture moves toward Z max after homing" >&2
    exit 1
fi
if [ "$(grep -c '^G1 Z-0\.20 F30$' "$TMP_DIR/z-complete.gcode")" != "24" ]; then
    cat "$TMP_DIR/z-complete.gcode"
    echo "FAIL: generated completion fixture does not move away from homed Z max" >&2
    exit 1
fi
if ! grep -qx 'G28 Z' "$TMP_DIR/z-complete.gcode" ||
   ! grep -qx 'G91' "$TMP_DIR/z-complete.gcode" ||
   ! grep -qx 'G90' "$TMP_DIR/z-complete.gcode"; then
    cat "$TMP_DIR/z-complete.gcode"
    echo "FAIL: generated completion fixture missing Z-home/relative/absolute guards" >&2
    exit 1
fi
if grep -Eq '^(G4|M400|M104|M109|M140|M190|M105)([[:space:]]|$)' \
    "$TMP_DIR/z-complete.gcode"; then
    cat "$TMP_DIR/z-complete.gcode"
    echo "FAIL: generated completion fixture contains dwell/sync commands" >&2
    exit 1
fi
if ! grep -Eq 'phase=make-complete-fixture .*rc=0 .*command=G1_Z' \
    "$TMP_DIR/z-complete.summary"; then
    cat "$TMP_DIR/z-complete.summary"
    echo "FAIL: generated completion fixture summary missing success evidence" >&2
    exit 1
fi
echo "PASS: generated bounded Z completion fixture"

sh "$SMOKE" --make-active-fixture "$TMP_DIR/z-active.gcode" 36 \
    --summary "$TMP_DIR/z-active.summary" \
    --log "$TMP_DIR/z-active.log"
if [ "$(grep -c '^G1 Z' "$TMP_DIR/z-active.gcode")" != "36" ]; then
    cat "$TMP_DIR/z-active.gcode"
    echo "FAIL: generated active fixture did not contain 36 Z move commands" >&2
    exit 1
fi
if grep -Eq '^(G28|G4|M400|M104|M109|M140|M190|M105)([[:space:]]|$)' \
    "$TMP_DIR/z-active.gcode"; then
    cat "$TMP_DIR/z-active.gcode"
    echo "FAIL: generated active fixture contains home, dwell, sync, heat, or polling commands" >&2
    exit 1
fi
if grep -Eq '^G1 [XYE]' "$TMP_DIR/z-active.gcode"; then
    cat "$TMP_DIR/z-active.gcode"
    echo "FAIL: generated active fixture moves X/Y/E axes" >&2
    exit 1
fi
if [ "$(grep -c '^G1 Z-0\.20 F30$' "$TMP_DIR/z-active.gcode")" != "36" ]; then
    cat "$TMP_DIR/z-active.gcode"
    echo "FAIL: generated active fixture does not move away from homed Z max" >&2
    exit 1
fi
if ! grep -qx 'G91' "$TMP_DIR/z-active.gcode" ||
   ! grep -qx 'G90' "$TMP_DIR/z-active.gcode"; then
    cat "$TMP_DIR/z-active.gcode"
    echo "FAIL: generated active fixture missing relative/absolute guards" >&2
    exit 1
fi
if ! grep -Eq 'phase=make-active-fixture .*rc=0 .*command=G1_Z' \
    "$TMP_DIR/z-active.summary"; then
    cat "$TMP_DIR/z-active.summary"
    echo "FAIL: generated active fixture summary missing success evidence" >&2
    exit 1
fi
echo "PASS: generated bounded Z active fixture"

expect_failure smoke_rejects_oversized_active_fixture \
    sh "$SMOKE" --make-active-fixture "$TMP_DIR/z-active-too-large.gcode" 481 \
    --summary "$TMP_DIR/z-active-too-large.summary" \
    --log "$TMP_DIR/z-active-too-large.log"

sh "$SMOKE" --make-preheat-abort-fixture "$TMP_DIR/preheat-abort.gcode" \
    --summary "$TMP_DIR/preheat-abort-fixture.summary" \
    --log "$TMP_DIR/preheat-abort-fixture.log"
if ! grep -qx 'M140 S35' "$TMP_DIR/preheat-abort.gcode" ||
   ! grep -qx 'M109 S45' "$TMP_DIR/preheat-abort.gcode" ||
   ! grep -qx 'M104 S0' "$TMP_DIR/preheat-abort.gcode" ||
   ! grep -qx 'M140 S0' "$TMP_DIR/preheat-abort.gcode"; then
    cat "$TMP_DIR/preheat-abort.gcode"
    echo "FAIL: generated preheat-abort fixture missing low heat/cooldown commands" >&2
    exit 1
fi
if grep -Eq '^G1 [XYE]' "$TMP_DIR/preheat-abort.gcode"; then
    cat "$TMP_DIR/preheat-abort.gcode"
    echo "FAIL: generated preheat-abort fixture moves X/Y/E axes" >&2
    exit 1
fi
if grep -Eq '^(G4|M400|M190|M105)([[:space:]]|$)' \
    "$TMP_DIR/preheat-abort.gcode"; then
    cat "$TMP_DIR/preheat-abort.gcode"
    echo "FAIL: generated preheat-abort fixture contains dwell/sync/poll commands" >&2
    exit 1
fi
if ! grep -Eq 'phase=make-preheat-abort-fixture .*rc=0 .*command=M109_low_target' \
    "$TMP_DIR/preheat-abort-fixture.summary"; then
    cat "$TMP_DIR/preheat-abort-fixture.summary"
    echo "FAIL: generated preheat-abort fixture summary missing success evidence" >&2
    exit 1
fi
echo "PASS: generated low-temperature preheat-abort fixture"

cat > "$TMP_DIR/dwell-only.gcode" <<'EOF'
; Bad completion fixture: this can sit in firmware dwell/sync behavior.
G4 P1000
M400
EOF
expect_failure smoke_rejects_dwell_only_completion_fixture \
    sh "$SMOKE" --physical-ok --complete-job "$TMP_DIR/dwell-only.gcode" \
    --summary "$TMP_DIR/dwell-only.summary" \
    --log "$TMP_DIR/dwell-only.log"
if ! grep -Eq 'phase=complete-job-fixture-check .*rc=1 .*reason=dwell_only' \
    "$TMP_DIR/dwell-only.summary"; then
    cat "$TMP_DIR/dwell-only.summary"
    echo "FAIL: dwell-only completion fixture failure did not write expected summary" >&2
    exit 1
fi
echo "PASS: dwell-only completion fixture writes actionable summary"

echo "deneb-printsvc smoke verifier selftest passed"
