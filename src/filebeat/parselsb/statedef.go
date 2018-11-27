package parselsb

import (
	"github.com/elastic/beats/libbeat/logp"
)

const (
	PEND = iota
	PSUSP
	RUN
	SSUSP
	USUSP
	WAIT
	UNKWN
	PDONE
	PERR
	DONE_PDONE
	DONE_WAIT
	DONE_PERR
	ERROR
	DONE
	EXIT
)

type stateWithReason struct {
	state  int
	reason string
}

type stateMessage struct {
	cluster      string
	jobId        int
	jobIdx       int
	lastState    int
	currentState int
	reason       string
}

// get static state and reason
var eventStateMapping map[string]stateWithReason

var stateNameMapping map[int]string

var exitReason map[int]string

var jobPropertyMapping map[string](map[string]string)

func init() {
	initEventStateMapping()
	initStateNameMapping()
	initExitReasonMapping()
}

func initEventStateMapping() {
	eventStateMapping = make(map[string]stateWithReason)
	eventStateMapping["JOB_NEW"] = stateWithReason{state: PEND, reason: "new job submitted"}
	eventStateMapping["JOB_START_ACCEPT"] = stateWithReason{state: RUN, reason: "job starts"}
}

func initStateNameMapping() {
	stateNameMapping = make(map[int]string)
	stateNameMapping[0] = "PEND"
	stateNameMapping[1] = "PSUSP"
	stateNameMapping[2] = "RUN"
	stateNameMapping[3] = "SSUSP"
	stateNameMapping[4] = "USUSP"
	stateNameMapping[5] = "WAIT"
	stateNameMapping[6] = "UNKWN"
	stateNameMapping[7] = "PDONE"
	stateNameMapping[8] = "PERR"
	stateNameMapping[9] = "DONE+PDONE"
	stateNameMapping[10] = "DONE+WAIT"
	stateNameMapping[11] = "DONE+PERR"
	stateNameMapping[12] = "ERROR"
	stateNameMapping[13] = "DONE"
	stateNameMapping[14] = "EXIT"
}

func initExitReasonMapping() {
	exitReason = make(map[int]string)
	exitReason[0] = "job exited, reason unknown TERM_UNKNOWN"
	exitReason[1] = "job killed after preemption TERM_PREEMPT"
	exitReason[2] = "job killed after queue run window is closed TERM_WINDOW"
	exitReason[3] = "job killed after load exceeds threshold TERM_LOAD"
	exitReason[4] = "job exited, reason unknown TERM_OTHER"
	exitReason[5] = "job killed after reaching LSF run time limit TERM_RUNLIMIT"
	exitReason[6] = "job killed after deadline expires TERM_DEADLINE"
	exitReason[7] = "job killed after reaching LSF process TERM_PROCESSLIMIT"
	exitReason[8] = "job killed by owner without time for cleanup TERM_FORCE_OWNER"
	exitReason[9] = "job killed by root or LSF administrator without time for cleanup TERM_FORCE_ADMIN"
	exitReason[10] = "job killed and requeued by owner TERM_REQUEUE_OWNER"
	exitReason[11] = "job killed and requeued by root or LSF administrator TERM_REQUEUE_ADMIN"
	exitReason[12] = "job killed after reaching LSF CPU usage limit TERM_CPULIMIT"
	exitReason[13] = "job killed after checkpointing TERM_CHKPNT"
	exitReason[14] = "job killed by owner TERM_OWNER"
	exitReason[15] = "job killed by root or an administrator TERM_ADMIN"
	exitReason[16] = "job killed after reaching LSF memory usage limit TERM_MEMLIMIT"
	exitReason[17] = "job killed by a signal external to lsf TERM_EXTERNAL_SIGNAL"
	exitReason[18] = "job terminated abnormally in RMS TERM_RMS"
	exitReason[19] = "job killed when LSF is not available TERM_ZOMBIE"
	exitReason[20] = "job killed after reaching LSF swap usage limit TERM_SWAP"
	exitReason[21] = "job killed after reaching LSF thread TERM_THREADLIMIT"
	exitReason[22] = "job terminated abnormally in SLURM TERM_SLURM"
	exitReason[23] = "job exited, reason unknown TERM_BUCKET_KILL"
	exitReason[24] = "job terminated after control PID died TERM_CTRL_PID"
	exitReason[25] = "Current working directory is not accessible or does not exist on the execution host TERM_CWD_NOTEXIST"
	exitReason[26] = "hung job removed from the LSF system TERM_REMOVE_HUNG_JOB"
	exitReason[27] = "TERM_ORPHAN_SYSTEM"
	exitReason[28] = "TERM_PRE_EXEC_FAIL"
	exitReason[29] = "TERM_DATA"
	exitReason[30] = "TERM_MC_RECALL"
	exitReason[31] = "TERM_RC_RECLAIM"
}

func getStateName(enum int) string {
	if str, ok := stateNameMapping[enum]; ok {
		return str
	} else {
		return ""
	}
}

func getExitReason(event map[string]interface{}) string {
	eventType := getString(event, "event_type")
	if eventType == "JOB_STATUS" {
		exitStatus := getInt(event, "exit_status")
		exitInfo := getInt(event, "exit_info")
		if exitStatus > 0 && exitInfo >= 0 && exitInfo <= 26 {
			if val, ok := exitReason[exitInfo]; ok {
				return val
			}
		}
	}

	return ""
}

func getState(event map[string]interface{}) *stateWithReason {
	eventType := getString(event, "event_type")
	if val, ok := eventStateMapping[eventType]; ok {
		return &val
	}

	if eventType == "JOB_STATUS" {
		status := getString(event, "job_status")
		exitReason := getExitReason(event)
		switch status {
		case "DONE":
			return &stateWithReason{state: DONE, reason: exitReason}
		case "EXIT":
			return &stateWithReason{state: EXIT, reason: exitReason}
		case "PEND":
			return &stateWithReason{state: PEND}
		case "PSUSP":
			return &stateWithReason{state: PSUSP}
		case "RUN":
			return &stateWithReason{state: RUN}
		case "SSUSP":
			return &stateWithReason{state: SSUSP}
		case "USUSP":
			return &stateWithReason{state: USUSP}
		case "PDONE":
			return &stateWithReason{state: PDONE, reason: exitReason}
		case "PERR":
			return &stateWithReason{state: PERR, reason: exitReason}
		case "WAIT":
			return &stateWithReason{state: WAIT}
		case "UNKWN":
			return &stateWithReason{state: UNKWN, reason: exitReason}
		case "DONE+PDONE":
			return &stateWithReason{state: DONE_PDONE, reason: exitReason}
		case "DONE+WAIT":
			return &stateWithReason{state: DONE_WAIT, reason: exitReason}
		case "DONE+PERR":
			return &stateWithReason{state: DONE_PERR, reason: exitReason}
		case "ERROR":
			return &stateWithReason{state: ERROR, reason: exitReason}
		default:
			logp.Err("Unsupported job status - %s", status)
		}
	}

	return nil
}
