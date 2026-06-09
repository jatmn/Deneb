#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Verify compact evidence produced by deneb-printsvc-smoke. This intentionally
# stays shell/awk/grep-only so it can run on the target without Python.

set -u

SUMMARY="${DENEB_PRINTSVC_SMOKE_SUMMARY:-/tmp/deneb-printsvc-smoke.summary}"
REQUIRE_NATIVE=0
REQUIRE_IDLE=0
REQUIRE_HEAT=0
REQUIRE_MOTION=0
REQUIRE_MACRO=0
REQUIRE_LOCAL_JOB=0
REQUIRE_JOB=0
REQUIRE_CURA_JOB=0
REQUIRE_PREHEAT_ABORT=0
REQUIRE_ACTIVE_ABORT=0
REQUIRE_PAUSE_RESUME=0
REQUIRE_COMPLETE_JOB=0
REQUIRE_RESTART=0
REQUIRE_RESOURCES=0
REQUIRE_BOOT_SYNC=0
REQUIRE_CLIENT_PROOF=0

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke-verify [options] [summary-file]

Checks a deneb-printsvc-smoke summary for required evidence.

Options:
  --native      Require native route enable evidence
  --idle        Require initial idle status and inactive stop state
  --heat        Require heat/cool evidence
  --motion      Require Z-home motion evidence
  --macro       Require macro-backed manual action evidence
  --local-job   Require native USB/local JOB acceptance evidence
  --job         Require multipart job start and abort/stop evidence
  --cura-job    Require Cura cluster API job start and abort evidence
  --preheat-abort
               Require quick abort evidence during preparation/preheat
  --active-abort
               Require abort evidence after active printing has started
  --pause-resume
               Require pause and resume evidence during --job
  --complete-job
               Require a job run to leave the active-job API without abort
  --restart     Require native print-service restart recovery evidence
  --resources   Require resource evidence: uptime, memory, CPU, and load samples
  --boot-sync   Require bounded boot/backend readiness evidence
  --client-proof
               Require observe-only local client API/bridge evidence
  --full        Require native, heat, motion, and job evidence
  -h, --help    Show this help

The verifier returns nonzero if required evidence is missing or if any recorded
API phase has a nonzero rc value.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --native) REQUIRE_NATIVE=1 ;;
        --idle) REQUIRE_IDLE=1 ;;
        --heat) REQUIRE_HEAT=1 ;;
        --motion) REQUIRE_MOTION=1 ;;
        --macro) REQUIRE_MACRO=1 ;;
        --local-job)
            REQUIRE_NATIVE=1
            REQUIRE_LOCAL_JOB=1
            ;;
        --job) REQUIRE_JOB=1 ;;
        --cura-job) REQUIRE_CURA_JOB=1 ;;
        --preheat-abort) REQUIRE_PREHEAT_ABORT=1 ;;
        --active-abort) REQUIRE_ACTIVE_ABORT=1 ;;
        --pause-resume)
            REQUIRE_JOB=1
            REQUIRE_PAUSE_RESUME=1
            ;;
        --complete-job) REQUIRE_COMPLETE_JOB=1 ;;
        --restart)
            REQUIRE_NATIVE=1
            REQUIRE_RESTART=1
            ;;
        --resources) REQUIRE_RESOURCES=1 ;;
        --boot-sync) REQUIRE_BOOT_SYNC=1 ;;
        --client-proof) REQUIRE_CLIENT_PROOF=1 ;;
        --full)
            REQUIRE_NATIVE=1
            REQUIRE_IDLE=1
            REQUIRE_HEAT=1
            REQUIRE_MOTION=1
            REQUIRE_MACRO=1
            REQUIRE_LOCAL_JOB=1
            REQUIRE_JOB=1
            REQUIRE_CURA_JOB=1
            REQUIRE_PREHEAT_ABORT=1
            REQUIRE_ACTIVE_ABORT=1
            REQUIRE_PAUSE_RESUME=1
            REQUIRE_COMPLETE_JOB=1
            REQUIRE_RESTART=1
            REQUIRE_RESOURCES=1
            REQUIRE_BOOT_SYNC=1
            REQUIRE_CLIENT_PROOF=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            SUMMARY="$1"
            ;;
    esac
    shift
done

failures=0

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

pass() {
    echo "PASS: $*"
}

require_pattern() {
    pattern="$1"
    label="$2"
    if grep -Eq "$pattern" "$SUMMARY"; then
        pass "$label"
    else
        fail "$label"
    fi
}

reject_pattern() {
    pattern="$1"
    label="$2"
    if grep -Eq "$pattern" "$SUMMARY"; then
        fail "$label"
    else
        pass "$label"
    fi
}

require_completion_runtime_evidence() {
    if grep -Eq ' phase=status-complete-job-running .*rc=0 .*status=printing' "$SUMMARY" &&
       grep -Eq ' phase=printer-complete-job-running .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "$SUMMARY"; then
        pass "completion job-running status/stop evidence captured"
        return
    fi

    if grep -Eq ' phase=status-complete-job-running .*rc=0 .*status=idle' "$SUMMARY" &&
       grep -Eq ' phase=printer-complete-job-running .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "$SUMMARY" &&
       grep -Eq ' phase=job-completion-wait .*elapsed=0 .*rc=0' "$SUMMARY"; then
        pass "completion finished before delayed running snapshot"
        return
    fi

    fail "completion runtime evidence is neither active-running nor fast-completed"
}

if [ ! -s "$SUMMARY" ]; then
    echo "summary file missing or empty: $SUMMARY" >&2
    exit 1
fi

require_pattern '(^| )start .*native=' "start record present"
require_pattern ' phase=printsvc-self-test rc=0' "printsvc self-test passed"
require_pattern ' snapshot=initial' "initial snapshot present"
require_pattern ' snapshot=final' "final snapshot present"
require_pattern ' phase=route-initial .*rc=0 .*body=.*native_only_route:true' "initial native-only route query passed"
require_pattern ' phase=status-initial .*rc=0 .*status=' "initial status query passed with body"
require_pattern ' phase=printer-initial .*rc=0 .*body=' "initial printer root query passed with body"
require_pattern 'sample=initial .*mem_total_kb=' "initial memory sample present"
require_pattern 'sample=initial .*uptime_seconds=' "initial uptime sample present"
require_pattern 'sample=initial .*cpu_total_jiffies=' "initial CPU sample present"
require_pattern 'sample=initial .*load1=' "initial load sample present"

if grep -Eq ' phase=[^ ]+ .*rc=[1-9][0-9]*' "$SUMMARY"; then
    fail "one or more recorded phases failed"
else
    pass "no recorded phase failures"
fi

if [ "$REQUIRE_NATIVE" = "1" ]; then
    require_pattern ' phase=native-route-enabled ' "native route enabled"
    require_pattern ' phase=native-driver-process .*deneb_printsvc=1 .*print_service_py=0 .*rc=0' "native driver process owns marlindriver route"
    reject_pattern 'sample=[^ ]+ .*command="?.*print_service[.]py' "native summary has no stock print_service.py process samples"
    require_pattern ' snapshot=native-enabled' "native route snapshot present"
    require_pattern ' phase=route-native-enabled .*rc=0 .*body=.*print_backend:native.*native_only_route:true' "native-only route query passed"
    require_pattern ' phase=status-native-enabled .*rc=0 .*status=' "native status query passed"
    require_pattern ' phase=printer-native-enabled .*rc=0' "native printer root query passed"
fi

if [ "$REQUIRE_IDLE" = "1" ]; then
    require_pattern ' phase=status-initial .*rc=0 .*status=idle' "initial status is idle"
    require_pattern ' phase=printer-initial .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "initial idle native active/stop flags are false"
fi

if [ "$REQUIRE_HEAT" = "1" ]; then
    require_pattern ' phase=heat-safety .*kind=physical .*axes=none .*required_home=none .*travel=bed_to_40C_nozzle_to_50C_then_cooldown .*rc=0' "heat physical safety plan present"
    require_pattern ' phase=bed-low-heat .*rc=0' "bed heat command passed"
    require_pattern ' phase=nozzle-low-heat .*rc=0' "nozzle heat command passed"
    require_pattern ' snapshot=heating' "heating snapshot present"
    require_pattern ' phase=status-heating .*rc=0 .*status=' "heating status query passed"
    require_pattern ' phase=printer-heating .*rc=0 .*body=' "heating printer root query passed"
    require_pattern ' phase=bed-cooldown .*rc=0' "bed cooldown command passed"
    require_pattern ' phase=nozzle-cooldown .*rc=0' "nozzle cooldown command passed"
    require_pattern ' snapshot=cooldown' "cooldown snapshot present"
    require_pattern ' phase=status-cooldown .*rc=0 .*status=' "cooldown status query passed"
    require_pattern ' phase=printer-cooldown .*rc=0 .*body=' "cooldown printer root query passed"
fi

if [ "$REQUIRE_MOTION" = "1" ]; then
    require_pattern ' phase=motion-safety .*kind=physical .*axes=Z .*required_home=Z .*travel=z_home_to_max_only .*rc=0' "motion physical safety plan present"
    require_pattern ' phase=z-home .*rc=0' "Z-home command passed"
    require_pattern ' snapshot=motion' "motion snapshot present"
    require_pattern ' phase=status-motion .*rc=0 .*status=' "motion status query passed"
    require_pattern ' phase=printer-motion .*rc=0 .*body=' "motion printer root query passed"
fi

if [ "$REQUIRE_MACRO" = "1" ]; then
    require_pattern ' phase=macro-safety .*kind=physical .*axes=(XYZ|Z) .*required_home=(XYZ|Z) .*rc=0' "macro physical safety plan present"
    require_pattern ' phase=macro-(home|bed_up|bed_down) .*rc=0' "macro-backed action passed"
    require_pattern ' snapshot=macro' "macro snapshot present"
    require_pattern ' phase=status-macro .*rc=0 .*status=' "macro status query passed"
    require_pattern ' phase=printer-macro .*rc=0 .*body=' "macro printer root query passed"
fi

if [ "$REQUIRE_LOCAL_JOB" = "1" ]; then
    require_pattern ' phase=local-job-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' "local-job physical safety plan present"
    require_pattern ' phase=local-job-native .*rc=0' "native local job smoke passed"
    require_pattern ' phase=local-job-start .*source=USB .*rc=0' "local job source is USB"
    require_pattern ' phase=local-job-accepted .*deneb_state=pre_print .*native_active=true .*native_stop_allowed=true .*source=USB .*rc=0' "local job native accepted state is stoppable"
    require_pattern ' phase=local-job-abort .*rc=0' "local job abort passed"
    require_pattern ' phase=local-job-aborted-state .*deneb_state=idle .*native_active=false .*native_stop_allowed=false .*rc=0' "local job native aborted state is idle"
fi

if [ "$REQUIRE_RESTART" = "1" ]; then
    require_pattern ' phase=service-restart .*rc=0' "service restart passed"
    require_pattern ' snapshot=service-restarted' "service restart snapshot present"
    require_pattern ' phase=route-service-restarted .*rc=0 .*body=.*native_only_route:true' "post-restart native-only route query passed"
    require_pattern ' phase=status-service-restarted .*rc=0' "post-restart status query passed"
    require_pattern ' phase=printer-service-restarted .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "post-restart idle native active/stop flags are false"
fi

if [ "$REQUIRE_BOOT_SYNC" = "1" ]; then
    require_pattern ' phase=boot-sync-ready .*elapsed_seconds=[0-9]+ .*uptime_delta_seconds=[0-9]+ .*rc=0' "boot sync reached readiness"
    require_pattern ' phase=boot-sync-ready .*route_body=[^ ]*native_only_route:true' "boot sync route body has native-only route evidence"
    require_pattern ' phase=boot-sync-ready .*status=(idle|printing|paused|error|offline|finished) ' "boot sync status is scalar"
fi

if [ "$REQUIRE_CLIENT_PROOF" = "1" ]; then
    require_pattern ' snapshot=client-proof' "client-proof snapshot present"
    require_pattern ' phase=client-deneb-route .*rc=0 .*body=.*native_only_route:true' "client Deneb route reports native-only"
    require_pattern ' phase=client-um-status .*rc=0 .*status=' "client UM status endpoint responds"
    require_pattern ' phase=client-um-printer .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "client UM printer root has idle native flags"
    require_pattern ' phase=client-um-system .*rc=0 .*body=' "client UM system endpoint responds"
    require_pattern ' phase=client-cura-printers .*rc=0 .*body=' "client Cura printers endpoint responds"
    require_pattern ' phase=client-cura-print-jobs .*rc=0 .*body=' "client Cura print_jobs endpoint responds"
    require_pattern ' phase=client-cura-materials .*rc=0 .*body=' "client Cura materials endpoint responds"
    require_pattern ' phase=client-digital-factory-status .*installed=1 .*body=.*(status_timeout|state_[A-Za-z0-9_.:-]+|accepted_[01])' "client Digital Factory bridge reports status"
    require_pattern ' phase=client-proof-complete .*rc=0' "client-proof completed"
fi

if [ "$REQUIRE_RESOURCES" = "1" ]; then
    require_pattern 'sample=final .*mem_total_kb=' "final memory sample present"
    require_pattern 'sample=final .*uptime_seconds=' "final uptime sample present"
    require_pattern 'sample=final .*cpu_total_jiffies=' "final CPU sample present"
    require_pattern 'sample=final .*load1=' "final load sample present"
    require_pattern 'sample=initial pid=[0-9]+ .*vmrss_kb=' "initial process RSS sample present"
    require_pattern 'sample=final pid=[0-9]+ .*vmrss_kb=' "final process RSS sample present"
    require_pattern ' phase=job-throughput .*bytes=[1-9][0-9]* .*elapsed_seconds=[1-9][0-9]* .*bytes_per_second=[1-9][0-9]* .*rc=0' "resource mode includes print throughput"
fi

if [ "$REQUIRE_JOB" = "1" ]; then
    if [ "$REQUIRE_PAUSE_RESUME" = "1" ]; then
        require_pattern ' phase=job-safety .*kind=physical .*axes=XYZ .*required_home=home .*rc=0' "pause/resume job physical safety plan uses all-axis home"
    else
        require_pattern ' phase=job-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' "job physical safety plan present"
    fi
    require_pattern ' phase=job-start .*rc=0' "job start passed"
    require_pattern ' snapshot=job-running' "job-running snapshot present"
    require_pattern ' phase=status-job-running .*rc=0 .*status=printing' "job-running status is printing"
    require_pattern ' phase=printer-job-running .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "job-running native active/stop flags are true"
    if [ "$REQUIRE_PAUSE_RESUME" = "1" ]; then
        require_pattern ' phase=job-pause .*rc=0' "job pause passed"
        require_pattern ' snapshot=job-paused' "job-paused snapshot present"
        require_pattern ' phase=status-job-paused .*rc=0 .*status=paused' "job-paused status is paused"
        require_pattern ' phase=printer-job-paused .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "job-paused native active/stop flags are true"
        require_pattern ' phase=job-resume .*rc=0' "job resume passed"
        require_pattern ' snapshot=job-resumed' "job-resumed snapshot present"
        require_pattern ' phase=status-job-resumed .*rc=0 .*status=printing' "job-resumed status is printing"
        require_pattern ' phase=printer-job-resumed .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "job-resumed native active/stop flags are true"
    fi
    if grep -Eq ' phase=job-abort .*rc=0| phase=job-stop .*rc=0' "$SUMMARY"; then
        pass "job abort/stop passed"
    else
        fail "job abort/stop passed"
    fi
    require_pattern ' snapshot=job-abort-requested' "job abort-requested snapshot present"
    require_pattern ' snapshot=job-abort-draining' "job abort-draining snapshot present"
    require_pattern ' snapshot=job-aborted' "job-aborted snapshot present"
    require_pattern ' phase=status-job-aborted .*rc=0 .*status=idle' "job-aborted status is idle"
    require_pattern ' phase=printer-job-aborted .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "job-aborted native active/stop flags are false"
fi

if [ "$REQUIRE_CURA_JOB" = "1" ]; then
    require_pattern ' phase=cura-job-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' "Cura job physical safety plan present"
    require_pattern ' phase=cura-job-start .*rc=0' "Cura job start passed"
    require_pattern ' snapshot=cura-job-running' "Cura job-running snapshot present"
    require_pattern ' phase=status-cura-job-running .*rc=0 .*status=printing' "Cura job-running status is printing"
    require_pattern ' phase=printer-cura-job-running .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "Cura job-running native active/stop flags are true"
    require_pattern ' phase=cura-job-abort .*rc=0' "Cura job abort passed"
    require_pattern ' snapshot=cura-job-abort-requested' "Cura job abort-requested snapshot present"
    require_pattern ' snapshot=cura-job-abort-draining' "Cura job abort-draining snapshot present"
    require_pattern ' snapshot=cura-job-aborted' "Cura job-aborted snapshot present"
    require_pattern ' phase=status-cura-job-aborted .*rc=0 .*status=idle' "Cura job-aborted status is idle"
    require_pattern ' phase=printer-cura-job-aborted .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "Cura job-aborted native active/stop flags are false"
fi

if [ "$REQUIRE_PREHEAT_ABORT" = "1" ]; then
    require_pattern ' phase=preheat-abort-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' "preheat-abort physical safety plan present"
    require_pattern ' phase=preheat-abort-start .*rc=0' "preheat abort job start passed"
    require_pattern ' snapshot=preheat-abort-active' "preheat-abort active snapshot present"
    require_pattern ' phase=status-preheat-abort-active .*rc=0 .*status=printing' "preheat-abort active status is printing"
    require_pattern ' phase=printer-preheat-abort-active .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "preheat-abort native active/stop flags are true"
    if grep -Eq ' phase=preheat-abort .*rc=0| phase=preheat-stop .*rc=0' "$SUMMARY"; then
        pass "preheat abort/stop passed"
    else
        fail "preheat abort/stop passed"
    fi
    require_pattern ' snapshot=preheat-abort-requested' "preheat-abort requested snapshot present"
    require_pattern ' phase=status-preheat-abort-requested .*rc=0 .*status=aborting' "preheat-abort requested status is aborting"
    require_pattern ' phase=printer-preheat-abort-requested .*rc=0 .*body=.*native_active:true.*native_stop_allowed:false' "preheat-abort requested native active stays true and stop is disabled"
    require_pattern ' snapshot=preheat-abort-draining' "preheat-abort draining snapshot present"
    if grep -Eq ' phase=status-preheat-abort-draining .*rc=0 .*status=aborting' "$SUMMARY"; then
        require_pattern ' phase=printer-preheat-abort-draining .*rc=0 .*body=.*native_active:true.*native_stop_allowed:false' "preheat-abort draining abort state keeps native active and stop disabled"
    else
        require_pattern ' phase=status-preheat-abort-draining .*rc=0 .*status=idle' "preheat-abort draining status is aborting or idle after cleanup"
        require_pattern ' phase=printer-preheat-abort-draining .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "preheat-abort draining idle state clears native active/stop flags"
    fi
    require_pattern ' snapshot=preheat-aborted' "preheat-aborted snapshot present"
    require_pattern ' phase=status-preheat-aborted .*rc=0 .*status=idle' "preheat-aborted status is idle"
    require_pattern ' phase=printer-preheat-aborted .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "preheat-aborted native active/stop flags are false"
fi

if [ "$REQUIRE_ACTIVE_ABORT" = "1" ]; then
    require_pattern ' phase=active-abort-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' "active-abort physical safety plan present"
    require_pattern ' phase=active-abort-start .*rc=0' "active abort job start passed"
    require_pattern ' snapshot=active-abort-printing' "active-abort printing snapshot present"
    require_pattern ' phase=status-active-abort-printing .*rc=0 .*status=printing' "active-abort printing status is printing"
    require_pattern ' phase=printer-active-abort-printing .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "active-abort native active/stop flags are true"
    if grep -Eq ' phase=active-abort .*rc=0| phase=active-stop .*rc=0' "$SUMMARY"; then
        pass "active abort/stop passed"
    else
        fail "active abort/stop passed"
    fi
    require_pattern ' snapshot=active-abort-requested' "active-abort requested snapshot present"
    require_pattern ' phase=status-active-abort-requested .*rc=0 .*status=aborting' "active-abort requested status is aborting"
    require_pattern ' phase=printer-active-abort-requested .*rc=0 .*body=.*native_active:true.*native_stop_allowed:false' "active-abort requested native active stays true and stop is disabled"
    require_pattern ' snapshot=active-abort-draining' "active-abort draining snapshot present"
    if grep -Eq ' phase=status-active-abort-draining .*rc=0 .*status=aborting' "$SUMMARY"; then
        require_pattern ' phase=printer-active-abort-draining .*rc=0 .*body=.*native_active:true.*native_stop_allowed:false' "active-abort draining abort state keeps native active and stop disabled"
    else
        require_pattern ' phase=status-active-abort-draining .*rc=0 .*status=idle' "active-abort draining status is aborting or idle after cleanup"
        require_pattern ' phase=printer-active-abort-draining .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "active-abort draining idle state clears native active/stop flags"
    fi
    require_pattern ' snapshot=active-aborted' "active-aborted snapshot present"
    require_pattern ' phase=status-active-aborted .*rc=0 .*status=idle' "active-aborted status is idle"
    require_pattern ' phase=printer-active-aborted .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "active-aborted native active/stop flags are false"
fi

if [ "$REQUIRE_COMPLETE_JOB" = "1" ]; then
    require_pattern ' phase=complete-job-safety .*kind=physical .*axes=Z .*required_home=(z_home|home) .*travel=bounded_relative_Z_negative_max_96mm .*rc=0' "completion physical safety plan present"
    require_pattern ' phase=complete-job-fixture-check .*rc=0 .*reason=progress_command' "completion fixture passed non-dwell check"
    require_pattern ' phase=complete-job-start .*rc=0' "completion job start passed"
    require_pattern ' snapshot=complete-job-running' "completion job-running snapshot present"
    require_completion_runtime_evidence
    require_pattern ' phase=job-completion-wait .*rc=0' "completion wait observed inactive job"
    require_pattern ' phase=job-throughput .*bytes=[1-9][0-9]* .*elapsed_seconds=[1-9][0-9]* .*bytes_per_second=[1-9][0-9]* .*rc=0' "completion throughput sample present"
    require_pattern ' snapshot=job-completed' "job-completed snapshot present"
    require_pattern ' phase=status-job-completed .*rc=0 .*status=idle' "job-completed status is idle"
    require_pattern ' phase=printer-job-completed .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "job-completed native active/stop flags are false"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures verification failure(s)" >&2
    exit 1
fi

echo "summary verification passed: $SUMMARY"
