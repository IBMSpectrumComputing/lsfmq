/************************************************************************
 *
 * LSB Stream Parse JNI
 *
 * lsbevent_parse.c -- xxin, 2006-06-20
 *
 * The following routines are for parse event data string and convert to
 * java hashmap object, which invoke LSF function lsb_readeventrecord.
 *
 * EXPORTED ROUTINES:
 *
 * readlsbEvents() - parse event string to JSON string.
 * readlsbStream() - parse stream record string to JSON string.
 * readlsbAcct() - 	 parse accounting record string to JSON string.
 * readlsbStatus() - parse status string to JSON string.
 *
 * EXPORTED VARIABLES:
 *
 * NULL.
 *
 * TODO:
 *  - refactor JSON field names; define constants in the header instead
 *    of literals in the code
 *  - FIELD_JSTATUS and FIELD_JOB_STATUS duplicate FIELD_STATUS and 
 *    FIELD_STATUS_STR respectively. Leaving them here for now for 
 *    possible backward compatibility. Once the model is stabilized may be
 *    remove the former two.
 ************************************************************************/

#include "lsbevent_parse.h"
#include "json4c.h"
#include "job_array.h"
#include "lsbatch.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "lsbevent_fields.h"
#include <string.h>
#include <time.h>
#if defined(WIN32)
#include <windows.h>
#endif
#if defined(_HPUXIA64_)
#include <dlfcn.h>
#endif

#ifdef WIN32
#define WTERMSIG(x) (x & 0177)
#endif

/* define the long long int data type on different platform*/
#ifdef WIN32
typedef __int64 LONGLONG;
#define LONG_FORMAT "%I64d "
#else
typedef long long int LONGLONG;
#define LONG_FORMAT "%lld "
#endif /*WIN32*/

/*include files for Dynamic Lib*/
#if !defined(WIN32)
#if defined(hpux) || defined(__hpux)
#include <dl.h>
#else
#if defined(_MACOS_X_)
#include "dlfcn_mac.h"
#elif !defined(CRAY) && !defined(SX4)
#include <dlfcn.h>
#endif /* CRAY */
#endif
#endif /* WIN32 */

#define MAX_EVENT_TYPE_NAME_LEN 64

#define FREEUP(pointer)                                                        \
  if (pointer != NULL) {                                                       \
    free(pointer);                                                             \
    pointer = NULL;                                                            \
  }

#if defined(DEBUG)
#define TRACE(...)	printf(__VA_ARGS__)
#else
#define TRACE(...)	
#endif


/* Da stream object as loaded from the liblsbstream.so
 * The function addresses are exported by the library.
 */
struct streamer {
	void *handle;
	char *library;
#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	struct eventRec *(*lsb_readstreamlineMT)(const char *);
	void (*lsb_freelogrec)(struct eventRec *);
#else
	struct eventRec *(*lsb_readstreamline)(const char *);
#endif
};

/*It must be in accordance with the above definition*/
typedef struct eventRec *(*MOD_LOG_FUNCEVR)(void *);

static struct streamer stream = { 0 };

static char *timeConvert(long l) {
	if (l <= 0) { // don't convert "invalid" time
		return NULL;
	}
	time_t t = l;
	struct tm *p = localtime(&t);
	char *s = malloc(100);
	strftime(s, 100, "%FT%T%z", p);
	return s;
}

/*
 * *****************************************
 * Added by ZK on 2016-08-11
 * Hold the SYS_EXCEPT_MASK_MAPPING table
 * *****************************************
 */
const char* sysExitInfoMapping[30] = { "TERM_UNKNOWN", "TERM_PREEMPT",
		"TERM_WINDOW", "TERM_LOAD", "TERM_OTHER", "TERM_RUNLIMIT",
		"TERM_DEADLINE", "TERM_PROCESSLIMIT", "TERM_FORCE_OWNER",
		"TERM_FORCE_ADMIN", "TERM_REQUEUE_OWNER", "TERM_REQUEUE_ADMIN",
		"TERM_CPULIMIT", "TERM_CHKPNT", "TERM_OWNER", "TERM_ADMIN",
		"TERM_MEMLIMIT", "TERM_EXTERNAL_SIGNAL", "TERM_RMS", "TERM_ZOMBIE",
		"TERM_SWAP", "TERM_THREADLIMIT", "TERM_SLURM", "TERM_BUCKET_KILL",
		"TERM_CTRL_PID", "TERM_CWD_NOTEXIST", "TERM_REMOVE_HUNG_JOB",
		"TERM_ORPHAN_SYSTEM", "TERM_PRE_EXEC_FAIL", "TERM_DATA" };

/*
 * ****************************************
 * Added by ZK on 2016-08-11 to get related
 * EXCEPT_MASK_REASON from EXCEPT_MASK
 * ****************************************
 */
char* getExceptMaskReason(int mask) {
	switch (mask) {
	case 2:
		return "JOB_OVERRUN";
	case 4:
		return "JOB_UNDERRUN";
	case 128:
		return "JOB_IDLE";
	case 256:
		return "JOB_RUNTIME_EST_EXCEEDED";
	default:
		return NULL;
	}
}

// char* exitReason = formatExitReason(&(logrec->eventLog.jobFinish2Log))

#if defined(LSF10)
static char* formatExitReason(struct jobFinish2Log *jobFinish2Log) {
	int MAX_LEN = 1024;
	int exitInfo = jobFinish2Log->exitInfo;
	int exceptMask = jobFinish2Log->exceptMask;
	int exitStatus = jobFinish2Log->exitStatus;

	char* exitReason = (char*)malloc(MAX_LEN);

	if (exitReason == NULL) {
		return NULL;
	}

	if(jobFinish2Log->jStatus == JOB_STAT_DONE || jobFinish2Log->jStatus == JOB_STAT_PDONE) {
		sprintf(exitReason,"FINISHED_JOB");
	}
	else {
		if(exitInfo == 0 && exceptMask == 0) {
			sprintf(exitReason, "-");
		} else if(exitInfo > 0 && exitInfo < 30) {
			sprintf(exitReason, sysExitInfoMapping[exitInfo]);
		} else if(exceptMask > 0) {
			sprintf(exitReason, getExceptMaskReason(exceptMask));
		} else if(exitStatus == 0) {
			sprintf(exitReason, "RECALLED JOB");
		} else if(exitStatus > 255) {
			if((exitStatus >> 8) <= 128 || (exitStatus >> 8) > 165) {
				sprintf(exitReason, "APP EXIT: %d",(exitStatus >> 8));
			} else {
				sprintf(exitReason, "OS SIGNAL(U): %d", (exitStatus & 127));
			}
		} else {
			sprintf(exitReason, "OS SIGNAL(L): %d, OR APP EXIT: %d", (exitStatus >> 8) - 128, (exitStatus >> 8));
		}
	}
	return exitReason;
}
#endif
/*
 *-----------------------------------------------------------------------
 *
 * initstream --
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * obj[IN]: java object.
 *
 * DESCRIPTION:
 *
 * init stream function, get function pointer from library.
 *
 * RETURN:
 *
 * 0 on success, -1 on failure.
 *
 *-----------------------------------------------------------------------
 */
static int initstream(char **msg) {
	void *handle;

	/* only first startup to init. */
	if (NULL != stream.handle) {
		return 0;
	}

#if defined(WIN32)
	stream.library = "liblsbstream.dll";
#else
	stream.library = "liblsbstream.so";
#endif

	/* Load the stream library from the standard
	 * library location.
	 */
#if defined(WIN32)
	handle = LoadLibrary(stream.library);
	*msg = NULL;
#else
	handle = dlopen(stream.library, RTLD_LAZY);
	*msg = dlerror();
#endif /*WIN32*/
	if (!handle) {
		return -1;
	}
	stream.handle = handle;

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	/* check for the known symbols...
	 */
#if defined(WIN32)
	stream.lsb_readstreamlineMT =
	(MOD_LOG_FUNCEVR)GetProcAddress(handle, "lsb_readstreamlineMT");
#else
	stream.lsb_readstreamlineMT = dlsym(handle, "lsb_readstreamlineMT");
#endif /* WIN32 */
	if (stream.lsb_readstreamlineMT == NULL) {
		goto screwed;
	}

#if defined(WIN32)
	stream.lsb_freelogrec =
	(MOD_LOG_FUNCEVR)GetProcAddress(handle, "lsb_freelogrec");
#else
	stream.lsb_freelogrec = dlsym(handle, "lsb_freelogrec");
#endif /* WIN32 */
	if (stream.lsb_freelogrec == NULL) {
		goto screwed;
	}
#else /* LSB_EVENT_VERSION9_1 */
	/* check for the known symbols...
	 */
#if defined(WIN32)
	stream.lsb_readstreamline =
	(MOD_LOG_FUNCEVR)GetProcAddress(handle, "lsb_readstreamline");
#else
	stream.lsb_readstreamline = dlsym(handle, "lsb_readstreamline");
#endif /* WIN32 */
	if (stream.lsb_readstreamline == NULL) {
		goto screwed;
	}

#endif /* LSB_EVENT_VERSION9_1 */

	return (0);

	screwed: if (handle != NULL) {
#if defined(WIN32)
		FreeLibrary(handle);
#else
		dlclose(handle);
#endif /*WIN32*/
	}

	return (-1);

} /* initstream() */

/*
 *-----------------------------------------------------------------------
 *
 * putJobHEAD -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put FIELD_VERSION , FIELD_EVENT_TYPE, FIELD_EVENT_TIME  key/value to hashmap object.
 * these three keys is common part of all event type.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobHEAD(Json4c *objHashMap, struct eventRec *logrec) {
	// hashmap_put_string(env, objHashMap, HashMap_put, "version",
	// logrec->version);
	addStringToObject(objHashMap, FIELD_VERSION, logrec->version);

	char *time;
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHashMap, FIELD_EVENT_TIME, time);
	FREEUP(time);

	/* chang by guosheng on 2006-7-12 , fix a bug, change from timestamp to int*/
	addNumberToObject(objHashMap, FIELD_EVENT_TIME_UTC, (int)(logrec->eventTime));
}

/*
 *-----------------------------------------------------------------------------------------------------
 * transform the jstatus number to string.
 *
 #define JOB_STAT_NULL         0x00      0
 #define JOB_STAT_PEND         0x01      1
 #define JOB_STAT_PSUSP        0x02      2
 #define JOB_STAT_RUN          0x04        4
 #define JOB_STAT_SSUSP        0x08        8
 #define JOB_STAT_USUSP        0x10        16
 #define JOB_STAT_EXIT         0x20          32
 #define JOB_STAT_DONE         0x40v       64
 #define JOB_STAT_PDONE        (0x80)   Post job process done successfully  128
 #define JOB_STAT_PERR         (0x100) Post job process has error 256
 #define JOB_STAT_WAIT         (0x200)  Chunk job waiting its turn to exec 512
 #define JOB_STAT_UNKWN        0x10000 65536
 *--------------------------------------------------------------------------------------------------------
 */
static char *transformJstatus(int jstatus) {
	char *JstatusString;
	JstatusString = (char *) malloc(128 * sizeof(char));
	switch (jstatus) {
	case JOB_STAT_NULL:
		sprintf(JstatusString, "NULL");
		break;
	case JOB_STAT_PEND:
		sprintf(JstatusString, "PEND");
		break;
	case JOB_STAT_PSUSP:
		sprintf(JstatusString, "PSUSP");
		break;
	case JOB_STAT_RUN:
		sprintf(JstatusString, "RUN");
		break;
	case JOB_STAT_SSUSP:
		sprintf(JstatusString, "SSUSP");
		break;
	case JOB_STAT_USUSP:
		sprintf(JstatusString, "USUSP");
		break;
	case JOB_STAT_EXIT:
		sprintf(JstatusString, "EXIT");
		break;
	case JOB_STAT_DONE:
		sprintf(JstatusString, "DONE");
		break;
	case JOB_STAT_PDONE:
		sprintf(JstatusString, "PDONE");
		break;
	case JOB_STAT_PERR:
		sprintf(JstatusString, "PERR");
		break;
	case JOB_STAT_WAIT:
		sprintf(JstatusString, "WAIT");
		break;
	case JOB_STAT_UNKWN:
		sprintf(JstatusString, "UNKWN");
		break;

		/* complex job status*/
	case JOB_STAT_RUN + JOB_STAT_WAIT:
		sprintf(JstatusString, "WAIT");
		break;
	case JOB_STAT_DONE + JOB_STAT_PDONE:
		sprintf(JstatusString, "DONE+PDONE");
		break;
	case JOB_STAT_DONE + JOB_STAT_WAIT:
		sprintf(JstatusString, "DONE+WAIT");
		break;
	case JOB_STAT_DONE + JOB_STAT_PERR:
		sprintf(JstatusString, "DONE+PERR");
		break;
	default:
		sprintf(JstatusString, "ERROR");
	}

	return JstatusString;
}


const char* expandedExitInfoMapping[32] = { "TERM_UNKNOWN",
		"job killed after preemption TERM_PREEMPT",
		"job killed after queue run window is closed TERM_WINDOW",
		"job killed after load exceeds threshold TERM_LOAD",
		"job exited, reason unknown TERM_OTHER",
		"job killed after reaching LSF run time limit TERM_RUNLIMIT",
		"job killed after deadline expires TERM_DEADLINE",
		"job killed after reaching LSF process TERM_PROCESSLIMIT",
		"job killed by owner without time for cleanup TERM_FORCE_OWNER",
		"job killed by root or LSF administrator without time for cleanup TERM_FORCE_ADMIN",
		"job killed and requeued by owner TERM_REQUEUE_OWNER",
		"job killed and requeued by root or LSF administrator TERM_REQUEUE_ADMIN",
		"job killed after reaching LSF CPU usage limit TERM_CPULIMIT",
		"job killed after checkpointing TERM_CHKPNT",
		"job killed by owner TERM_OWNER",
		"job killed by root or an administrator TERM_ADMIN",
		"job killed after reaching LSF memory usage limit TERM_MEMLIMIT",
		"job killed by a signal external to lsf TERM_EXTERNAL_SIGNAL",
		"job terminated abnormally in RMS TERM_RMS",
		"job killed when LSF is not available TERM_ZOMBIE",
		"job killed after reaching LSF swap usage limit TERM_SWAP",
		"job killed after reaching LSF thread TERM_THREADLIMIT",
		"job terminated abnormally in SLURM TERM_SLURM",
		"job exited, reason unknown TERM_BUCKET_KILL",
		"job terminated after control PID died TERM_CTRL_PID",
		"TERM_CWD_NCurrent working directory is not accessible or does not exist on the execution host TERM_CWD_NOTEXISTOTEXIST",
		"hung job removed from the LSF system TERM_REMOVE_HUNG_JOB",
		"TERM_ORPHAN_SYSTEM",
		"TERM_PRE_EXEC_FAIL",
		"TERM_DATA",
		"TERM_MC_RECALL"
		"TERM_RC_RECLAIM"};

/*-----------------------------------------------------------------------------------------------------
 get job exit reason from jStatus, exitInfo, exceptMask and exitStatus using below logic

 (CASE
				WHEN A.JOB_EXIT_STATUS='DONE' THEN 'FINISHED_JOB'
                WHEN A.EXIT_INFO >0 THEN I.EXIT_INFO_REASON
				WHEN A.EXCEPT_MASK >0 THEN M.EXCEPT_MASK_REASON
                WHEN A.JOB_EXIT_CODE = 0 THEN 'RECALLED JOB'
				WHEN A.JOB_EXIT_CODE > 255 THEN
                     (CASE
                     WHEN (A.JOB_EXIT_CODE::INT >> 8) <= 128 OR (A.JOB_EXIT_CODE::INT >> 8) >165 THEN 'APP EXIT: '|| to_char( A.JOB_EXIT_CODE::INT >>8)
                     ELSE 'OS SIGNAL(U): '|| to_char((A.JOB_EXIT_CODE::INT >> 8) - 128) || ', OR APP EXIT: '|| to_char(A.JOB_EXIT_CODE::INT >> 8)
                     END)
				ELSE 'OS SIGNAL(L): '|| to_char(A.JOB_EXIT_CODE::INT & 127)
				END ) AS EXIT_REASON
 *----------------------------------------------------------------------------------------------------- */
static char * getExitReason(int jStatus, int exitInfo, int exceptMask, int exitStatus){
	char *exitReason;
	exitReason = (char *) malloc(128 * sizeof(char));

	if (jStatus == JOB_STAT_DONE){
		sprintf(exitReason, "job finished normally FINISHED_JOB");
	} else if (exitInfo > 0 && exitInfo <= 31) {
		sprintf(exitReason, expandedExitInfoMapping[exitInfo]);
	} else if (exceptMask > 0) {
		switch (exceptMask) {
		case 2:
			sprintf(exitReason, "JOB_OVERRUN");
			break;
		case 4:
			sprintf(exitReason, "JOB_UNDERRUN");
			break;
		case 128:
			sprintf(exitReason, "JOB_IDLE");
			break;
		case 256:
			sprintf(exitReason, "JOB_RUNTIME_EST_EXCEEDED");
			break;
		default:
			sprintf(exitReason, "invalid except mask value");
		}

	} else if (exitInfo == 0) {
		sprintf(exitReason, "RECALLED JOB");
	} else if(exitInfo > 255) {
		int shiftExitInfo = (exitInfo >> 8);
		if (shiftExitInfo <= 128 || shiftExitInfo > 165) {
			sprintf(exitReason, "APP EXIT: %d", shiftExitInfo);
		} else {
			sprintf(exitReason, "OS SIGNAL(U): %d, OR APP EXIT: %d", shiftExitInfo - 128, shiftExitInfo);
		}
	} else {
		sprintf(exitReason, "OS SIGNAL(L): %d", exitInfo & 127);
	}

	return exitReason;
}



#if defined(LSF8) || defined(LSF9) || defined(LSF10)
/* Potentially unsafe -- this function is called with both jobFinish2Log
 * and jobStatus2Log as input (nicki) */
// static int getNumExecProc(struct jobFinish2Log *jobFinish2Log) {
static int getNumExecProc(int numExHosts, int* slotUsages) {
	int numExecProcessors = 0, i;
	for (i = 0; i < numExHosts; i++) {
		numExecProcessors += slotUsages[i];
	}
	return numExecProcessors;
}
#endif
/*
 *-----------------------------------------------------------------------
 *
 * getHostsHashmapArray -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * type[IN]: event type.
 * eventTime[IN]: event time.
 * jobId[IN]: event's jobId.
 * numHosts[IN]: the number of hosts.
 * askedHosts[IN]: Hosts name string array pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * create hashmap object array according askedHosts field in struct eventRec.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getHostsHashmapArray(char *p, time_t eventTime, int jobId,
		int numHosts, char **askedHosts, int idx) {
	Json4c *askedHostsArray = NULL;
	Json4c *objHost = NULL;
	int i;
	char *time;
	/* create new array instance. */
	askedHostsArray = jCreateArray();

	/* put dynamic field to hashmap. */
	for (i = 0; i < numHosts; i++) {
		// objHost = (*env)->NewObject(env, clsHASHMAP, HashMap_init);
		objHost = jCreateObject();
		if (NULL == objHost) {
			//   throw_exception_by_key(env, logger, "perf.lsf.events.nullObject",
			//   NULL);
			return NULL;
		}

		addNumberToObject(objHost, FIELD_JOB_ID, jobId);

		addStringToObject(objHost, FIELD_EVENT_TYPE, p);

		time = timeConvert(eventTime);
		addStringToObject(objHost, FIELD_EVENT_TIME, time);

		FREEUP(time);
		addNumberToObject(objHost, FIELD_EVENT_TIME_UTC, (int)eventTime);

		addStringToObject(objHost, FIELD_HOST_NAME, askedHosts[i]);

		addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, idx);

		addInstanceToArray(askedHostsArray, objHost);
	}

	return askedHostsArray;
}

/*
 *-----------------------------------------------------------------------
 *
 * getExecHostsHashmapArray -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * type[IN]: event type.
 * eventTime[IN]: event time.
 * jobId[IN]: event's jobId.
 * numExHosts[IN]: the number of hosts.
 * execHosts[IN]: Hosts name string array pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * create hashmap object array according execHosts field in struct eventRec.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getExecHostsHashmapArray(char *eventType, time_t eventTime,
		time_t submitTime, LONGLONG jobId, int numExHosts, char **execHosts,
		int idx) {
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	int i, j, found;
	int numRealHosts = 0;
	char **RealHosts = NULL;
	int *HostSlots = NULL;
	char *p = NULL;
	int nHost = 0;
	if (execHosts == NULL) {
		return NULL;
	}

	/* init RealHost array. */
	RealHosts = calloc(numExHosts, sizeof(char *));
	HostSlots = calloc(numExHosts, sizeof(int));

	/* copy first host to RealHost array. */
	RealHosts[0] = strdup(execHosts[0]);
	numRealHosts = 1;

	found = 0;
	/* search how many different host. */
	for (i = 0; i < numExHosts; i++) {
		for (j = 0; j < numRealHosts; j++) {
			if (0 == strcmp(execHosts[i], RealHosts[j])) {
				found = 1;
				HostSlots[j]++;
				break;
			}
		}
		if (0 == found) {
			RealHosts[numRealHosts] = strdup(execHosts[i]);
			HostSlots[numRealHosts]++;
			numRealHosts++;
		}
		found = 0;
	}

	/* create new array instance. */
	execHostsArray = jCreateArray();

	/* put dynamic field to hashmap. */
	for (i = 0; i < numRealHosts; i++) {
		objHost = jCreateObject();
		if (NULL == objHost) {
			return NULL;
		}


		/* handle SHORT_EVENTFILE. */
		p = strchr(RealHosts[i], '*');
		nHost = atoi(RealHosts[i]);
		if (NULL != p && nHost > 0) {
			addStringToObject(objHost, FIELD_HOST_NAME, p + 1);

			addNumberToObject(objHost, FIELD_EXECHOST_SLOT_NUM, nHost);
		} else {
			addStringToObject(objHost, FIELD_HOST_NAME, RealHosts[i]);

			addNumberToObject(objHost, FIELD_EXECHOST_SLOT_NUM, HostSlots[i]);
		}
		addInstanceToArray(execHostsArray, objHost);
	}

	/* free memory. */
	for (j = 0; j < numRealHosts; j++) {
		free(RealHosts[j]);
	}
	free(RealHosts);
	free(HostSlots);

	return execHostsArray;
}

/*
 *-----------------------------------------------------------------------
 *
 * getJobExecHostsHashmapArray -- jguo, 2011-03-03
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * type[IN]: event type.
 * eventTime[IN]: event time.
 * numExHosts[IN]: the number of hosts.
 * execHosts[IN]: Hosts name string array pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * create hashmap object array according execHosts field in struct eventRec.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
#if defined(LSF8) || defined(LSF9) || defined(LSF10)

static Json4c *getJobExecHostsHashmapArray(char *eventType, time_t eventTime,
		struct jobFinish2Log *jobFinish2Log) {
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	int i, j;
	int numExecProcessors = 0;
	//char *time;
	/* Job array index and job id */
	//lsfArrayIdx = LSB_ARRAY_IDX(jobFinish2Log->jobId);
	//lsfJobId = LSB_ARRAY_JOBID(jobFinish2Log->jobId);
	/* create new array instance. */
	// execHostsArray = _VECTOR_(env, clsHASHMAP, jobFinish2Log->numExHosts);
	execHostsArray = jCreateArray();

	/*Number of exec processors*/
	numExecProcessors = getNumExecProc(jobFinish2Log->numExHosts, jobFinish2Log->slotUsages);

	/* put dynamic field to hashmap. */
	for (i = 0; i < jobFinish2Log->numExHosts; i++) {
		//long gmt;
		// objHost = (*env)->NewObject(env, clsHASHMAP, HashMap_init);
		objHost = jCreateObject();
		if (NULL == objHost) {
			//   throw_exception_by_key(env, logger, "perf.lsf.events.nullObject",
			//   NULL);
			return NULL;
		}

		addStringToObject(objHost, FIELD_HOST_NAME, jobFinish2Log->execHosts[i]);

		addNumberToObject(objHost, FIELD_EXECHOST_SLOT_NUM, jobFinish2Log->slotUsages[i]);

		if (jobFinish2Log->numhRusages > 0) {
			for (j = 0; j < jobFinish2Log->numhRusages; j++) {
				if (0 == strcmp(jobFinish2Log->execHosts[i],
								jobFinish2Log->hostRusage[j].name)) {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
							jobFinish2Log->hostRusage[j].mem);

					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
							jobFinish2Log->hostRusage[j].swap);

					addNumberToObject(objHost, FIELD_EXECHOST_UTIME,
							jobFinish2Log->hostRusage[j].utime);
					addNumberToObject(objHost, FIELD_EXECHOST_STIME,
							jobFinish2Log->hostRusage[j].stime);

					addNumberToObject(objHost, FIELD_CPU_TIME,
							jobFinish2Log->hostRusage[j].utime + jobFinish2Log->hostRusage[j].stime);
					break;
				}
			}
		} else {
			if (numExecProcessors == 0) {
				addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE, 0);
				addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE, 0);
				addNumberToObject(objHost, FIELD_EXECHOST_UTIME, 0);
				addNumberToObject(objHost, FIELD_EXECHOST_STIME, 0);

				addNumberToObject(objHost, FIELD_CPU_TIME, 0);
			} else {
				double precent =
				(double)jobFinish2Log->slotUsages[i] / numExecProcessors;
				if (jobFinish2Log->lsfRusage.ru_maxrss < 0) {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
							jobFinish2Log->lsfRusage.ru_maxrss);

				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
							jobFinish2Log->lsfRusage.ru_maxrss * precent);
				}
				if (jobFinish2Log->lsfRusage.ru_nswap < 0) {;
					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
							jobFinish2Log->lsfRusage.ru_nswap);
				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
							jobFinish2Log->lsfRusage.ru_nswap * precent);
				}
				double utime = 0;
				if (jobFinish2Log->lsfRusage.ru_utime < 0) {
					addNumberToObject(objHost, FIELD_EXECHOST_UTIME,
							jobFinish2Log->lsfRusage.ru_utime);
					utime = jobFinish2Log->lsfRusage.ru_utime;
				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_UTIME,
							jobFinish2Log->lsfRusage.ru_utime * precent);
					utime = jobFinish2Log->lsfRusage.ru_utime * precent;
				}
				double stime = 0;
				if (jobFinish2Log->lsfRusage.ru_stime < 0) {
					addNumberToObject(objHost, FIELD_EXECHOST_STIME,
							jobFinish2Log->lsfRusage.ru_stime);
					stime = jobFinish2Log->lsfRusage.ru_stime;
				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_STIME,
							jobFinish2Log->lsfRusage.ru_stime * precent);
					stime = jobFinish2Log->lsfRusage.ru_stime * precent;
				}

				addNumberToObject(objHost, FIELD_CPU_TIME, utime + stime);
			}
		}
		addInstanceToArray(execHostsArray, objHost);
	}
	return execHostsArray;
}

#endif

/*
 *-----------------------------------------------------------------------
 *
 * getExecHostsString as short format -- jgao, 2007-07-23
 * For example,
 * convert {"host1", "host2", "host1", "host3"} to "2*host1 host2 host3"
 *
 * ARGUMENTS:
 *
 * numExHosts[IN]: the number of hosts.
 * execHosts[IN]: Hosts name string array pointer.
 * objMap[IN]: map obj for adding total slots.
 *
 * DESCRIPTION:
 *
 * create string of execHost with short format according **execHosts.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * string of execHost on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static char *getExecHostsStr(char **execHosts, int numExHosts, Json4c *objMap) {
	char *execHostStr = NULL;
	/*temp execHosts char array*/
	char **hostsTemp = NULL;
	/*each execHosts num*/
	int *numHostsTemp = NULL;
	/*total temp Hosts num*/
	int totalHostsTemp = 0;
	int i;
	int j;
	char numStr[21];
	int totalLen = 0;
	int totalSlots = 0;

	if (NULL == execHosts || numExHosts == 0) {
		return NULL;
	}

	/* get TempHosts array memory. */
	hostsTemp = (char **) malloc(numExHosts * sizeof(char *));
	if (hostsTemp == NULL) {
		return NULL;
	}

	numHostsTemp = (int *) malloc(numExHosts * sizeof(int));
	if (numHostsTemp == NULL) {
		return NULL;
	}

	/* Initialize array of int */
	for (i = 0; i < numExHosts; i++) {
		numHostsTemp[i] = 1;
	}

	/* copy first host to temp array. */
	hostsTemp[0] = strdup(execHosts[0]);
	totalHostsTemp = 1;

	/*log_msg(env, logger, _LOG_INFO_INT_, "# of execHosts: %d", numExHosts,
	 * NULL);*/
	/* cumpute num of each execHost. */
	for (i = 1; i < numExHosts; i++) {
		for (j = 0; j < totalHostsTemp; j++) {
			if (0 == strcmp(hostsTemp[j], execHosts[i])) {
				numHostsTemp[j]++;
				break;
			}
		}
		if (j == totalHostsTemp) {
			hostsTemp[totalHostsTemp] = strdup(execHosts[i]);
			totalHostsTemp++;
		}
	}

	/* Populate total length of the execHostStr */
	for (i = 0; i < totalHostsTemp; i++) {
		if (numHostsTemp[i] > 1) {
			/* Length of the string converted from number plus length of the character
			 * '*' */
			totalLen += (int) log10(numHostsTemp[i]) + 2;
		}
		totalLen += strlen(hostsTemp[i]) + 1;
	}

	execHostStr = (char *) malloc((totalLen + 1) * sizeof(char));
	if (execHostStr == NULL) {
		return NULL;
	}
	memset(execHostStr, 0x00, totalLen);

	/* Concat hosts in hostsTemp to execHostStr */
	for (i = 0; i < totalHostsTemp; i++) {
		if (numHostsTemp[i] > 1) {
			sprintf(numStr, "%d%c", numHostsTemp[i], '*');
			strcat(execHostStr, numStr);
			totalSlots += numHostsTemp[i];
		}
		strcat(execHostStr, hostsTemp[i]);
		strcat(execHostStr, " ");
	}
	execHostStr[totalLen - 1] = 0x00;

	/* free memory. */
	for (i = 0; i < totalHostsTemp; i++) {
		free(hostsTemp[i]);
	}

	if (hostsTemp != NULL) {
		free(hostsTemp);
	}

	free(numHostsTemp);

	// hashmap_put_int(env, objMap, HashMap_put, "num_exec_procs", totalSlots);
	addNumberToObject(objMap, FIELD_NUM_EXEC_PROCESSORS, totalSlots);

	return execHostStr;
}

/*
 *-----------------------------------------------------------------------
 *
 * putLsfRusage -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * HashMap_put[IN]: Hashmap put method.
 * lsfRusage[IN]: struct lsfRusage pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put structure lsfrusage data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putLsfRusage(Json4c *objHashMap, struct lsfRusage *lsfRusage) {

	addNumberToObject(objHashMap, FIELD_RU_UTIME, lsfRusage->ru_utime);

	addNumberToObject(objHashMap, FIELD_RU_STIME, lsfRusage->ru_stime);

	addNumberToObject(objHashMap, FIELD_RU_MAX_RSS, lsfRusage->ru_maxrss);

	addNumberToObject(objHashMap, FIELD_RU_IXRSS, lsfRusage->ru_ixrss);

	addNumberToObject(objHashMap, FIELD_RU_ISMRSS, lsfRusage->ru_ismrss);

	addNumberToObject(objHashMap, FIELD_RU_IDRSS, lsfRusage->ru_idrss);

	addNumberToObject(objHashMap, FIELD_RU_ISRSS, lsfRusage->ru_isrss);

	addNumberToObject(objHashMap, FIELD_RU_MINOR_FAULTS, lsfRusage->ru_minflt);

	addNumberToObject(objHashMap, FIELD_RU_MAJOR_FAULTS, lsfRusage->ru_majflt);

	addNumberToObject(objHashMap, FIELD_RU_NUM_SWAPOUT, lsfRusage->ru_nswap);

	addNumberToObject(objHashMap, FIELD_RU_BLOCK_IN, lsfRusage->ru_inblock);

	addNumberToObject(objHashMap, FIELD_RU_BLOCK_OUT, lsfRusage->ru_oublock);

	addNumberToObject(objHashMap, FIELD_RU_IO_CHARS, lsfRusage->ru_ioch);

	addNumberToObject(objHashMap, FIELD_RU_MSGSND, lsfRusage->ru_msgsnd);

	addNumberToObject(objHashMap, FIELD_RU_MSGRCV, lsfRusage->ru_msgrcv);

	addNumberToObject(objHashMap, FIELD_RU_NUM_SIGNALS, lsfRusage->ru_nsignals);

	addNumberToObject(objHashMap, FIELD_RU_NUM_V_CSW, lsfRusage->ru_nvcsw);

	addNumberToObject(objHashMap, FIELD_RU_NUM_INV_CSW, lsfRusage->ru_nivcsw);

	addNumberToObject(objHashMap, FIELD_RU_EXUTIME, lsfRusage->ru_exutime);
}

/*
 *-----------------------------------------------------------------------
 *
 * putRLimitArray -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * HashMap_put[IN]: Hashmap put method.
 * rLimit[IN]: rLimit array pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put rLimit array data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putRLimitArray(Json4c *objHashMap, int *rLimits) {

	// Add a nested level "resource_limits"
	Json4c * originalRoot = objHashMap;
	objHashMap = jCreateObject();
	addInstanceToObject(originalRoot, "resource_limit", objHashMap);

	addNumberToObject(objHashMap, FIELD_RLIMIT_CPU, rLimits[LSF_RLIMIT_CPU]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_FSIZE, rLimits[LSF_RLIMIT_FSIZE]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_DATA, rLimits[LSF_RLIMIT_DATA]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_STACK, rLimits[LSF_RLIMIT_STACK]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_CORE, rLimits[LSF_RLIMIT_CORE]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_RSS, rLimits[LSF_RLIMIT_RSS]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_FILES, rLimits[LSF_RLIMIT_NOFILE]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_OPEN_MAX, rLimits[LSF_RLIMIT_OPEN_MAX]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_VMEM, rLimits[LSF_RLIMIT_VMEM]);

	addNumberToObject(objHashMap,FIELD_RLIMIT_RUNTIME, rLimits[LSF_RLIMIT_RUN]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_PROCESS, rLimits[LSF_RLIMIT_PROCESS]);

	addNumberToObject(objHashMap, FIELD_RLIMIT_THREAD, rLimits[LSF_RLIMIT_THREAD]);
}

/*
 *-----------------------------------------------------------------------
 *
 * putXFStructure -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * HashMap_put[IN]: Hashmap put method.
 * nxf[IN]: the number of xf structure array.
 * xf[IN]: xf structure pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put xf structure data to hashmap object, now we just connect xf.subFn string
 * to a whole string.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putXFStructure(Json4c *objHashMap, int nxf, struct xFile *xf) {
	int i;
	char *buf = NULL;
	char option[11] = { '\0' };
	/* alloc the mem for the buf*/
	if (nxf > 0) {
		buf = malloc(nxf * (4094 * 2 + 128));
		if (buf == NULL)
			return;
		memset(buf, 0, 10);
	}

	/* copy subFn to buf. */
	for (i = 0; i < nxf; i++) {
		strcat(buf, xf[i].subFn);
		strcat(buf, " ");
		strcat(buf, xf[i].execFn);
		strcat(buf, " ");
		sprintf(option, "%d ", xf[i].options);
		strcat(buf, option);
		memset(option, 0, 11);
	}
	/* put string to hashmap. */
	addNumberToObject(objHashMap, FIELD_NUM_XFER_FILES, nxf);
	if (buf != NULL) {
		addStringToObject(objHashMap, FIELD_XFER_FILES, buf);
		free(buf);
	}
}

/*
 *-----------------------------------------------------------------------
 * put the asked host list into a field or a string.
 * internal used
 *------------------------------------------------------------------------
 */
static void putAskedhostlist(Json4c *objHashMap, int askednum,
		char **askedhostlist) {
	int i = 0, j;
	char *temp = NULL;
	char *buff = NULL;
	buff = calloc(askednum, 512 * sizeof(char));
	temp = buff;
	for (i = 0; i < askednum; i++) {
		j = sprintf(buff, "%s ", askedhostlist[i]);
		buff += j;
	}
	buff = '\0';

	addStringToObject(objHashMap, FIELD_ASKED_HOSTS, temp);

	/* free memory*/
	free(temp);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobNew -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_NEW type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobNew(Json4c *objHashMap, struct eventRec *logrec) {
	Json4c *askedHostsArray;

	int jobCount = 1; /* Number of jobs in the job array */
	char *jobIndexList = NULL; /* Job Indexes in the job array */
	char *jstatsstr = NULL;
	char *time;

	TRACE("Handling JOB_NEW\n");
	/* put static field to hashmap. */
	putJobHEAD(objHashMap, logrec);

	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobNewLog.jobId);
	TRACE("JobID %d\n", logrec->eventLog.jobNewLog.jobId);

	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.jobNewLog.userId);

	addStringToObject(objHashMap, FIELD_USER_NAME,
			logrec->eventLog.jobNewLog.userName);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS,
			logrec->eventLog.jobNewLog.options);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS_2,
			logrec->eventLog.jobNewLog.options2);

	if (logrec->eventLog.jobNewLog.options2 & SUB2_HOLD) {
		jstatsstr = transformJstatus(JOB_STAT_PSUSP);
		addStringToObject(objHashMap, FIELD_JOB_STATUS, jstatsstr);
	}
	addNumberToObject(objHashMap, FIELD_NUM_PROCESSORS,
			logrec->eventLog.jobNewLog.numProcessors);


	addNumberToObject(objHashMap, FIELD_SUBMIT_TIME, logrec->eventLog.jobNewLog.submitTime);
	time = timeConvert(logrec->eventLog.jobNewLog.submitTime);
	addStringToObject(objHashMap, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_BEGIN_TIME, logrec->eventLog.jobNewLog.beginTime);
	time = timeConvert(logrec->eventLog.jobNewLog.beginTime);
	addStringToObject(objHashMap, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_TERM_TIME, logrec->eventLog.jobNewLog.termTime);
	time = timeConvert(logrec->eventLog.jobNewLog.termTime);
	addStringToObject(objHashMap, FIELD_TERM_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_SIGNAL_VALUE,
				logrec->eventLog.jobNewLog.sigValue);

	addNumberToObject(objHashMap, FIELD_CHECKPOINT_INTERVAL,
				logrec->eventLog.jobNewLog.chkpntPeriod);

	addNumberToObject(objHashMap, FIELD_RESTART_PID,
			logrec->eventLog.jobNewLog.restartPid);

	/* put rLimits array. */
	// modified by zk on 2017-04-18 to add nested object for rlimit values
	Json4c * rlimitObj = jCreateObject();
	putRLimitArray(rlimitObj, logrec->eventLog.jobNewLog.rLimits);
	addInstanceToObject(objHashMap, FIELD_RESOURCE_LIMIT, rlimitObj);	

	addStringToObject(objHashMap, FIELD_HOST_SPEC,
				logrec->eventLog.jobNewLog.hostSpec);

	addNumberToObject(objHashMap, FIELD_HOST_CPU_FACTOR,
				logrec->eventLog.jobNewLog.hostFactor);

	addNumberToObject(objHashMap, FIELD_UMASK, logrec->eventLog.jobNewLog.umask);
	addStringToObject(objHashMap, FIELD_QUEUE_NAME, logrec->eventLog.jobNewLog.queue);
	addStringToObject(objHashMap, FIELD_RES_REQ, logrec->eventLog.jobNewLog.resReq);

	addStringToObject(objHashMap, FIELD_SUBMISSION_HOST_NAME,
				logrec->eventLog.jobNewLog.fromHost);

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	addStringToObject(objHashMap, FIELD_SUBMISSION_CLUSTER_NAME,
				logrec->eventLog.jobNewLog.srcCluster);
#endif

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	/* cwd */
	if (logrec->eventLog.jobNewLog.subcwd != NULL) {
		char cwd[MAXFULLFILENAMELEN];
		memset(cwd, 0, MAXFULLFILENAMELEN);

		if (logrec->eventLog.jobNewLog.subcwd[0] == '/' ||
				logrec->eventLog.jobNewLog.subcwd[0] == '\\' ||
				(logrec->eventLog.jobNewLog.subcwd[0] != '\0' &&
						logrec->eventLog.jobNewLog.subcwd[1] == ':'))
		sprintf(cwd, "%s", logrec->eventLog.jobNewLog.subcwd);
		else if (logrec->eventLog.jobNewLog.subcwd[0] == '\0')
		sprintf(cwd, "%s", logrec->eventLog.jobNewLog.subHomeDir);
		else
		sprintf(cwd, "%s/%s", logrec->eventLog.jobNewLog.subHomeDir,
				logrec->eventLog.jobNewLog.subcwd);

		addStringToObject(objHashMap, FIELD_CWD, cwd);
	} else if (logrec->eventLog.jobNewLog.subHomeDir != NULL) {
		addStringToObject(objHashMap, FIELD_CWD,
				logrec->eventLog.jobNewLog.subHomeDir);
	}
#else
	/* cwd */
	if (logrec->eventLog.jobNewLog.cwd != NULL) {
		char cwd[MAXFULLFILENAMELEN];
		memset(cwd, 0, MAXFULLFILENAMELEN);

		if (logrec->eventLog.jobNewLog.cwd[0] == '/'
				|| logrec->eventLog.jobNewLog.cwd[0] == '\\'
				|| (logrec->eventLog.jobNewLog.cwd[0] != '\0'
						&& logrec->eventLog.jobNewLog.cwd[1] == ':'))
			sprintf(cwd, "%s", logrec->eventLog.jobNewLog.cwd);
		else if (logrec->eventLog.jobNewLog.cwd[0] == '\0')
			sprintf(cwd, "%s", logrec->eventLog.jobNewLog.subHomeDir);
		else
			sprintf(cwd, "%s/%s", logrec->eventLog.jobNewLog.subHomeDir,
					logrec->eventLog.jobNewLog.cwd);

		addStringToObject(objHashMap, FIELD_CWD, cwd);
	} else if (logrec->eventLog.jobNewLog.subHomeDir != NULL) {
		addStringToObject(objHashMap, FIELD_CWD,
				logrec->eventLog.jobNewLog.subHomeDir);
	}
#endif

	addStringToObject(objHashMap, FIELD_CHECKPOINT_DIR,
				logrec->eventLog.jobNewLog.chkpntDir);

	addStringToObject(objHashMap, FIELD_IN_FILE, logrec->eventLog.jobNewLog.inFile);

	addStringToObject(objHashMap, FIELD_OUT_FILE,
				logrec->eventLog.jobNewLog.outFile);

	addStringToObject(objHashMap, FIELD_ERR_FILE,
				logrec->eventLog.jobNewLog.errFile);

	addStringToObject(objHashMap, FIELD_IN_FILE_SPOOL,
				logrec->eventLog.jobNewLog.inFileSpool);

	addStringToObject(objHashMap, FIELD_COMMAND_SPOOL,
			logrec->eventLog.jobNewLog.commandSpool);

	addStringToObject(objHashMap, FIELD_JOB_SPOOL_DIR,
				logrec->eventLog.jobNewLog.jobSpoolDir);

	addStringToObject(objHashMap, FIELD_SUBMITTER_HOME,
				logrec->eventLog.jobNewLog.subHomeDir);

	addStringToObject(objHashMap, FIELD_JOB_FILE,
				logrec->eventLog.jobNewLog.jobFile);

	addStringToObject(objHashMap, FIELD_DEPEND_COND,
				logrec->eventLog.jobNewLog.dependCond);

	addStringToObject(objHashMap, FIELD_TIME_EVENT,
				logrec->eventLog.jobNewLog.timeEvent);

	if (logrec->eventLog.jobNewLog.jobName == NULL
			|| strlen(logrec->eventLog.jobNewLog.jobName) == 0) {
		addStringToObject(objHashMap, FIELD_JOB_NAME,
						logrec->eventLog.jobNewLog.command);

		addStringToObject(objHashMap, FIELD_JOB_NAME_FULL,
						logrec->eventLog.jobNewLog.command);

	} else {
		addStringToObject(objHashMap,FIELD_JOB_NAME,
					logrec->eventLog.jobNewLog.jobName);

		addStringToObject(objHashMap, FIELD_JOB_NAME_FULL,
						logrec->eventLog.jobNewLog.jobName);
	}
	addStringToObject(objHashMap, FIELD_JOB_COMMAND,
			logrec->eventLog.jobNewLog.command);
	addStringToObject(objHashMap, FIELD_PREEXEC_CMD,
				logrec->eventLog.jobNewLog.preExecCmd);

	addStringToObject(objHashMap, FIELD_MAIL_USER,
				logrec->eventLog.jobNewLog.mailUser);

	addStringToObject(objHashMap, FIELD_PROJECT_NAME,
				logrec->eventLog.jobNewLog.projectName);

	addNumberToObject(objHashMap, FIELD_NIOS_PORT,
				logrec->eventLog.jobNewLog.niosPort);

	addNumberToObject(objHashMap, FIELD_REQ_NUM_PROCS_MAX,
				logrec->eventLog.jobNewLog.maxNumProcessors);

	addStringToObject(objHashMap, FIELD_SCHED_HOST_TYPE,
				logrec->eventLog.jobNewLog.schedHostType);

	addStringToObject(objHashMap, FIELD_LOGIN_SHELL,
				logrec->eventLog.jobNewLog.loginShell);

	addStringToObject(objHashMap, FIELD_USER_GROUP_NAME,
			logrec->eventLog.jobNewLog.userGroup);

	addStringToObject(objHashMap, FIELD_EXCEPT_LIST,
				logrec->eventLog.jobNewLog.exceptList);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobNewLog.idx);

	addNumberToObject(objHashMap, FIELD_PRIORITY,
				logrec->eventLog.jobNewLog.userPriority);

	addStringToObject(objHashMap, FIELD_ADV_RSV_ID, logrec->eventLog.jobNewLog.rsvId);

	addStringToObject(objHashMap, FIELD_JOB_GROUP,
				logrec->eventLog.jobNewLog.jobGroup);

	addStringToObject(objHashMap, FIELD_EXTSCHED,
			logrec->eventLog.jobNewLog.extsched);
	addNumberToObject(objHashMap, FIELD_WARNING_TIME_PERIOD,
				logrec->eventLog.jobNewLog.warningTimePeriod);

	addStringToObject(objHashMap, FIELD_WARNING_ACTION,
				logrec->eventLog.jobNewLog.warningAction);

	addStringToObject(objHashMap, FIELD_SLA, logrec->eventLog.jobNewLog.sla);

	addNumberToObject(objHashMap, FIELD_SLA_RUN_LIMIT,
				logrec->eventLog.jobNewLog.SLArunLimit);

	addStringToObject(objHashMap, FIELD_LIC_PROJECT_NAME,
				logrec->eventLog.jobNewLog.licenseProject);

#if defined(LSF8) || defined(LSF9) || defined(LSF10)
	addStringToObject(objHashMap, FIELD_JOB_DESCRIPTION,
				logrec->eventLog.jobNewLog.jobDescription);

#endif

#if !defined(LSF6)
	/* add postExecCmd, runtimeEStimation, 2007-7-24, jgao */
	addStringToObject(objHashMap, FIELD_POSTEXEC_CMD,
				logrec->eventLog.jobNewLog.postExecCmd);

	addNumberToObject(objHashMap, FIELD_RUNTIME_EST,
				logrec->eventLog.jobNewLog.runtimeEstimation);
#endif

	/* handle xf structure. */
	putXFStructure(objHashMap, logrec->eventLog.jobNewLog.nxf,
			logrec->eventLog.jobNewLog.xf);

	/*put the asked hostlists */
	addNumberToObject(objHashMap, FIELD_NUM_ASKED_HOSTS,
			logrec->eventLog.jobNewLog.numAskedHosts);

	if (logrec->eventLog.jobNewLog.numAskedHosts > 0)
		putAskedhostlist(objHashMap, logrec->eventLog.jobNewLog.numAskedHosts,
				logrec->eventLog.jobNewLog.askedHosts);

#if !defined(LSF6)
	/* put the application_tag into hashmap*/
	addNumberToObject(objHashMap, FIELD_JOB_OPTS_3,
				logrec->eventLog.jobNewLog.options3);

	addStringToObject(objHashMap, FIELD_APP_PROFILE, logrec->eventLog.jobNewLog.app);

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	addStringToObject(objHashMap, FIELD_FLOW_ID,
			logrec->eventLog.jobNewLog.flow_id);
#endif
#endif
	/* Count job number according to job name */
	jobCount = countJobByName(logrec->eventLog.jobNewLog.jobName,
			&jobIndexList);
	/* Put job count to hashmap */
	if (jobCount > 0) {
		addNumberToObject(objHashMap, FIELD_NUM_ARR_ELEMENTS, jobCount);

	}
	/* Put job list to hashmap */
	if (jobIndexList != NULL) {
		addStringToObject(objHashMap, FIELD_JOB_IDX_LIST, jobIndexList);

		/* Release memory of the jobIndexList pointer */
		free(jobIndexList);
	}
	TRACE("Finish handling JOB_NEW\n");
} // end putJobNew

/*
 *-----------------------------------------------------------------------
 *
 * putJobStart -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_START type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobStart(Json4c *objHashMap, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	char *execHostStr = NULL;
	// jobjectArray execHostsArray = NULL;
	Json4c *execHostsArray = NULL;
	char *time;
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobStartLog.jobId);

	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE, logrec->eventLog.jobStartLog.jStatus);

	/* add Jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobStartLog.jStatus);
	addStringToObject(objHashMap, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHashMap, FIELD_JOB_PID,
				logrec->eventLog.jobStartLog.jobPid);

	addNumberToObject(objHashMap, FIELD_JOB_PGID,
				logrec->eventLog.jobStartLog.jobPGid);

	addNumberToObject(objHashMap, FIELD_HOST_CPU_FACTOR,
				logrec->eventLog.jobStartLog.hostFactor);

	addStringToObject(objHashMap, FIELD_QUEUE_PRE_CMD,
				logrec->eventLog.jobStartLog.queuePreCmd);

	addStringToObject(objHashMap, FIELD_QUEUE_POST_CMD,
				logrec->eventLog.jobStartLog.queuePostCmd);

	addNumberToObject(objHashMap, FIELD_JOB_FLAGS,
				logrec->eventLog.jobStartLog.jFlags);


	addStringToObject(objHashMap, FIELD_USER_GROUP_NAME,
				logrec->eventLog.jobStartLog.userGroup);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobStartLog.idx);

	addStringToObject(objHashMap, FIELD_ADDITIONAL_INFO,
				logrec->eventLog.jobStartLog.additionalInfo);

	addNumberToObject(objHashMap, FIELD_START_TIME, logrec->eventTime);
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHashMap, FIELD_START_TIME_STR, time);
	FREEUP(time);
#if !defined(LSF6)
	addNumberToObject(objHashMap, FIELD_PREEMPT_BACKFILL, logrec->eventLog.jobStartLog.duration4PreemptBackfill);

#endif

	// add effective_res_req field for bhist display by ZK on 2016-10-13
	addStringToObject(objHashMap, FIELD_EFFECTIVE_RES_REQ,
				logrec->eventLog.jobStartLog.effectiveResReq);

#if defined(LSF10)
	// add alloc_slots_num field for bhist display by ZK on 2016-10-13
	addNumberToObject(objHashMap, FIELD_ALLOC_SLOTS_NUM,
				logrec->eventLog.jobStartLog.numAllocSlots);
#endif

	/* number of exec host*/
	addNumberToObject(objHashMap, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobStartLog.numExHosts);

	/*add name string of exec host --jgao 2007-7-20*/
	execHostStr = getExecHostsStr(logrec->eventLog.jobStartLog.execHosts,
			logrec->eventLog.jobStartLog.numExHosts, objHashMap);
	/*addStringToObject(objHashMap, FIELD_EXEC_HOSTS, execHostStr);*/
	if (execHostStr != NULL)
		free(execHostStr);
	free(jstatsstr);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobStatus -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_STATUS type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobStatus(Json4c *objHashMap, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	char *time;
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobStatusLog.jobId);

	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE, logrec->eventLog.jobStatusLog.jStatus);
	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobStatusLog.jStatus);
	addStringToObject(objHashMap, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHashMap, FIELD_PEND_REASON,
			logrec->eventLog.jobStatusLog.reason);
	addNumberToObject(objHashMap, FIELD_PEND_SUBREASON,
			logrec->eventLog.jobStatusLog.subreasons);
	addNumberToObject(objHashMap, FIELD_CPU_TIME,
				logrec->eventLog.jobStatusLog.cpuTime);

	addNumberToObject(objHashMap, FIELD_END_TIME, logrec->eventLog.jobStatusLog.endTime);
	time = timeConvert(logrec->eventLog.jobStatusLog.endTime);
	addStringToObject(objHashMap, FIELD_END_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_RUSAGE_FLAG, logrec->eventLog.jobStatusLog.ru);

	putLsfRusage(objHashMap, &logrec->eventLog.jobStatusLog.lsfRusage);

	addNumberToObject(objHashMap, FIELD_JOB_FLAGS,
				logrec->eventLog.jobStatusLog.jFlags);

	addNumberToObject(objHashMap, FIELD_EXIT_STATUS,
				logrec->eventLog.jobStatusLog.exitStatus);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobStatusLog.idx);

	addNumberToObject(objHashMap, FIELD_EXIT_INFO,
				logrec->eventLog.jobStatusLog.exitInfo);

	// add max_mem and avg_mem fields by ZK on 2016/10/13
	addNumberToObject(objHashMap, FIELD_MAX_MEM,
				logrec->eventLog.jobStatusLog.maxMem);
	addNumberToObject(objHashMap, FIELD_AVG_MEM,
					logrec->eventLog.jobStatusLog.avgMem);

	free(jstatsstr);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobSwitch -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_SWITCH type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobSwitch(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobSwitchLog.jobId);
	
	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.jobSwitchLog.userId);

	addStringToObject(objHashMap, FIELD_QUEUE_NAME, logrec->eventLog.jobSwitchLog.queue);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobSwitchLog.idx);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobSwitchLog.userName);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobMove -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_MOVE type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobMove(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	
	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.jobMoveLog.userId);

	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobMoveLog.jobId);

	addNumberToObject(objHashMap, FIELD_POSITION,
			logrec->eventLog.jobMoveLog.position);
	addNumberToObject(objHashMap, FIELD_POSITION_BASE, logrec->eventLog.jobMoveLog.base);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobMoveLog.idx);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobMoveLog.userName);
}

/*
 *-----------------------------------------------------------------------
 *
 * putUnfulfillLog -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_MBD_UNFULFILL type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putUnfulfillLog(Json4c *objHashMap, struct eventRec *logrec) {
	char flag[11] = { '\0' };
	putJobHEAD(objHashMap, logrec);

	sprintf(flag, "%d", logrec->eventLog.unfulfillLog.notModified);

	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.unfulfillLog.jobId);

	addNumberToObject(objHashMap, FIELD_NOT_SWITCHED,
				logrec->eventLog.unfulfillLog.notSwitched);

	addNumberToObject(objHashMap, FIELD_SIG, logrec->eventLog.unfulfillLog.sig);
	addNumberToObject(objHashMap, FIELD_SIG_1, logrec->eventLog.unfulfillLog.sig1);

	addNumberToObject(objHashMap, FIELD_SIG_1_FLAGS,
				logrec->eventLog.unfulfillLog.sig1Flags);

	addNumberToObject(objHashMap, FIELD_CHECKPOINT_INTERVAL,
				logrec->eventLog.unfulfillLog.chkPeriod);

	addStringToObject(objHashMap, FIELD_NOT_MODIFIED, flag);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.unfulfillLog.idx);

	addNumberToObject(objHashMap, FIELD_MISC_OPTS_4_PEND_SIG,
				logrec->eventLog.unfulfillLog.miscOpts4PendSig);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobFinish -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_FINISH type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobFinish(Json4c *objHashMap, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	char *execHostStr = NULL;
	char *exitReason = NULL;
	long gmt;
	// jobjectArray askedHostsArray = NULL;
	// jobjectArray execHostsArray = NULL;
	Json4c *askedHostsArray = NULL;
	Json4c *execHostsArray = NULL;
	char *time;

	int doneFlag = 0;
	int exitFlag = 0;
	long pendTime = 0;
	long totalTime = 0;
	double hogFactor = 0;
	double expFactor = 0;

	putJobHEAD(objHashMap, logrec);

	// add several statistics for bacct command by ZK on 2016/10/13
	// add done_flag and exit_flag fields to count for total done and exit job number
	switch (logrec->eventLog.jobFinishLog.jStatus) {
	case JOB_STAT_DONE:
		doneFlag = 1;
		break;
	case JOB_STAT_EXIT:
		exitFlag = 1;
		break;
	}
	addNumberToObject(objHashMap, FIELD_DONE_FLAG, doneFlag);
	addNumberToObject(objHashMap, FIELD_EXIT_FLAG, exitFlag);

	// add pend_time field
	if (logrec->eventLog.jobFinishLog.startTime > 0) {
		pendTime = logrec->eventLog.jobFinishLog.startTime
				- logrec->eventLog.jobFinishLog.submitTime ;
	} else {
		pendTime = logrec->eventLog.jobFinishLog.endTime
				- logrec->eventLog.jobFinishLog.submitTime ;
	}
	addNumberToObject(objHashMap, FIELD_PEND_TIME, pendTime);

	// add turnaround_time field
	totalTime = logrec->eventLog.jobFinishLog.endTime
			- logrec->eventLog.jobFinishLog.submitTime ;
	addNumberToObject(objHashMap, FIELD_TURNAROUND_TIME, totalTime);

	// add hog_factor field
	if (totalTime > 0) {
		hogFactor = logrec->eventLog.jobFinishLog.cpuTime / totalTime;
	}
	addNumberToObject(objHashMap, FIELD_HOG_FACTOR, hogFactor);

	// add expand_factor
#if defined(LSF9) || defined(LSF10)
	if (logrec->eventLog.jobFinishLog.runTime > 0 ){
		expFactor = totalTime * 1.0 / logrec->eventLog.jobFinishLog.runTime;
	}
	addNumberToObject(objHashMap, FIELD_EXPAND_FACTOR, expFactor);
	
	// add effective_res_req on 2016/10/17 by zk
	addStringToObject(objHashMap, FIELD_EFFECTIVE_RES_REQ, logrec->eventLog.jobFinishLog.effectiveResReq);
#endif
	// finish adding statistics

	// add prov_time on 2016/10/17 by zk
	addNumberToObject(objHashMap, FIELD_PROV_TIME, logrec->eventLog.jobFinishLog.totalProvisionTime);

	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobFinishLog.jobId);

	addNumberToObject(objHashMap, FIELD_UID,
				logrec->eventLog.jobFinishLog.userId);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobFinishLog.userName);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS,
			logrec->eventLog.jobFinishLog.options);

	addNumberToObject(objHashMap, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobFinishLog.numProcessors);

	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE, logrec->eventLog.jobFinishLog.jStatus);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobFinishLog.jStatus);
	addStringToObject(objHashMap, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHashMap, FIELD_SUBMIT_TIME, logrec->eventLog.jobFinishLog.submitTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.submitTime);
	addStringToObject(objHashMap, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_BEGIN_TIME, logrec->eventLog.jobFinishLog.beginTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.beginTime);
	addStringToObject(objHashMap, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_TERM_TIME, logrec->eventLog.jobFinishLog.termTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.termTime);
	addStringToObject(objHashMap, FIELD_TERM_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_START_TIME, logrec->eventLog.jobFinishLog.startTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.startTime);
	addStringToObject(objHashMap, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_END_TIME, logrec->eventLog.jobFinishLog.endTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.endTime);
	addStringToObject(objHashMap, FIELD_END_TIME_STR, time);
	FREEUP(time);

	addStringToObject(objHashMap, FIELD_QUEUE_NAME, logrec->eventLog.jobFinishLog.queue);
	addStringToObject(objHashMap, FIELD_RES_REQ, logrec->eventLog.jobFinishLog.resReq);

	addStringToObject(objHashMap, FIELD_SUBMISSION_HOST_NAME, logrec->eventLog.jobFinishLog.fromHost);
	
	gmt = (int)logrec->eventLog.jobFinishLog.submitTime;
    addNumberToObject(objHashMap, FIELD_SUBMIT_TIME_UTC,gmt*1000);
    gmt = (int) logrec->eventLog.jobFinishLog.startTime;
    addNumberToObject(objHashMap, FIELD_START_TIME_UTC,gmt*1000);
    gmt = (int) logrec->eventLog.jobFinishLog.endTime;
    addNumberToObject(objHashMap, FIELD_END_TIME_UTC,gmt*1000);


#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	/* cwd */
	if (logrec->eventLog.jobFinishLog.subcwd != NULL &&
			logrec->eventLog.jobFinishLog.subcwd[0] != '\0') {
		char cwd[MAXFULLFILENAMELEN];
		memset(cwd, 0, MAXFULLFILENAMELEN);

		if (logrec->eventLog.jobFinishLog.subcwd[0] == '/' ||
				logrec->eventLog.jobFinishLog.subcwd[0] == '\\' ||
				(logrec->eventLog.jobFinishLog.subcwd[0] != '\0' &&
						logrec->eventLog.jobFinishLog.subcwd[1] == ':'))
		sprintf(cwd, "%s", logrec->eventLog.jobFinishLog.subcwd);
		else if (logrec->eventLog.jobFinishLog.subcwd[0] == '\0')
		sprintf(cwd, "$HOME");
		else
		sprintf(cwd, "$HOME/%s", logrec->eventLog.jobFinishLog.subcwd);

		addStringToObject(objHashMap, FIELD_CWD, cwd);
	}
	/* PAC needs this runtime value in JOB_FINISH event */
	addNumberToObject(objHashMap, FIELD_RUN_TIME, logrec->eventLog.jobFinishLog.runTime);


#else
	/* cwd */
	if (logrec->eventLog.jobFinishLog.cwd != NULL
			&& logrec->eventLog.jobFinishLog.cwd[0] != '\0') {
		char cwd[MAXFULLFILENAMELEN];
		memset(cwd, 0, MAXFULLFILENAMELEN);

		if (logrec->eventLog.jobFinishLog.cwd[0] == '/'
				|| logrec->eventLog.jobFinishLog.cwd[0] == '\\'
				|| (logrec->eventLog.jobFinishLog.cwd[0] != '\0'
						&& logrec->eventLog.jobFinishLog.cwd[1] == ':'))
			sprintf(cwd, "%s", logrec->eventLog.jobFinishLog.cwd);
		else if (logrec->eventLog.jobFinishLog.cwd[0] == '\0')
			sprintf(cwd, "$HOME");
		else
			sprintf(cwd, "$HOME/%s", logrec->eventLog.jobFinishLog.cwd);

		addStringToObject(objHashMap, FIELD_CWD, cwd);
	}
#endif
	addStringToObject(objHashMap, FIELD_IN_FILE,
				logrec->eventLog.jobFinishLog.inFile);

	addStringToObject(objHashMap, FIELD_OUT_FILE,
				logrec->eventLog.jobFinishLog.outFile);

	addStringToObject(objHashMap, FIELD_ERR_FILE,
				logrec->eventLog.jobFinishLog.errFile);

	addStringToObject(objHashMap, FIELD_IN_FILE_SPOOL,
			logrec->eventLog.jobFinishLog.inFileSpool);

	addStringToObject(objHashMap, FIELD_COMMAND_SPOOL,
				logrec->eventLog.jobFinishLog.commandSpool);

	addStringToObject(objHashMap, FIELD_JOB_FILE,
			logrec->eventLog.jobFinishLog.jobFile);

	addNumberToObject(objHashMap, FIELD_HOST_CPU_FACTOR,
				logrec->eventLog.jobFinishLog.hostFactor);

	addNumberToObject(objHashMap, FIELD_CPU_TIME,
				logrec->eventLog.jobFinishLog.cpuTime);

	if (logrec->eventLog.jobFinishLog.jobName == NULL
			|| strlen(logrec->eventLog.jobFinishLog.jobName) == 0) {
		addStringToObject(objHashMap, FIELD_JOB_NAME,
						logrec->eventLog.jobFinishLog.command);

		addStringToObject(objHashMap, FIELD_JOB_NAME_FULL,
						logrec->eventLog.jobFinishLog.command);
	} else {
		addStringToObject(objHashMap, FIELD_JOB_NAME,
					logrec->eventLog.jobFinishLog.jobName);
		addStringToObject(objHashMap, FIELD_JOB_NAME_FULL,
						logrec->eventLog.jobFinishLog.jobName);

	}

	addStringToObject(objHashMap, FIELD_JOB_COMMAND,
			logrec->eventLog.jobFinishLog.command);
	addStringToObject(objHashMap, FIELD_DEPEND_COND,
				logrec->eventLog.jobFinishLog.dependCond);

	addStringToObject(objHashMap, FIELD_TIME_EVENT,
				logrec->eventLog.jobFinishLog.timeEvent);

	addStringToObject(objHashMap, FIELD_PREEXEC_CMD,
				logrec->eventLog.jobFinishLog.preExecCmd);

	addStringToObject(objHashMap, FIELD_MAIL_USER,
				logrec->eventLog.jobFinishLog.mailUser);

	addStringToObject(objHashMap, FIELD_PROJECT_NAME,
			logrec->eventLog.jobFinishLog.projectName);

	addNumberToObject(objHashMap, FIELD_EXIT_STATUS,
			logrec->eventLog.jobFinishLog.exitStatus);

	addNumberToObject(objHashMap, FIELD_REQ_NUM_PROCS_MAX,
				logrec->eventLog.jobFinishLog.maxNumProcessors);

	addStringToObject(objHashMap, FIELD_LOGIN_SHELL,
				logrec->eventLog.jobFinishLog.loginShell);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobFinishLog.idx);

	addNumberToObject(objHashMap, FIELD_MAX_MEM,
				logrec->eventLog.jobFinishLog.maxRMem);
	addNumberToObject(objHashMap, FIELD_AVG_MEM,
				logrec->eventLog.jobFinishLog.avgMem);

	addNumberToObject(objHashMap, FIELD_MAX_SWAP,
			logrec->eventLog.jobFinishLog.maxRSwap);

	addStringToObject(objHashMap, FIELD_ADV_RSV_ID, logrec->eventLog.jobFinishLog.rsvId);

	addStringToObject(objHashMap, FIELD_SLA, logrec->eventLog.jobFinishLog.sla);
	addNumberToObject(objHashMap, FIELD_EXCEPT_MASK,
				logrec->eventLog.jobFinishLog.exceptMask);

	addStringToObject(objHashMap, FIELD_ADDITIONAL_INFO,
				logrec->eventLog.jobFinishLog.additionalInfo);

	addNumberToObject(objHashMap, FIELD_EXIT_INFO,
			logrec->eventLog.jobFinishLog.exitInfo);

	addNumberToObject(objHashMap, FIELD_WARNING_TIME_PERIOD,
			logrec->eventLog.jobFinishLog.warningTimePeriod);

	addStringToObject(objHashMap, FIELD_WARNING_ACTION,
				logrec->eventLog.jobFinishLog.warningAction);

	addStringToObject(objHashMap, FIELD_CHARGED_SAAP,
				logrec->eventLog.jobFinishLog.chargedSAAP);

	addStringToObject(objHashMap, FIELD_LIC_PROJECT_NAME,
				logrec->eventLog.jobFinishLog.licenseProject);


#if defined(LSF8) || defined(LSF9) || defined(LSF10)
	addStringToObject(objHashMap, FIELD_JOB_DESCRIPTION,
				logrec->eventLog.jobFinishLog.jobDescription);
#endif

#if !defined(LSF7) && !defined(LSF8) && !defined(LSF6)
	addNumberToObject(objHashMap, FIELD_EXIT_INFO,
				logrec->eventLog.jobFinishLog.exitInfo);

	addNumberToObject(objHashMap, FIELD_EXCEPT_MASK,
				logrec->eventLog.jobFinishLog.exceptMask);

	addNumberToObject(objHashMap, FIELD_SUBMIT_TIME, logrec->eventLog.jobFinishLog.submitTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.submitTime);
	addStringToObject(objHashMap, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_BEGIN_TIME, logrec->eventLog.jobFinishLog.beginTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.beginTime);
	addStringToObject(objHashMap, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_TERM_TIME, logrec->eventLog.jobFinishLog.termTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.termTime);
	addStringToObject(objHashMap, FIELD_TERM_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_START_TIME, logrec->eventLog.jobFinishLog.startTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.startTime);
	addStringToObject(objHashMap, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_END_TIME, logrec->eventLog.jobFinishLog.endTime);
	time = timeConvert(logrec->eventLog.jobFinishLog.endTime);
	addStringToObject(objHashMap, FIELD_END_TIME_STR, time);
	FREEUP(time);

	addStringToObject(objHashMap, FIELD_DEPEND_COND,
				logrec->eventLog.jobFinishLog.dependCond);

	addNumberToObject(objHashMap, FIELD_RUNLIMIT,
				logrec->eventLog.jobFinishLog.runLimit);

#endif
#if !defined(LSF6)
	/* add postExecCmd, runtimeEStimation, jobGroup, 2007-7-24, jgao */
	addStringToObject(objHashMap, FIELD_POSTEXEC_CMD,
				logrec->eventLog.jobFinishLog.postExecCmd);

	addNumberToObject(objHashMap, FIELD_RUNTIME_EST,
				logrec->eventLog.jobFinishLog.runtimeEstimation);

	addStringToObject(objHashMap, FIELD_JOB_GROUP,
				logrec->eventLog.jobFinishLog.jgroup);
#endif
	/* put lsfRusage struct. */
	putLsfRusage(objHashMap, &logrec->eventLog.jobFinishLog.lsfRusage);

	/*put the asked host list */
	addNumberToObject(objHashMap, FIELD_NUM_ASKED_HOSTS,
			logrec->eventLog.jobFinishLog.numAskedHosts);

	if (logrec->eventLog.jobFinishLog.numAskedHosts > 0)
		putAskedhostlist(objHashMap,
				logrec->eventLog.jobFinishLog.numAskedHosts,
				logrec->eventLog.jobFinishLog.askedHosts);

	/* number of exec host*/
	addNumberToObject(objHashMap, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobFinishLog.numExHosts);

#if !defined(LSF6)
	/* put the application_tag into hashmap*/
	addStringToObject(objHashMap, FIELD_APP_PROFILE, logrec->eventLog.jobFinishLog.app);

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	addStringToObject(objHashMap, FIELD_FLOW_ID,
			logrec->eventLog.jobFinishLog.flow_id);
#endif

#endif

	/*add name string of exec host --jgao 2007-7-20*/
	execHostStr = getExecHostsStr(logrec->eventLog.jobFinishLog.execHosts,
			logrec->eventLog.jobFinishLog.numExHosts, objHashMap);

	// add exec_host_list_str on 2016/10/17 by zk
	addStringToObject(objHashMap, FIELD_EXEC_HOSTS_LIST, execHostStr);

#if !defined(LSF7)
	addStringToObject(objHashMap, FIELD_JOB_DESCRIPTION,
				logrec->eventLog.jobFinishLog.jobDescription);
#endif

	// added exit_reason by zk on 2018-10-15
	exitReason = getExitReason(logrec->eventLog.jobFinishLog.jStatus, logrec->eventLog.jobFinishLog.exitInfo, logrec->eventLog.jobFinishLog.exceptMask, logrec->eventLog.jobFinishLog.exitStatus);
	addStringToObject(objHashMap, FIELD_JOB_EXIT_REASON, exitReason);
	free(exitReason);
	if (execHostStr != NULL)
		free(execHostStr);
	free(jstatsstr);
}

void ltrim(char *s) {
	char *p;
	p = s;
	while (' ' == *p || '\t' == *p) {
		*p++;
	}
	strcpy(s, p);
}

void rtrim(char *s) {
	int i;
	i = strlen(s) - 1;
	while ((' ' == s[i] || '\t' == s[i]) && i >= 0) {
		i--;
	}
	s[i + 1] = '\0';
}

/*
 *-----------------------------------------------------------------------
 *
 * trim -- jianjin, 2013-09-22
 *
 * ARGUMENTS:
 *
 * s[IN/OUT]: Source string to be trimmed.
 *
 * DESCRIPTION:
 *
 * Trim blank in head and tail of source string.
 *
 * RETURN:
 *
 * char[] trimmed source string.
 *
 *-----------------------------------------------------------------------
 */
char *trim(char *s) {
	if (NULL == s || 0 == strlen(s)) {
		return s;
	}
	ltrim(s);
	rtrim(s);
	return s;
}

#if defined(LSF8) || defined(LSF9) || defined(LSF10)

static Json4c *getJobFinish2HashmapArray(struct eventRec *logrec) {
	char *jstatsstr = NULL;
	long lsfArrayIdx;
	long lsfJobId;
	long jobSignalCode;
	Json4c *execHostsArray = NULL;
	int jobType = JOB_TYPE_BATCH;
	int numExecProcessors = 0;
	Json4c *objHost = NULL;
	char *time;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(logrec->eventLog.jobFinish2Log.jobId);
	lsfJobId = LSB_ARRAY_JOBID(logrec->eventLog.jobFinish2Log.jobId);

	/* Job signal code */
	if (logrec->eventLog.jobFinish2Log.exitStatus) {
		if (WIFEXITED(logrec->eventLog.jobFinish2Log.exitStatus)) {
			jobSignalCode = WEXITSTATUS(logrec->eventLog.jobFinish2Log.exitStatus);
		} else {
			jobSignalCode = WTERMSIG(logrec->eventLog.jobFinish2Log.exitStatus);
		}
	} else {
		jobSignalCode = logrec->eventLog.jobFinish2Log.exitStatus;
	}

	/*Job Type*/
	if (logrec->eventLog.jobFinish2Log.options & SUB_INTERACTIVE) {
		jobType |= JOB_TYPE_INTERACTIVE;
	}

	if (logrec->eventLog.jobFinish2Log.numProcessors > 1) {
		jobType |= JOB_TYPE_PARALLEL;
	}

	/*Number of exec processors*/
	numExecProcessors = getNumExecProc(logrec->eventLog.jobFinish2Log.numExHosts, logrec->eventLog.jobFinish2Log.slotUsages);

	/* create new array instance. */
	// execHostsArray = _VECTOR_(env, clsHASHMAP, 1);
	execHostsArray = jCreateArray();
	objHost = jCreateObject();
	if (NULL == objHost) {
		// throw_exception_by_key(env, logger, "perf.lsf.events.nullObject", NULL);
		return NULL;
	}

	addStringToObject(objHost, FIELD_VERSION, logrec->version);
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHost, FIELD_EVENT_TIME_STR, time);
	FREEUP(time);
	addNumberToObject(objHost, FIELD_EPOCH, (int)logrec->eventTime);

	addNumberToObject(objHost, FIELD_JOB_ID, lsfJobId);

	addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, lsfArrayIdx);

	switch (jobType) {
		case JOB_TYPE_BATCH:
		addStringToObject(objHost, FIELD_JOB_TYPE, "batch");
		break;
		case JOB_TYPE_INTERACTIVE:
		addStringToObject(objHost, FIELD_JOB_TYPE, "interactive");
		break;
		case JOB_TYPE_PARALLEL:
		addStringToObject(objHost, FIELD_JOB_TYPE, "parallel");
		break;
		case (JOB_TYPE_INTERACTIVE + JOB_TYPE_PARALLEL):
		addStringToObject(objHost, FIELD_JOB_TYPE, "interactive & parallel");
		break;
	}

	addStringToObject(objHost, FIELD_USER_NAME,
				logrec->eventLog.jobFinish2Log.userName);

	addNumberToObject(objHost, FIELD_JOB_OPTS,
				logrec->eventLog.jobFinish2Log.options);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobFinish2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_EXEC_PROCESSORS, numExecProcessors);

	addNumberToObject(objHost, FIELD_JOB_STATUS_CODE, logrec->eventLog.jobFinish2Log.jStatus);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobFinish2Log.jStatus);

	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHost, FIELD_SUBMIT_TIME, logrec->eventLog.jobFinish2Log.submitTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.submitTime);
	addStringToObject(objHost, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

#if defined(LSF10)
	addNumberToObject(objHost, FIELD_BEGIN_TIME, logrec->eventLog.jobFinish2Log.beginTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.beginTime);
	addStringToObject(objHost, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);
#endif

	addNumberToObject(objHost, FIELD_TERM_TIME, logrec->eventLog.jobFinish2Log.termTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.termTime);
	addStringToObject(objHost, FIELD_TERM_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_START_TIME, logrec->eventLog.jobFinish2Log.startTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.startTime);
	addStringToObject(objHost, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_END_TIME, logrec->eventLog.jobFinish2Log.endTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.endTime);
	addStringToObject(objHost, FIELD_END_TIME_STR, time);
	FREEUP(time);



	addStringToObject(objHost, FIELD_QUEUE_NAME, logrec->eventLog.jobFinish2Log.queue);
	addStringToObject(objHost, FIELD_RES_REQ, logrec->eventLog.jobFinish2Log.resReq);

	addStringToObject(objHost, FIELD_SUBMISSION_HOST_NAME, logrec->eventLog.jobFinish2Log.fromHost);

	addStringToObject(objHost, FIELD_CWD, logrec->eventLog.jobFinish2Log.cwd);
	addStringToObject(objHost, FIELD_IN_FILE, logrec->eventLog.jobFinish2Log.inFile);

	addStringToObject(objHost, FIELD_OUT_FILE, logrec->eventLog.jobFinish2Log.outFile);

	addStringToObject(objHost, FIELD_JOB_FILE, logrec->eventLog.jobFinish2Log.jobFile);

	addNumberToObject(objHost, FIELD_CPU_TIME, logrec->eventLog.jobFinish2Log.cpuTime);

	addStringToObject(objHost, FIELD_JOB_NAME, logrec->eventLog.jobFinish2Log.jobName);

	addStringToObject(objHost, FIELD_JOB_COMMAND, logrec->eventLog.jobFinish2Log.command);
	addStringToObject(objHost, FIELD_PREEXEC_CMD, logrec->eventLog.jobFinish2Log.preExecCmd);

	addStringToObject(objHost, FIELD_PROJECT_NAME, logrec->eventLog.jobFinish2Log.projectName);

	addNumberToObject(objHost, FIELD_EXIT_STATUS, logrec->eventLog.jobFinish2Log.exitStatus);

	addNumberToObject(objHost, FIELD_SIGNAL_VALUE, jobSignalCode);

	addNumberToObject(objHost, FIELD_REQ_NUM_PROCS_MAX, logrec->eventLog.jobFinish2Log.maxNumProcessors);

	addStringToObject(objHost, FIELD_SLA, logrec->eventLog.jobFinish2Log.sla);
	addNumberToObject(objHost, FIELD_JOB_EXIT_CODE, jobSignalCode);

	addNumberToObject(objHost, FIELD_EXIT_INFO, logrec->eventLog.jobFinish2Log.exitInfo);

	addStringToObject(objHost, FIELD_CHARGED_SAAP, logrec->eventLog.jobFinish2Log.chargedSAAP);

	addStringToObject(objHost, FIELD_LIC_PROJECT_NAME, logrec->eventLog.jobFinish2Log.licenseProject);

	int gmt = (int)logrec->eventLog.jobFinish2Log.submitTime;
	addNumberToObject(objHost, FIELD_SUBMIT_TIME_UTC, gmt*1000);
    gmt = (int) logrec->eventLog.jobFinish2Log.startTime;
	addNumberToObject(objHost, FIELD_START_TIME_UTC, gmt*1000);
    gmt = (int) logrec->eventLog.jobFinish2Log.endTime;
	addNumberToObject(objHost, FIELD_END_TIME_UTC, gmt*1000);

#if defined(LSF10)
	addNumberToObject(objHost, FIELD_RUNLIMIT,
				logrec->eventLog.jobFinish2Log.runLimit);

	addStringToObject(objHost, FIELD_JOB_DESCRIPTION,
				logrec->eventLog.jobFinish2Log.jobDescription);

	addNumberToObject(objHost, FIELD_JOB_OPTS_3,
				logrec->eventLog.jobFinish2Log.options3);

	addStringToObject(objHost, FIELD_REQUEUE_EXIT_VALS,
				logrec->eventLog.jobFinish2Log.requeueEValues);

	addStringToObject(objHost, FIELD_DEPEND_COND,
				logrec->eventLog.jobFinish2Log.dependCond);

	addNumberToObject(objHost, FIELD_JOB_OPTS_2,
			logrec->eventLog.jobFinish2Log.options2);

	addNumberToObject(objHost, FIELD_HOST_CPU_FACTOR,
				logrec->eventLog.jobFinish2Log.hostFactor);

	addNumberToObject(objHost, FIELD_EXCEPT_MASK,
				logrec->eventLog.jobFinish2Log.exceptMask);

	addStringToObject(objHost, FIELD_ADV_RSV_ID,
				logrec->eventLog.jobFinish2Log.rsvId);
#endif

#if defined(LSF9) || defined(LSF10)
	addStringToObject(objHost, FIELD_EFFECTIVE_RES_REQ,
				trim(logrec->eventLog.jobFinish2Log.effectiveResReq));
#endif

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)

	addStringToObject(objHost, FIELD_FLOW_ID,
			logrec->eventLog.jobFinish2Log.flow_id);
	addNumberToObject(objHost, FIELD_FORWARD_TIME,
				logrec->eventLog.jobFinish2Log.forwardTime);

	addStringToObject(objHost, FIELD_SRC_CLUSTER_NAME,
				logrec->eventLog.jobFinish2Log.srcCluster);

	addNumberToObject(objHost, FIELD_SRC_JOB_ID,
				logrec->eventLog.jobFinish2Log.srcJobId);

	addStringToObject(objHost, FIELD_DST_CLUSTER_NAME,
			logrec->eventLog.jobFinish2Log.dstCluster);

	addNumberToObject(objHost, FIELD_DST_JOB_ID,
				logrec->eventLog.jobFinish2Log.dstJobId);
#endif

#if !defined(LSF6)
	addStringToObject(objHost, FIELD_POSTEXEC_CMD,
			logrec->eventLog.jobFinish2Log.postExecCmd);

	addStringToObject(objHost, FIELD_JOB_GROUP,
				logrec->eventLog.jobFinish2Log.jgroup);
#endif
	/* put lsfRusage struct. */
	putLsfRusage(objHost, &logrec->eventLog.jobFinish2Log.lsfRusage);
	/* number of exec host*/
	addNumberToObject(objHost, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobFinish2Log.numExHosts);

#if !defined(LSF6)
	/* put the application_tag into hashmap*/

	addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobFinish2Log.app);
#endif

	/*new attributes for jobfinish2 events*/
	addStringToObject(objHost, FIELD_EXEC_RUSAGE,
				logrec->eventLog.jobFinish2Log.execRusage);

	addStringToObject(objHost, FIELD_CLUSTER_NAME,
				logrec->eventLog.jobFinish2Log.clusterName);

	addStringToObject(objHost, FIELD_USER_GROUP_NAME,
				logrec->eventLog.jobFinish2Log.userGroup);

	addNumberToObject(objHost, FIELD_RUN_TIME,
				logrec->eventLog.jobFinish2Log.runtime);

#if defined(LSF9) || defined(LSF10)
	addNumberToObject(objHost, FIELD_TOTAL_PROVISION_TIME,
				logrec->eventLog.jobFinish2Log.totalProvisionTime);

	if (logrec->eventLog.jobFinish2Log.dcJobFlags & AC_JOB_INFO_VMJOB) {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "Y");

	} else {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "N");
	}
#endif

	/*
	 * **********************************************
	 * Added by ZK on 2016-08-15 to find EXIT_REASON
	 * **********************************************
	 */
#if defined(LSF10)
	char* exitReason = formatExitReason(&(logrec->eventLog.jobFinish2Log));
	addStringToObject(objHost, FIELD_EXIT_REASON, exitReason);
	FREEUP(exitReason);
#endif

	// _VECTOR_PUT_(env, execHostsArray, 0, objHost);
	addInstanceToArray(execHostsArray, objHost);
	free(jstatsstr);
	// (*env)->DeleteLocalRef(env, objHost);
	return execHostsArray;
}

static void putJobFinish2(Json4c *objHost, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	long lsfArrayIdx;
	long lsfJobId;
	long jobSignalCode;
	int jobType = JOB_TYPE_BATCH;
	int numExecProcessors = 0;
	char *time;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(logrec->eventLog.jobFinish2Log.jobId);
	lsfJobId = LSB_ARRAY_JOBID(logrec->eventLog.jobFinish2Log.jobId);

	/* Job signal code */
	if (logrec->eventLog.jobFinish2Log.exitStatus) {
		if (WIFEXITED(logrec->eventLog.jobFinish2Log.exitStatus)) {
			jobSignalCode = WEXITSTATUS(logrec->eventLog.jobFinish2Log.exitStatus);
		} else {
			jobSignalCode = WTERMSIG(logrec->eventLog.jobFinish2Log.exitStatus);
		}
	} else {
		jobSignalCode = logrec->eventLog.jobFinish2Log.exitStatus;
	}

	/*Job Type*/
	if (logrec->eventLog.jobFinish2Log.options & SUB_INTERACTIVE) {
		jobType |= JOB_TYPE_INTERACTIVE;
	}

	if (logrec->eventLog.jobFinish2Log.numProcessors > 1) {
		jobType |= JOB_TYPE_PARALLEL;
	}

	/*Number of exec processors*/
	numExecProcessors = getNumExecProc(logrec->eventLog.jobFinish2Log.numExHosts, logrec->eventLog.jobFinish2Log.slotUsages);

	addStringToObject(objHost, FIELD_VERSION, logrec->version);
	time = timeConvert(logrec->eventTime);
	//addStringToObject(objHost, "eventTime", time);
	addStringToObject(objHost, FIELD_EVENT_TIME_STR, time);

	FREEUP(time);
	addNumberToObject(objHost, FIELD_EPOCH, (int)logrec->eventTime);

	addNumberToObject(objHost, FIELD_JOB_ID, lsfJobId);

	addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, lsfArrayIdx);

	switch (jobType) {
		case JOB_TYPE_BATCH:
		addStringToObject(objHost, FIELD_JOB_TYPE, "batch");
		break;
		case JOB_TYPE_INTERACTIVE:
		addStringToObject(objHost, FIELD_JOB_TYPE, "interactive");
		break;
		case JOB_TYPE_PARALLEL:
		addStringToObject(objHost, FIELD_JOB_TYPE, "parallel");
		break;
		case (JOB_TYPE_INTERACTIVE + JOB_TYPE_PARALLEL):
		addStringToObject(objHost, FIELD_JOB_TYPE, "interactive & parallel");
		break;
	}

	addStringToObject(objHost, FIELD_USER_NAME,
			logrec->eventLog.jobFinish2Log.userName);

	addNumberToObject(objHost, FIELD_JOB_OPTS,
			logrec->eventLog.jobFinish2Log.options);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
			logrec->eventLog.jobFinish2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_EXEC_PROCESSORS, numExecProcessors);

	addNumberToObject(objHost, FIELD_JOB_STATUS_CODE, logrec->eventLog.jobFinish2Log.jStatus);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobFinish2Log.jStatus);

	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHost, FIELD_SUBMIT_TIME, logrec->eventLog.jobFinish2Log.submitTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.submitTime);
	addStringToObject(objHost, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

#if defined(LSF10)
	addNumberToObject(objHost, FIELD_BEGIN_TIME, logrec->eventLog.jobFinish2Log.beginTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.beginTime);
	addStringToObject(objHost, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);
#endif

	addNumberToObject(objHost, FIELD_TERM_TIME, logrec->eventLog.jobFinish2Log.termTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.termTime);
	addStringToObject(objHost, FIELD_TERM_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_START_TIME, logrec->eventLog.jobFinish2Log.startTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.startTime);
	addStringToObject(objHost, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_END_TIME, logrec->eventLog.jobFinish2Log.endTime);
	time = timeConvert(logrec->eventLog.jobFinish2Log.endTime);
	addStringToObject(objHost, FIELD_END_TIME_STR, time);
	FREEUP(time);

	addStringToObject(objHost, FIELD_QUEUE_NAME, logrec->eventLog.jobFinish2Log.queue);
	addStringToObject(objHost, FIELD_RES_REQ, logrec->eventLog.jobFinish2Log.resReq);

	addStringToObject(objHost, FIELD_SUBMISSION_HOST_NAME, logrec->eventLog.jobFinish2Log.fromHost);

	addStringToObject(objHost, FIELD_CWD, logrec->eventLog.jobFinish2Log.cwd);
	addStringToObject(objHost, FIELD_IN_FILE, logrec->eventLog.jobFinish2Log.inFile);

	addStringToObject(objHost, FIELD_OUT_FILE, logrec->eventLog.jobFinish2Log.outFile);

	addStringToObject(objHost, FIELD_JOB_FILE, logrec->eventLog.jobFinish2Log.jobFile);

	addNumberToObject(objHost, FIELD_CPU_TIME, logrec->eventLog.jobFinish2Log.cpuTime);

	addStringToObject(objHost, FIELD_JOB_NAME, logrec->eventLog.jobFinish2Log.jobName);

	addStringToObject(objHost, FIELD_JOB_COMMAND, logrec->eventLog.jobFinish2Log.command);
	addStringToObject(objHost, FIELD_PREEXEC_CMD, logrec->eventLog.jobFinish2Log.preExecCmd);

	addStringToObject(objHost, FIELD_PROJECT_NAME, logrec->eventLog.jobFinish2Log.projectName);

	addNumberToObject(objHost, FIELD_EXIT_STATUS, logrec->eventLog.jobFinish2Log.exitStatus);

	addNumberToObject(objHost, FIELD_SIGNAL_VALUE, jobSignalCode);

	addNumberToObject(objHost, FIELD_REQ_NUM_PROCS_MAX, logrec->eventLog.jobFinish2Log.maxNumProcessors);

	addStringToObject(objHost, FIELD_SLA, logrec->eventLog.jobFinish2Log.sla);
	addNumberToObject(objHost, FIELD_JOB_EXIT_CODE, jobSignalCode);

	addNumberToObject(objHost, FIELD_EXIT_INFO, logrec->eventLog.jobFinish2Log.exitInfo);

	addStringToObject(objHost, FIELD_CHARGED_SAAP,
			logrec->eventLog.jobFinish2Log.chargedSAAP);

	addStringToObject(objHost, FIELD_LIC_PROJECT_NAME,
			logrec->eventLog.jobFinish2Log.licenseProject);

	int gmt = (int)logrec->eventLog.jobFinish2Log.submitTime;
	addNumberToObject(objHost, FIELD_SUBMIT_TIME_UTC, gmt*1000);
    gmt = (int) logrec->eventLog.jobFinish2Log.startTime;
	addNumberToObject(objHost, FIELD_START_TIME_UTC, gmt*1000);
    gmt = (int) logrec->eventLog.jobFinish2Log.endTime;
	addNumberToObject(objHost, FIELD_END_TIME_UTC, gmt*1000);

#if defined(LSF10)
	addNumberToObject(objHost, FIELD_RUNLIMIT,
			logrec->eventLog.jobFinish2Log.runLimit);

	addStringToObject(objHost, FIELD_JOB_DESCRIPTION,
			logrec->eventLog.jobFinish2Log.jobDescription);

	addNumberToObject(objHost, FIELD_JOB_OPTS_3,
			logrec->eventLog.jobFinish2Log.options3);

	addStringToObject(objHost, FIELD_REQUEUE_EXIT_VALS,
			logrec->eventLog.jobFinish2Log.requeueEValues);

	addStringToObject(objHost, FIELD_DEPEND_COND,
			logrec->eventLog.jobFinish2Log.dependCond);

	addNumberToObject(objHost, FIELD_JOB_OPTS_2,
			logrec->eventLog.jobFinish2Log.options2);

	addNumberToObject(objHost, FIELD_HOST_CPU_FACTOR,
			logrec->eventLog.jobFinish2Log.hostFactor);

	addNumberToObject(objHost, FIELD_EXCEPT_MASK,
			logrec->eventLog.jobFinish2Log.exceptMask);

	addStringToObject(objHost, FIELD_ADV_RSV_ID,
			logrec->eventLog.jobFinish2Log.rsvId);

#endif

#if defined(LSF9) || defined(LSF10)
	addStringToObject(objHost, FIELD_EFFECTIVE_RES_REQ,
			trim(logrec->eventLog.jobFinish2Log.effectiveResReq));

#endif

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)

	addStringToObject(objHost, FIELD_FLOW_ID,
			logrec->eventLog.jobFinish2Log.flow_id);
	addNumberToObject(objHost, FIELD_FORWARD_TIME, logrec->eventLog.jobFinish2Log.forwardTime);

	addStringToObject(objHost, FIELD_SRC_CLUSTER_NAME,
				logrec->eventLog.jobFinish2Log.srcCluster);

	addNumberToObject(objHost, FIELD_SRC_JOB_ID,
				logrec->eventLog.jobFinish2Log.srcJobId);

	addStringToObject(objHost, FIELD_DST_CLUSTER_NAME,
				logrec->eventLog.jobFinish2Log.dstCluster);

	addNumberToObject(objHost, FIELD_DST_JOB_ID,
				logrec->eventLog.jobFinish2Log.dstJobId);
#endif

#if !defined(LSF6)
	/* add postExecCmd, runtimeEStimation, jobGroup, 2007-7-24, jgao */
	addStringToObject(objHost, FIELD_POSTEXEC_CMD,
			logrec->eventLog.jobFinish2Log.postExecCmd);

	addStringToObject(objHost, FIELD_JOB_GROUP,
			logrec->eventLog.jobFinish2Log.jgroup);

#endif
	/* put lsfRusage struct. */
	putLsfRusage(objHost, &logrec->eventLog.jobFinish2Log.lsfRusage);
	/* number of exec host*/
	addNumberToObject(objHost, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobFinish2Log.numExHosts);

#if !defined(LSF6)
	/* put the application_tag into hashmap*/

	addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobFinish2Log.app);

#endif

	/*new attributes for jobfinish2 events*/
	addStringToObject(objHost, FIELD_EXEC_RUSAGE,
				logrec->eventLog.jobFinish2Log.execRusage);

	addStringToObject(objHost, FIELD_CLUSTER_NAME,
			logrec->eventLog.jobFinish2Log.clusterName);

	addStringToObject(objHost, FIELD_USER_GROUP_NAME,
			logrec->eventLog.jobFinish2Log.userGroup);

	addNumberToObject(objHost, FIELD_RUN_TIME,
			logrec->eventLog.jobFinish2Log.runtime);

#if defined(LSF9) || defined(LSF10)
	addNumberToObject(objHost, FIELD_TOTAL_PROVISION_TIME,
			logrec->eventLog.jobFinish2Log.totalProvisionTime);

	if (logrec->eventLog.jobFinish2Log.dcJobFlags & AC_JOB_INFO_VMJOB) {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "Y");
	} else {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "N");
	}
#endif

	/*
	 * **********************************************
	 * Added by ZK on 2016-08-11 to find EXIT_REASON
	 * **********************************************
	 */
#if defined(LSF10)
	char* exitReason = formatExitReason(&(logrec->eventLog.jobFinish2Log));
	addStringToObject(objHost, FIELD_EXIT_REASON, exitReason);
	FREEUP(exitReason);
#endif

	free(jstatsstr);
}

/* Execution rlimits for job */
const char *lsfRlimitsStr[] = {
//	"LSF_RLIMIT_CPU",
	"cpu",
//	"LSF_RLIMIT_FSIZE",
	"fsize",
//	"LSF_RLIMIT_DATA",
	"data",
//	"LSF_RLIMIT_STACK",
	"stack",
//	"LSF_RLIMIT_CORE",
	"core",
//	"LSF_RLIMIT_RSS",
	"mem",
//	"LSF_RLIMIT_NOFILE",
	"nofile",
//	"LSF_RLIMIT_OPEN_MAX",
	"open_max",
//	"LSF_RLIMIT_SWAP",
	"swap",
//	"LSF_RLIMIT_RUN",
	"runtime",
//	"LSF_RLIMIT_PROCESS",
	"process",
//	"LSF_RLIMIT_THREAD",
	"thread",
	NULL};

/* Submission rlimits for job*/
const char *jobRlimitsStr[] = {
//	"JOB_RLIMIT_CPU",
	"cpu",
//	"JOB_RLIMIT_FSIZE",
	"fsize",
//	"JOB_RLIMIT_DATA",
	"data",
//	"JOB_RLIMIT_STACK",
	"stack",
//	"JOB_RLIMIT_CORE",
	"core",
//	"JOB_RLIMIT_RSS",
	"mem",
//	"JOB_RLIMIT_NOFILE",
	"nofile",
//	"JOB_RLIMIT_OPEN_MAX",
	"open_max",
//	"JOB_RLIMIT_SWAP",
	"swap",
//	"JOB_RLIMIT_RUN",
	"runtime",
//	"JOB_RLIMIT_PROCESS",
	"process",
//	"JOB_RLIMIT_THREAD",
	"thread",
	NULL};
/*
 *-----------------------------------------------------------------------
 *
 * getJobStartLimitHashmapArray -- jguo, 2011-03-02
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * logrec[IN]: struct eventRec pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_STARTLIMIT type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getJobStartLimitHashmapArray(struct eventRec *logrec) {
	//long lsfArrayIdx;
	//long lsfJobId;
	int i = 0;
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	//long gmt;
	//char *time;
	/* create new array instance. */
	// execHostsArray = _VECTOR_(env, clsHASHMAP, 1);
	// objHost = (*env)->NewObject(env, clsHASHMAP, HashMap_init);
	execHostsArray = jCreateArray();
	objHost = jCreateObject();

	if (NULL == objHost) {
		// throw_exception_by_key(env, logger, "perf.lsf.events.nullObject", NULL);
		return NULL;
	}

	// Add nested level cluster_rlimit and job_rlimit by ZK
	Json4c * clusterRLimit = jCreateObject();
	addInstanceToObject(objHost, FIELD_CLUSTER_RLIMIT, clusterRLimit);

	Json4c * jobRLimit = jCreateObject();
	addInstanceToObject(objHost, FIELD_JOB_RLIMIT, jobRLimit);

	for (i = 0; i < LSF_RLIM_NLIMITS; i++) {
		addNumberToObject(clusterRLimit, lsfRlimitsStr[i],
				logrec->eventLog.jobStartLimitLog.lsfLimits[i]);
		addNumberToObject(jobRLimit, jobRlimitsStr[i],
				logrec->eventLog.jobStartLimitLog.jobRlimits[i]);
	}

	addInstanceToArray(execHostsArray, objHost);
	return execHostsArray;
}

static void putJobStartLimit(Json4c *objHost, struct eventRec *logrec) {
	long lsfArrayIdx;
	long lsfJobId;
	int i = 0;
	char *time;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(logrec->eventLog.jobStartLimitLog.jobId);
	lsfJobId = LSB_ARRAY_JOBID(logrec->eventLog.jobStartLimitLog.jobId);

	addStringToObject(objHost, FIELD_VERSION, logrec->version);
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHost, FIELD_EVENT_TIME_STR, time);
	FREEUP(time);
	addNumberToObject(objHost, FIELD_EPOCH, (int)logrec->eventTime);

	addNumberToObject(objHost, FIELD_JOB_ID, lsfJobId);

	addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, lsfArrayIdx);

	addStringToObject(objHost, FIELD_CLUSTER_NAME, logrec->eventLog.jobStartLimitLog.clusterName);

	// Add nested level cluster_rlimit and job_rlimit by ZK
	Json4c * clusterRLimit = jCreateObject();
	addInstanceToObject(objHost, FIELD_CLUSTER_RLIMIT, clusterRLimit);

	Json4c * jobRLimit = jCreateObject();
	addInstanceToObject(objHost, FIELD_JOB_RLIMIT, jobRLimit);

	for (i = 0; i < LSF_RLIM_NLIMITS; i++) {
		addNumberToObject(clusterRLimit, lsfRlimitsStr[i],
				logrec->eventLog.jobStartLimitLog.lsfLimits[i]);
		addNumberToObject(jobRLimit, jobRlimitsStr[i],
				logrec->eventLog.jobStartLimitLog.jobRlimits[i]);
	}
}

#endif
/*
 *-----------------------------------------------------------------------
 *
 * putMig -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_MIG type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putMig(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);

	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.migLog.jobId);

	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.migLog.userId);

	addStringToObject(objHashMap, FIELD_USER_NAME, logrec->eventLog.migLog.userName);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.migLog.idx);

	/*put the asked host list */
	addNumberToObject(objHashMap, FIELD_NUM_ASKED_HOSTS,
			logrec->eventLog.migLog.numAskedHosts);

	if (logrec->eventLog.migLog.numAskedHosts > 0) {
		putAskedhostlist(objHashMap, logrec->eventLog.migLog.numAskedHosts,
				logrec->eventLog.migLog.askedHosts);
	}
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobModify2 -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_MODIFY2 type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobModify2(Json4c *objHashMap, struct eventRec *logrec) {
	Json4c *askedHostsArray = NULL;

	int jobCount = 1; /* Number of jobs in the job array */
	char *jobIndexList = NULL; /* Job Indexes in the job array */
	char *idx = NULL;
	char *time;
	putJobHEAD(objHashMap, logrec);
	/* this type have no idx, so default is :0*/

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, 0);

	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.jobModLog.userId);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS,
			logrec->eventLog.jobModLog.options);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS_2,
				logrec->eventLog.jobModLog.options2);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS_DELETE,
				logrec->eventLog.jobModLog.delOptions);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS_2_DELETE,
				logrec->eventLog.jobModLog.delOptions2);

	if (logrec->eventLog.jobModLog.jobIdStr != NULL) {
		addNumberToObject(objHashMap, FIELD_JOB_ID,
						atoi(logrec->eventLog.jobModLog.jobIdStr));

		/* handle this case: the jobid including the index , eg: 128[11].*/
		if ((idx = strchr(logrec->eventLog.jobModLog.jobIdStr, '[')) != NULL) {
			/* eg:128[11] to parse into 128 and 11. idx point to the '['. so idx+1
			 * point to the 11.*/
			//   hashmap_put_int(env, objHashMap, HashMap_put, "idx", atoi(idx + 1));
			addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, atoi(idx + 1));

		}
	}

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobModLog.userName);

	addNumberToObject(objHashMap, FIELD_SUBMIT_TIME, logrec->eventLog.jobModLog.submitTime);
	time = timeConvert(logrec->eventLog.jobModLog.submitTime);
	addStringToObject(objHashMap, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_BEGIN_TIME, logrec->eventLog.jobModLog.beginTime);
	time = timeConvert(logrec->eventLog.jobModLog.beginTime);
	addStringToObject(objHashMap, FIELD_BEGIN_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHashMap, FIELD_TERM_TIME, logrec->eventLog.jobModLog.termTime);
	time = timeConvert(logrec->eventLog.jobModLog.termTime);
	addStringToObject(objHashMap, FIELD_TERM_TIME_STR, time);
	FREEUP(time);



	addNumberToObject(objHashMap, FIELD_UMASK, logrec->eventLog.jobModLog.umask);
	addNumberToObject(objHashMap, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobModLog.numProcessors);


	addNumberToObject(objHashMap, FIELD_SIGNAL_VALUE,
				logrec->eventLog.jobModLog.sigValue);

	addNumberToObject(objHashMap, FIELD_RESTART_PID,
				logrec->eventLog.jobModLog.restartPid);

	addStringToObject(objHashMap, FIELD_JOB_NAME,
				logrec->eventLog.jobModLog.jobName);

	addStringToObject(objHashMap, FIELD_JOB_NAME_FULL,
				logrec->eventLog.jobModLog.jobName);

	addStringToObject(objHashMap, FIELD_QUEUE_NAME, logrec->eventLog.jobModLog.queue);
	addStringToObject(objHashMap, FIELD_RES_REQ, logrec->eventLog.jobModLog.resReq);

	addStringToObject(objHashMap, FIELD_HOST_SPEC,
				logrec->eventLog.jobModLog.hostSpec);

	addStringToObject(objHashMap, FIELD_DEPEND_COND,
			logrec->eventLog.jobModLog.dependCond);

	/*if(logrec->eventLog.jobModLog.timeEvent !=NULL)*/
	addStringToObject(objHashMap, FIELD_TIME_EVENT,
				logrec->eventLog.jobModLog.timeEvent);

	addStringToObject(objHashMap, FIELD_SUBMITTER_HOME,
				logrec->eventLog.jobModLog.subHomeDir);

	addStringToObject(objHashMap, FIELD_IN_FILE, logrec->eventLog.jobModLog.inFile);

	addStringToObject(objHashMap, FIELD_OUT_FILE,
				logrec->eventLog.jobModLog.outFile);

	addStringToObject(objHashMap, FIELD_ERR_FILE,
				logrec->eventLog.jobModLog.errFile);

	addStringToObject(objHashMap, FIELD_JOB_COMMAND,
			logrec->eventLog.jobModLog.command);

	addStringToObject(objHashMap, FIELD_IN_FILE_SPOOL,
			logrec->eventLog.jobModLog.inFileSpool);

	addStringToObject(objHashMap, FIELD_COMMAND_SPOOL,
				logrec->eventLog.jobModLog.commandSpool);

	addNumberToObject(objHashMap, FIELD_CHECKPOINT_INTERVAL,
			logrec->eventLog.jobModLog.chkpntPeriod);

	addStringToObject(objHashMap, FIELD_CHECKPOINT_DIR,
				logrec->eventLog.jobModLog.chkpntDir);

	addStringToObject(objHashMap, FIELD_JOB_FILE,
				logrec->eventLog.jobModLog.jobFile);

	addStringToObject(objHashMap, FIELD_SUBMISSION_HOST_NAME,
				logrec->eventLog.jobModLog.fromHost);

	/* cwd */
	if (logrec->eventLog.jobModLog.cwd != NULL
			&& logrec->eventLog.jobModLog.cwd[0] != '\0') {
		char cwd[MAXFULLFILENAMELEN];
		memset(cwd, 0, MAXFULLFILENAMELEN);

		if (logrec->eventLog.jobModLog.cwd[0] == '/'
				|| logrec->eventLog.jobModLog.cwd[0] == '\\'
				|| (logrec->eventLog.jobModLog.cwd[0] != '\0'
						&& logrec->eventLog.jobModLog.cwd[1] == ':'))
			sprintf(cwd, "%s", logrec->eventLog.jobModLog.cwd);
		else if (logrec->eventLog.jobModLog.cwd[0] == '\0')
			sprintf(cwd, "$HOME");
		else
			sprintf(cwd, "$HOME/%s", logrec->eventLog.jobModLog.cwd);

		// hashmap_put_string(env, objHashMap, HashMap_put, "execcwd", cwd);
		addStringToObject(objHashMap, FIELD_EXEC_CWD, cwd);
	}

	addStringToObject(objHashMap, FIELD_PREEXEC_CMD,
			logrec->eventLog.jobModLog.preExecCmd);

	addStringToObject(objHashMap, FIELD_MAIL_USER,
				logrec->eventLog.jobModLog.mailUser);

	addStringToObject(objHashMap, FIELD_PROJECT_NAME,
			logrec->eventLog.jobModLog.projectName);

	addNumberToObject(objHashMap, FIELD_NIOS_PORT,
				logrec->eventLog.jobModLog.niosPort);

	addNumberToObject(objHashMap, FIELD_REQ_NUM_PROCS_MAX,
				logrec->eventLog.jobModLog.maxNumProcessors);

	addStringToObject(objHashMap, FIELD_LOGIN_SHELL,
				logrec->eventLog.jobModLog.loginShell);

	addStringToObject(objHashMap, FIELD_SCHED_HOST_TYPE,
				logrec->eventLog.jobModLog.schedHostType);

	addStringToObject(objHashMap, FIELD_USER_GROUP_NAME,
				logrec->eventLog.jobModLog.userGroup);

	addStringToObject(objHashMap, FIELD_EXCEPT_LIST,
				logrec->eventLog.jobModLog.exceptList);

	addNumberToObject(objHashMap, FIELD_PRIORITY,
			logrec->eventLog.jobModLog.userPriority);

	addStringToObject(objHashMap, FIELD_ADV_RSV_ID, logrec->eventLog.jobModLog.rsvId);

	addStringToObject(objHashMap, FIELD_EXTSCHED,
				logrec->eventLog.jobModLog.extsched);

	addNumberToObject(objHashMap, FIELD_WARNING_TIME_PERIOD,
				logrec->eventLog.jobModLog.warningTimePeriod);

	addStringToObject(objHashMap, FIELD_WARNING_ACTION,
				logrec->eventLog.jobModLog.warningAction);

	addStringToObject(objHashMap, FIELD_JOB_GROUP,
			logrec->eventLog.jobModLog.jobGroup);

	addStringToObject(objHashMap, FIELD_SLA, logrec->eventLog.jobModLog.sla);

	addStringToObject(objHashMap, FIELD_LIC_PROJECT_NAME,
				logrec->eventLog.jobModLog.licenseProject);


	/* put rLimit array. */
	// modified by zk on 2017-04-18 to add nested object for rlimit values
	Json4c * rlimit = jCreateObject();
	putRLimitArray(rlimit, logrec->eventLog.jobModLog.rLimits);
	addInstanceToObject(objHashMap, FIELD_RESOURCE_LIMIT, rlimit);

	/* handle xf structure. */
	putXFStructure(objHashMap, logrec->eventLog.jobModLog.nxf,
			logrec->eventLog.jobModLog.xf);

	/*put the asked host list */
	addNumberToObject(objHashMap, FIELD_NUM_ASKED_HOSTS,
			logrec->eventLog.jobModLog.numAskedHosts);

	if (logrec->eventLog.jobModLog.numAskedHosts > 0)
		putAskedhostlist(objHashMap, logrec->eventLog.jobModLog.numAskedHosts,
				logrec->eventLog.jobModLog.askedHosts);

#if !defined(LSF6)
	/* put the application_tag into hashmap*/
	addNumberToObject(objHashMap, FIELD_JOB_OPTS_3,
				logrec->eventLog.jobModLog.options3);

	addNumberToObject(objHashMap, FIELD_JOB_OPTS_3_DELETE,
				logrec->eventLog.jobModLog.delOptions3);

	addStringToObject(objHashMap, FIELD_APP_PROFILE, logrec->eventLog.jobModLog.app);

	addStringToObject(objHashMap, FIELD_APS_STRING,
				logrec->eventLog.jobModLog.apsString);

	/* add postExecCmd, runtimeEStimation, 2007-7-24, jgao */
	addStringToObject(objHashMap, FIELD_POSTEXEC_CMD,
				logrec->eventLog.jobModLog.postExecCmd);

	addNumberToObject(objHashMap, FIELD_RUNTIME_EST,
				logrec->eventLog.jobModLog.runtimeEstimation);

#endif
	/* Count job number according to job name */
	jobCount = countJobByName(logrec->eventLog.jobNewLog.jobName,
			&jobIndexList);
	/* Put job count to hashmap */
	if (jobCount > 0) {
		// hashmap_put_int(env, objHashMap, HashMap_put, "numArrayElements",
		// jobCount);
		addNumberToObject(objHashMap, FIELD_NUM_ARR_ELEMENTS, jobCount);
	}
	/* Put job list to hashmap */
	if (jobIndexList != NULL) {
		addStringToObject(objHashMap, FIELD_JOB_IDX_LIST, jobIndexList);

		/* Release memory of the jobIndexList pointer */
		free(jobIndexList);
	}
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobSignal -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_SIGNAL type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobSignal(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.signalLog.jobId);

	addNumberToObject(objHashMap, FIELD_UID, logrec->eventLog.signalLog.userId);

	addStringToObject(objHashMap, FIELD_SIGNAL_SYMBOL,
				logrec->eventLog.signalLog.signalSymbol);

	addNumberToObject(objHashMap, FIELD_RUN_COUNT,
				logrec->eventLog.signalLog.runCount);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.signalLog.idx);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.signalLog.userName);
}

/*
 *------------------------------------------------------------------------
 * put the reserved host list into DB
 *
 * write by guosheng on 200-7-13
 * internal using
 *--------------------------------------------------------------------------
 */
static void putReservhostlist(Json4c *objHashMap, int reservNum,
		char **reservhostlist) {
	int i = 0, j;
	char *temp = NULL;
	char *buff = NULL;
	buff = calloc(reservNum, 512 * sizeof(char));
	temp = buff;
	for (i = 0; i < reservNum; i++) {
		j = sprintf(buff, " %s", reservhostlist[i]);
		buff += j;
	}
	buff = '\0';

	addStringToObject(objHashMap, FIELD_RESERVE_HOSTS, temp);

	/* free memory*/
	free(temp);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobForward -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_FORWARD type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobForward(Json4c *objHashMap, struct eventRec *logrec) {
	// jobjectArray reserHostsArray = NULL;
	Json4c *reserHostsArray = NULL;

	putJobHEAD(objHashMap, logrec);

	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobForwardLog.jobId);

	addStringToObject(objHashMap, FIELD_REMOTE_CLUSTER_NAME,
			logrec->eventLog.jobForwardLog.cluster);

	addNumberToObject(objHashMap, FIELD_REMOTE_JOB_ATTRS,
				logrec->eventLog.jobForwardLog.jobRmtAttr);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobForwardLog.idx);

	/*put the reserv host list */
	addNumberToObject(objHashMap, FIELD_NUM_RESERVE_HOSTS,
			logrec->eventLog.jobForwardLog.numReserHosts);

	if (logrec->eventLog.jobForwardLog.numReserHosts > 0)
		putReservhostlist(objHashMap,
				logrec->eventLog.jobForwardLog.numReserHosts,
				logrec->eventLog.jobForwardLog.reserHosts);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobAccept -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_ACCEPT type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.REMOTE_JID
 *
 *-----------------------------------------------------------------------
 */
static void putJobAccept(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobAcceptLog.jobId);

	/* fix the bug :74272 , add the REMOTE_JID*/
	addNumberToObject(objHashMap, FIELD_SRC_JOB_ID,
				logrec->eventLog.jobAcceptLog.remoteJid);

	addStringToObject(objHashMap, FIELD_SRC_CLUSTER_NAME,
			logrec->eventLog.jobAcceptLog.cluster);
	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobAcceptLog.idx);

	addNumberToObject(objHashMap, FIELD_REMOTE_JOB_ATTRS,
				logrec->eventLog.jobAcceptLog.jobRmtAttr);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobStartAccept -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_START_ACCEPT type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobStartAccept(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	char *time;
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobStartAcceptLog.jobId);
	addNumberToObject(objHashMap, FIELD_JOB_PID, logrec->eventLog.jobStartAcceptLog.jobPid);
	addNumberToObject(objHashMap, FIELD_JOB_PGID, logrec->eventLog.jobStartAcceptLog.jobPGid);
	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobStartAcceptLog.idx);

	addNumberToObject(objHashMap, FIELD_START_TIME, logrec->eventTime);
	addNumberToObject(objHashMap, FIELD_EVENT_TIME, logrec->eventTime);
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHashMap, FIELD_START_TIME_STR, time);
	addStringToObject(objHashMap, FIELD_EVENT_TIME_STR, time);
	FREEUP(time);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobSigact -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_SIGACT type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobSigact(Json4c *objHashMap, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.sigactLog.jobId);

	addNumberToObject(objHashMap, FIELD_ACTION_PERIOD,
			(int) logrec->eventLog.sigactLog.period);
	/* logrec->eventLog.sigactLog.pid is action process id.*/
	addNumberToObject(objHashMap, FIELD_ACTION_PID, logrec->eventLog.sigactLog.pid);

	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE, logrec->eventLog.sigactLog.jStatus);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.sigactLog.jStatus);
	addStringToObject(objHashMap, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHashMap, FIELD_PEND_SUBREASON,
			logrec->eventLog.sigactLog.reasons);
	addNumberToObject(objHashMap, FIELD_ACTION_FLAGS, logrec->eventLog.sigactLog.flags);
	addStringToObject(objHashMap, FIELD_SIGNAL_SYMBOL,
				logrec->eventLog.sigactLog.signalSymbol);

	addNumberToObject(objHashMap, FIELD_ACTION_STATUS_CODE,
				logrec->eventLog.sigactLog.actStatus);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.sigactLog.idx);


	free(jstatsstr);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobExecute -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_EXECUTE type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobExecute(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	char *time;

	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobExecuteLog.jobId);

	addNumberToObject(objHashMap, FIELD_EXEC_UID,
				logrec->eventLog.jobExecuteLog.execUid);

	addStringToObject(objHashMap, FIELD_EXEC_HOME,
				logrec->eventLog.jobExecuteLog.execHome);

	addStringToObject(objHashMap, FIELD_EXEC_CWD,
				logrec->eventLog.jobExecuteLog.execCwd);

	addNumberToObject(objHashMap, FIELD_JOB_PGID,
				logrec->eventLog.jobExecuteLog.jobPGid);

	addStringToObject(objHashMap, FIELD_EXEC_USER_NAME,
				logrec->eventLog.jobExecuteLog.execUsername);

	addNumberToObject(objHashMap, FIELD_JOB_PID,
				logrec->eventLog.jobExecuteLog.jobPid);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobExecuteLog.idx);

	addStringToObject(objHashMap, FIELD_ADDITIONAL_INFO,
				logrec->eventLog.jobExecuteLog.additionalInfo);

	addNumberToObject(objHashMap, FIELD_SLA_SCALED_RUN_LIMIT,
				logrec->eventLog.jobExecuteLog.SLAscaledRunLimit);

	addNumberToObject(objHashMap, FIELD_POSITION,
			logrec->eventLog.jobExecuteLog.position);

	addStringToObject(objHashMap, FIELD_EXEC_RUSAGE,
				logrec->eventLog.jobExecuteLog.execRusage);

	addNumberToObject(objHashMap, FIELD_START_TIME, logrec->eventTime);
	time = timeConvert(logrec->eventTime);
	addStringToObject(objHashMap, FIELD_START_TIME_STR, time);
	FREEUP(time);

#if !defined(LSF6)
	addNumberToObject(objHashMap, FIELD_PREEMPT_BACKFILL,
				logrec->eventLog.jobExecuteLog.duration4PreemptBackfill);

#endif
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobRequeue -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_REQUEUE type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobRequeue(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobRequeueLog.jobId);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobRequeueLog.idx);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobClean -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_CLEAN type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobClean(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID, logrec->eventLog.jobCleanLog.jobId);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobCleanLog.idx);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobException -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_EXCEPTION type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobException(Json4c *objHashMap, struct eventRec *logrec) {
	char time[20] = { '\0' };
	putJobHEAD(objHashMap, logrec);

	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobExceptionLog.jobId);

	addNumberToObject(objHashMap, FIELD_EXCEPT_MASK,
				logrec->eventLog.jobExceptionLog.exceptMask);

	addNumberToObject(objHashMap, FIELD_ACTION_MASK,
				logrec->eventLog.jobExceptionLog.actMask);

	sprintf(time, "%d", (int) logrec->eventLog.jobExceptionLog.timeEvent);
	addStringToObject(objHashMap, FIELD_TIME_EVENT, time);

	addNumberToObject(objHashMap, FIELD_EXCEPT_CODE,
				logrec->eventLog.jobExceptionLog.exceptInfo);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobExceptionLog.idx);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobExtMsg -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_EXT_MSG type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobExtMsg(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobExternalMsgLog.jobId);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX,
				logrec->eventLog.jobExternalMsgLog.idx);

	addNumberToObject(objHashMap, FIELD_MSG_INDEX,
				logrec->eventLog.jobExternalMsgLog.msgIdx);

	addStringToObject(objHashMap, FIELD_DESC,
			logrec->eventLog.jobExternalMsgLog.desc);
	addNumberToObject(objHashMap, FIELD_UID,
				logrec->eventLog.jobExternalMsgLog.userId);

	addNumberToObject(objHashMap, FIELD_DATA_SIZE,
				logrec->eventLog.jobExternalMsgLog.dataSize);

	addNumberToObject(objHashMap, FIELD_POST_TIME, (int) logrec->eventLog.jobExternalMsgLog.postTime);

	addNumberToObject(objHashMap, FIELD_DATA_STATUS,
				logrec->eventLog.jobExternalMsgLog.dataStatus);

	addStringToObject(objHashMap, FIELD_FILE_NAME,
				logrec->eventLog.jobExternalMsgLog.fileName);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobExternalMsgLog.userName);

}

/*
 *-----------------------------------------------------------------------
 *
 * putJobChunk -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_CHUNK type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobChunk(Json4c *objHashMap, struct eventRec *logrec) {

	// Json4c *execHostsArray = NULL;
	// char *execHostStr = NULL;
	int i = 0;
	char *temp = NULL;
	char *buff = NULL;
	int j = 0;
	/* In DB , support jobid lenght is 15bit.*/
	buff = calloc(logrec->eventLog.jobChunkLog.membSize, sizeof(char) * 16);
	temp = buff;
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_MEMB_SIZE,
				logrec->eventLog.jobChunkLog.membSize);

	for (i = 0; i < logrec->eventLog.jobChunkLog.membSize; i++) {
		j = sprintf(buff, LONG_FORMAT,
				logrec->eventLog.jobChunkLog.membJobId[i]);
		buff += j;
	}
	*buff = '\0';

	addStringToObject(objHashMap, FIELD_MEMB_JOB_ID, temp);

	/* this type have no idx, so default is :0*/
	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, 0);

	/* using  the first job id in the chunk job as the jobid.*/
	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobChunkLog.membJobId[0]);

	/* number of exec host*/
	addNumberToObject(objHashMap, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobChunkLog.numExHosts);
}

/*
 *------------------------------------------------------------------------
 * putJRusage--  ghu , 2006-9-8
 * write the struct jRusage into a Hashmap
 * the truct:
 struct jRusage {
 int mem;
 int swap;
 int utime;
 int stime;
 int npids;
 struct pidInfo *pidInfo;

 int npgids;
 int *pgid;
 int nthreads;
 }
 *
 *------------------------------------------------------------------------
 */
static void putJRusage(Json4c *objHashMap, struct jRusage *jrusage) {

	char *temp = NULL;
	char *pgid = NULL;
	int i = 0, j = 0;
	
	// added nested rusage object by zk on 2017-04-18
	Json4c *rusage = jCreateObject();
	addInstanceToObject(objHashMap, FIELD_RUN_RUSAGE, rusage);

	addNumberToObject(rusage, FIELD_RUN_RU_MEM_USAGE, jrusage->mem);
	addNumberToObject(rusage, FIELD_RUN_RU_SWAP_USAGE, jrusage->swap);
	addNumberToObject(rusage, FIELD_RU_UTIME, jrusage->utime);
	addNumberToObject(rusage, FIELD_RU_STIME, jrusage->stime);
	addNumberToObject(rusage, FIELD_NUM_PIDS, jrusage->npids);
	addNumberToObject(rusage, FIELD_NUM_THREADS, jrusage->nthreads);
	addNumberToObject(rusage, FIELD_NUM_PGIDS, jrusage->npgids);

	/* put the process group id (int) into a string*/
	if (jrusage->npgids > 0) {

		pgid = calloc(jrusage->npgids, 20 * sizeof(char));
		if (pgid == NULL) {
			return;
		}
		temp = pgid;
		/* fprintf(stderr, "MEM: %X\n", pgid);*/
		for (i = 0; i < jrusage->npgids; i++) {
			/*fprintf(stderr, "i= %d\t pgid = %d\n", i,
			 * logrec->eventLog.jobRunRusageLog.jrusage->pgid[i]);*/
			j = sprintf(pgid, "%d ", jrusage->pgid[i]);
			pgid += j;
		}

		*pgid = '\0';

		addStringToObject(rusage, FIELD_PGIDS, temp);

		free(temp);
	}
	/*put the pidinfo (4 fields) into a string */
	if (jrusage->npids > 0) {
		pgid = calloc(jrusage->npids, 44);
		temp = pgid;
		for (i = 0; i < jrusage->npids; i++) {
			j = sprintf(pgid, "%d ", jrusage->pidInfo[i].pid);
			pgid += j;
			j = sprintf(pgid, "%d ", jrusage->pidInfo[i].ppid);
			pgid += j;
			j = sprintf(pgid, "%d ", jrusage->pidInfo[i].pgid);
			pgid += j;
			j = sprintf(pgid, "%d ", jrusage->pidInfo[i].jobid);
			pgid += j;
		}
		*pgid = '\0';
		addStringToObject(rusage, FIELD_PID_INFO, temp);
		free(temp);
	}
}

/*
 *-----------------------------------------------------------------------
 *
 * putSBDUnreportedStatus -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_SBD_UNREPORTED_STATUS type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putSBDUnreportedStatus(Json4c *objHashMap, struct eventRec *logrec) {
	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID,
			logrec->eventLog.sbdUnreportedStatusLog.jobId);

	addNumberToObject(objHashMap, FIELD_ACTION_PID,
				logrec->eventLog.sbdUnreportedStatusLog.actPid);

	addNumberToObject(objHashMap, FIELD_JOB_PID,
				logrec->eventLog.sbdUnreportedStatusLog.jobPid);

	addNumberToObject(objHashMap, FIELD_JOB_PGID,
			logrec->eventLog.sbdUnreportedStatusLog.jobPGid);
			
	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE,
				logrec->eventLog.sbdUnreportedStatusLog.newStatus);

	addNumberToObject(objHashMap, FIELD_PEND_REASON,
			logrec->eventLog.sbdUnreportedStatusLog.reason);
	addNumberToObject(objHashMap, FIELD_PEND_SUBREASON,
			logrec->eventLog.sbdUnreportedStatusLog.subreasons);
	/* put lsfRusage struct. */
	putLsfRusage(objHashMap,
			&logrec->eventLog.sbdUnreportedStatusLog.lsfRusage);

	addNumberToObject(objHashMap, FIELD_EXEC_UID,
				logrec->eventLog.sbdUnreportedStatusLog.execUid);

	addNumberToObject(objHashMap, FIELD_EXIT_STATUS,
			logrec->eventLog.sbdUnreportedStatusLog.exitStatus);

	addStringToObject(objHashMap, FIELD_EXEC_CWD,
			logrec->eventLog.sbdUnreportedStatusLog.execCwd);

	addStringToObject(objHashMap, FIELD_EXEC_HOME,
				logrec->eventLog.sbdUnreportedStatusLog.execHome);

	addStringToObject(objHashMap, FIELD_EXEC_USER_NAME,
				logrec->eventLog.sbdUnreportedStatusLog.execUsername);

	addNumberToObject(objHashMap, FIELD_MSG_IDX,
				logrec->eventLog.sbdUnreportedStatusLog.msgId);

	addNumberToObject(objHashMap, FIELD_SIGNAL_VALUE,
			logrec->eventLog.sbdUnreportedStatusLog.sigValue);

	addNumberToObject(objHashMap, FIELD_ACTION_LOG_STATUS_CODE,
			logrec->eventLog.sbdUnreportedStatusLog.actStatus);

	addNumberToObject(objHashMap, FIELD_SEQUENCE,
			logrec->eventLog.sbdUnreportedStatusLog.seq);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX,
				logrec->eventLog.sbdUnreportedStatusLog.idx);

	addNumberToObject(objHashMap, FIELD_EXIT_INFO,
				logrec->eventLog.sbdUnreportedStatusLog.exitInfo);

	putJRusage(objHashMap, &logrec->eventLog.sbdUnreportedStatusLog.runRusage);
}

/*
 *-----------------------------------------------------------------------
 *
 * putJobForce -- xxin, 2006-06-21
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * objHashMap[IN]: Hashmap object.
 * logrec[IN]: struct eventRec pointer.
 * HashMap_put[IN]: Hashmap put method.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_FORCE type data to hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * NULL.
 *
 *-----------------------------------------------------------------------
 */
static void putJobForce(Json4c *objHashMap, struct eventRec *logrec) {

	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID,
				logrec->eventLog.jobForceRequestLog.jobId);

	addNumberToObject(objHashMap, FIELD_UID,
				logrec->eventLog.jobForceRequestLog.userId);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX,
			logrec->eventLog.jobForceRequestLog.idx);

	addNumberToObject(objHashMap, FIELD_RUN_OPTIONS,
			logrec->eventLog.jobForceRequestLog.options);

	addStringToObject(objHashMap, FIELD_USER_NAME,
				logrec->eventLog.jobForceRequestLog.userName);

	addStringToObject(objHashMap, FIELD_QUEUE_NAME,
			logrec->eventLog.jobForceRequestLog.queue);

	/* number of exec host*/
	addNumberToObject(objHashMap, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobForceRequestLog.numExecHosts);
}

/*
 *------------------------------------------------------------------------
 * putRunRusage -- ghu , 2006-7-3
 * write the run_rusage data into hashMap
 * the job_run_rusage struct is struct jobRunRusageLog {
 int              jobid;
 int              idx;
 struct jRusage   jrusage;
 };

 struct jRusage {
 int mem;
 int swap;
 int utime;
 int stime;
 int npids;
 struct pidInfo *pidInfo;

 int npgids;
 int *pgid;
 int nthreads;
 }


 struct pidInfo {
 int pid;          -- process id
 int ppid;         -- parent's process id
 int pgid;         -- processes' group id
 int jobid;        -- process' cray job id (only on Cray)
 }; -- information about a process with its process ID pid.


 *
 * ------------------------------------------------------------------------

 */
// static void putRunRusage(JNIEnv *env, jobject objHashMap,
//                          struct eventRec *logrec, jmethodID HashMap_put) {
static void putRunRusage(Json4c *objHashMap, struct eventRec *logrec) {

	putJobHEAD(objHashMap, logrec);
	addNumberToObject(objHashMap, FIELD_JOB_ID,
			logrec->eventLog.jobRunRusageLog.jobid);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, logrec->eventLog.jobRunRusageLog.idx);

	/*put the struct jRusage */
	putJRusage(objHashMap, &logrec->eventLog.jobRunRusageLog.jrusage);
}

#if !defined(LSF6)
/*
 *total 18 columns
 *in this method insert 17 columns
 *the last column event_type will be insert in the caller method.

 *thedjobs*/
static void putMetricLog(Json4c *objHashMap, struct eventRec *logrec) {
	float ver;
	putJobHEAD(objHashMap, logrec);

	// Add nested level perfmon by ZK
	Json4c * perfmon = jCreateObject();
	addInstanceToObject(objHashMap, FIELD_PERFORMANCE_METRIC, perfmon);
	objHashMap = perfmon;

	char *time;
	time = timeConvert(logrec->eventLog.perfmonLog.startTime);
	addStringToObject(objHashMap, FIELD_METRIC_LOGSTART, time);

	FREEUP(time);
	addNumberToObject(objHashMap, FIELD_METRIC_LOGINTERVAL,
			logrec->eventLog.perfmonLog.samplePeriod);

	addNumberToObject(objHashMap, FIELD_METRIC_MBD_REQS,
			logrec->eventLog.perfmonLog.totalQueries);

	addNumberToObject(objHashMap, FIELD_METRIC_JOB_QUERIES,
			logrec->eventLog.perfmonLog.jobQuries);

	addNumberToObject(objHashMap, FIELD_METRIC_QUEUE_QUERIES,
			logrec->eventLog.perfmonLog.queueQuries);

	addNumberToObject(objHashMap, FIELD_METRIC_HOST_QUERIES,
			logrec->eventLog.perfmonLog.hostQuries);

	addNumberToObject(objHashMap, FIELD_METRIC_JOB_SUBMIT_REQS,
			logrec->eventLog.perfmonLog.submissionRequest);

	addNumberToObject(objHashMap, FIELD_METRIC_JOBS_SUBMITTED,
			logrec->eventLog.perfmonLog.jobSubmitted);

	addNumberToObject(objHashMap, FIELD_METRIC_JOBS_DISPATCHED,
			logrec->eventLog.perfmonLog.dispatchedjobs);

	addNumberToObject(objHashMap, FIELD_METRIC_JOBS_COMPLETED,
			logrec->eventLog.perfmonLog.jobcompleted);

	addNumberToObject(objHashMap, FIELD_JOB_ID, 0);

	addNumberToObject(objHashMap, FIELD_JOB_ARRAY_IDX, 0);

	addNumberToObject(objHashMap, FIELD_JOB_STATUS_CODE, -1);
	
	addNumberToObject(objHashMap, FIELD_JOB_PID, -1);

	addNumberToObject(objHashMap, FIELD_METRIC_JOBS_TO_REMOTE,
			logrec->eventLog.perfmonLog.jobMCSend);

	addNumberToObject(objHashMap, FIELD_METRIC_JOBS_FROM_REMOTE,
			logrec->eventLog.perfmonLog.jobMCReceive);

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	ver = atof(logrec->version);
	if (ver >= 9.1) {
		addNumberToObject(objHashMap, FIELD_METRIC_MBD_FILES_FREE,
				logrec->eventLog.perfmonLog.mbdFreeHandle);

		addNumberToObject(objHashMap, FIELD_METRIC_MBD_FILES_USED,
				logrec->eventLog.perfmonLog.mbdUsedHandle);

		addNumberToObject(objHashMap, FIELD_METRIC_SCHED_INTERVAL,
				logrec->eventLog.perfmonLog.scheduleInterval);

		addNumberToObject(objHashMap, FIELD_METRIC_MATCH_HOST_CRIT,
				logrec->eventLog.perfmonLog.hostRequirements);

		addNumberToObject(objHashMap, FIELD_METRIC_JOB_BUCKETS,
				logrec->eventLog.perfmonLog.jobBuckets);

	}
#endif

} /*end putEventMetricLog*/
#endif

static int getSlotFlag(struct jobStatus2Log *jobStatus2Log) {
	int tmpFlag = 0;
	/* Slot flag = 1 when job status is RUN, SSUSP, or USUSP */
	tmpFlag = (jobStatus2Log->jStatus == JOB_STAT_RUN)
			|| (jobStatus2Log->jStatus == JOB_STAT_SSUSP)
			|| (jobStatus2Log->jStatus == JOB_STAT_USUSP);

#ifdef LSF7
	/* Job was preempted by a higher priority job */
	if (jobStatus2Log->reason & SUSP_MBD_PREEMPT && !(jobStatus2Log->reason
					== SUSP_HOST_RSVACTIVE) && !(jobStatus2Log->reason
					== SUSP_ADVRSV_EXPIRED)) {
#else
	/* Job was preempted by a higher priority job */
	if (jobStatus2Log->reason & SUSP_MBD_PREEMPT
			&& !(jobStatus2Log->reason & SUSP_HOST_RSVACTIVE)) {
#endif
		tmpFlag = 0;
	}
	return tmpFlag;
}

/*
 *-----------------------------------------------------------------------
 *
 * getJobStatus2 -- jguo, 2011-04-18
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * logrec[IN]: struct eventRec pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_STATUS2 type data to hashmap object array.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getJobStatus2HashmapArray(struct eventRec *logrec) {
	char *jstatsstr = NULL;
	long lsfArrayIdx;
	long lsfJobId;
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	char *time;
	int slotFlag = 0;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(logrec->eventLog.jobStatus2Log.jobId);
	lsfJobId = LSB_ARRAY_JOBID(logrec->eventLog.jobStatus2Log.jobId);
	/* create new array instance. */
	execHostsArray = jCreateArray();
	objHost = jCreateObject();
	if (NULL == objHost) {
//		throw_exception_by_key(env, logger, "perf.lsf.events.nullObject", NULL);
		return NULL;
	}

	putJobHEAD(objHost, logrec);
	addNumberToObject(objHost, FIELD_JOB_ID, lsfJobId);

	addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, lsfArrayIdx);

	if (logrec->eventLog.jobStatus2Log.userName
			&& strlen(logrec->eventLog.jobStatus2Log.userName)) {
		addStringToObject(objHost, FIELD_USER_NAME,
						logrec->eventLog.jobStatus2Log.userName);

	} else {
		addStringToObject(objHost, FIELD_USER_NAME, "-");

	}

	addNumberToObject(objHost, FIELD_SAMPLE_INTERVAL,
				logrec->eventLog.jobStatus2Log.sampleInterval);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobStatus2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_JOBS,
				logrec->eventLog.jobStatus2Log.numJobs);

	/* add jstatus string*/

	jstatsstr = transformJstatus(logrec->eventLog.jobStatus2Log.jStatus);
	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHost, FIELD_SUBMIT_TIME, logrec->eventLog.jobStatus2Log.submitTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.submitTime);
	addStringToObject(objHost, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_START_TIME, logrec->eventLog.jobStatus2Log.startTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.startTime);
	addStringToObject(objHost, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_END_TIME, logrec->eventLog.jobStatus2Log.endTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.endTime);
	addStringToObject(objHost, FIELD_END_TIME_STR, time);
	FREEUP(time);

	if (logrec->eventLog.jobStatus2Log.queue
			&& strlen(logrec->eventLog.jobStatus2Log.queue)) {
		addStringToObject(objHost, FIELD_QUEUE_NAME,
				logrec->eventLog.jobStatus2Log.queue);
	} else {
		addStringToObject(objHost, FIELD_QUEUE_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.resReq
			&& strlen(logrec->eventLog.jobStatus2Log.resReq)) {
		addStringToObject(objHost, FIELD_RES_REQ,
					logrec->eventLog.jobStatus2Log.resReq);

	} else {
		addStringToObject(objHost, FIELD_RES_REQ, "-");
	}
	if (logrec->eventLog.jobStatus2Log.projectName
			&& strlen(logrec->eventLog.jobStatus2Log.projectName)) {
		addStringToObject(objHost, FIELD_PROJECT_NAME,
						logrec->eventLog.jobStatus2Log.projectName);
	} else {
		addStringToObject(objHost, FIELD_PROJECT_NAME, "-");
	}
#if !defined(LSF6)
	if (logrec->eventLog.jobStatus2Log.jgroup
			&& strlen(logrec->eventLog.jobStatus2Log.jgroup)) {
		addStringToObject(objHost, FIELD_JOB_GROUP,
						logrec->eventLog.jobStatus2Log.jgroup);
	} else {
		addStringToObject(objHost, FIELD_JOB_GROUP, "-");
	}
#endif
	/* put lsfRusage struct. */
	putLsfRusage(objHost, &logrec->eventLog.jobStatus2Log.lsfRusage);
	/* number of exec host*/
	addNumberToObject(objHost, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobStatus2Log.numExHosts);
#if !defined(LSF6)
	/* put the application_tag into hashmap*/

	if (logrec->eventLog.jobStatus2Log.app
			&& strlen(logrec->eventLog.jobStatus2Log.app)) {

		addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobStatus2Log.app);
	} else {
		addStringToObject(objHost, FIELD_APP_PROFILE, "-");
	}
#endif
	if (logrec->eventLog.jobStatus2Log.execRusage
			&& strlen(logrec->eventLog.jobStatus2Log.execRusage)) {
		addStringToObject(objHost, FIELD_EXEC_RUSAGE,
						logrec->eventLog.jobStatus2Log.execRusage);
	} else {
		addStringToObject(objHost, FIELD_EXEC_RUSAGE, "-");
	}
	if (logrec->eventLog.jobStatus2Log.clusterName
			&& strlen(logrec->eventLog.jobStatus2Log.clusterName)) {

		addStringToObject(objHost, FIELD_CLUSTER_NAME,
						logrec->eventLog.jobStatus2Log.clusterName);
	} else {
		addStringToObject(objHost, FIELD_CLUSTER_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.userGroup
			&& strlen(logrec->eventLog.jobStatus2Log.userGroup)) {

		addStringToObject(objHost, FIELD_USER_GROUP_NAME,
						logrec->eventLog.jobStatus2Log.userGroup);
	} else {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME, "-");
	}

	addNumberToObject(objHost, FIELD_RUNTIME_DELTA,
				logrec->eventLog.jobStatus2Log.runtimeDelta);

	addNumberToObject(objHost, FIELD_REMOTE_ATT,
				logrec->eventLog.jobStatus2Log.jobRmtAttr);

	addNumberToObject(objHost, FIELD_PEND_REASON, logrec->eventLog.jobStatus2Log.reason);
#if defined(LSF9) || defined(LSF10)
	addNumberToObject(objHost, FIELD_PROVTIME_DELTA,
				logrec->eventLog.jobStatus2Log.provtimeDelta);

	if(logrec->eventLog.jobStatus2Log.dcJobFlags & AC_JOB_INFO_VMJOB) {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "Y");
	} else {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "N");
	}
#endif
	/* Slot flag */
	slotFlag = getSlotFlag(&(logrec->eventLog.jobStatus2Log));
	addNumberToObject(objHost, FIELD_SLOT_FLAG, slotFlag);

	addInstanceToArray(execHostsArray, objHost);
	free(jstatsstr);
	return execHostsArray;
}

static void putJobStatus2(Json4c *objHost, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	long lsfArrayIdx;
	long lsfJobId;
	char *time;
	int slotFlag = 0;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(logrec->eventLog.jobStatus2Log.jobId);
	lsfJobId = LSB_ARRAY_JOBID(logrec->eventLog.jobStatus2Log.jobId);
	putJobHEAD(objHost, logrec);
	
	addNumberToObject(objHost, FIELD_JOB_ID, lsfJobId);

	addNumberToObject(objHost, FIELD_JOB_ARRAY_IDX, lsfArrayIdx);

	if (logrec->eventLog.jobStatus2Log.userName
			&& strlen(logrec->eventLog.jobStatus2Log.userName)) {
		addStringToObject(objHost, FIELD_USER_NAME,
						logrec->eventLog.jobStatus2Log.userName);

	} else {
		addStringToObject(objHost, FIELD_USER_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_SAMPLE_INTERVAL,
				logrec->eventLog.jobStatus2Log.sampleInterval);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobStatus2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_JOBS,
				logrec->eventLog.jobStatus2Log.numJobs);

	/* add jstatus string*/

	jstatsstr = transformJstatus(logrec->eventLog.jobStatus2Log.jStatus);
	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	addNumberToObject(objHost, FIELD_SUBMIT_TIME, logrec->eventLog.jobStatus2Log.submitTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.submitTime);
	addStringToObject(objHost, FIELD_SUBMIT_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_START_TIME, logrec->eventLog.jobStatus2Log.startTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.startTime);
	addStringToObject(objHost, FIELD_START_TIME_STR, time);
	FREEUP(time);

	addNumberToObject(objHost, FIELD_END_TIME, logrec->eventLog.jobStatus2Log.endTime);
	time = timeConvert(logrec->eventLog.jobStatus2Log.endTime);
	addStringToObject(objHost, FIELD_END_TIME_STR, time);
	FREEUP(time);


	if (logrec->eventLog.jobStatus2Log.queue
			&& strlen(logrec->eventLog.jobStatus2Log.queue)) {
		addStringToObject(objHost, FIELD_QUEUE_NAME,
				logrec->eventLog.jobStatus2Log.queue);
	} else {
		addStringToObject(objHost, FIELD_QUEUE_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.resReq
			&& strlen(logrec->eventLog.jobStatus2Log.resReq)) {
		addStringToObject(objHost, FIELD_RES_REQ,
						logrec->eventLog.jobStatus2Log.resReq);

	} else {
		addStringToObject(objHost, FIELD_RES_REQ, "-");
	}
	if (logrec->eventLog.jobStatus2Log.projectName
			&& strlen(logrec->eventLog.jobStatus2Log.projectName)) {
		addStringToObject(objHost, FIELD_PROJECT_NAME,
					logrec->eventLog.jobStatus2Log.projectName);
	} else {
		addStringToObject(objHost, FIELD_PROJECT_NAME, "-");
	}
#if !defined(LSF6)
	if (logrec->eventLog.jobStatus2Log.jgroup
			&& strlen(logrec->eventLog.jobStatus2Log.jgroup)) {
		addStringToObject(objHost, FIELD_JOB_GROUP,
					logrec->eventLog.jobStatus2Log.jgroup);
	} else {
		addStringToObject(objHost, FIELD_JOB_GROUP, "-");
	}
#endif
	/* put lsfRusage struct. */
	putLsfRusage(objHost, &logrec->eventLog.jobStatus2Log.lsfRusage);
	/* number of exec host*/
	addNumberToObject(objHost, FIELD_NUM_EXEC_HOSTS,
			logrec->eventLog.jobStatus2Log.numExHosts);
#if !defined(LSF6)
	/* put the application_tag into hashmap*/

	if (logrec->eventLog.jobStatus2Log.app
			&& strlen(logrec->eventLog.jobStatus2Log.app)) {
		addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobStatus2Log.app);
	} else {
		addStringToObject(objHost, FIELD_APP_PROFILE, "-");
	}
#endif
	if (logrec->eventLog.jobStatus2Log.execRusage
			&& strlen(logrec->eventLog.jobStatus2Log.execRusage)) {
		addStringToObject(objHost, FIELD_EXEC_RUSAGE,
						logrec->eventLog.jobStatus2Log.execRusage);
	} else {
		addStringToObject(objHost, FIELD_EXEC_RUSAGE, "-");
	}
	if (logrec->eventLog.jobStatus2Log.clusterName
			&& strlen(logrec->eventLog.jobStatus2Log.clusterName)) {
		addStringToObject(objHost, FIELD_CLUSTER_NAME,
						logrec->eventLog.jobStatus2Log.clusterName);
	} else {
		addStringToObject(objHost, FIELD_CLUSTER_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.userGroup
			&& strlen(logrec->eventLog.jobStatus2Log.userGroup)) {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME,
				logrec->eventLog.jobStatus2Log.userGroup);
	} else {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_RUNTIME_DELTA,
				logrec->eventLog.jobStatus2Log.runtimeDelta);
				
	addNumberToObject(objHost, FIELD_REMOTE_ATT,
			logrec->eventLog.jobStatus2Log.jobRmtAttr);

	addNumberToObject(objHost, FIELD_PEND_REASON, logrec->eventLog.jobStatus2Log.reason);
#if defined(LSF9) || defined(LSF10)
	addNumberToObject(objHost, FIELD_PROVTIME_DELTA,
			logrec->eventLog.jobStatus2Log.provtimeDelta);

	if(logrec->eventLog.jobStatus2Log.dcJobFlags & AC_JOB_INFO_VMJOB) {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "Y");
	} else {
		addStringToObject(objHost, FIELD_VIRTUALIZATION, "N");
	}
#endif
	/* Slot flag */
	slotFlag = getSlotFlag(&(logrec->eventLog.jobStatus2Log));
	addNumberToObject(objHost, FIELD_SLOT_FLAG, slotFlag);
	free(jstatsstr);
}

/*
 *-----------------------------------------------------------------------
 *
 * getJobStatus2Pend -- jguo, 2011-05-18
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * logrec[IN]: struct eventRec pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * put EVENT_JOB_STATUS2 type pend data to hashmap object array.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getJobStatus2PendHashmapArray(struct eventRec *logrec) {
	char *jstatsstr = NULL;
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	/* create new array instance. */
	execHostsArray = jCreateArray();
	objHost = jCreateObject();
	if (NULL == objHost) {
//		throw_exception_by_key(env, logger, "perf.lsf.events.nullObject", NULL);
		return NULL;
	}
	putJobHEAD(objHost, logrec);
	addNumberToObject(objHost, FIELD_JOB_ID, logrec->eventLog.jobStatus2Log.jobId);

	if (logrec->eventLog.jobStatus2Log.userName
			&& strlen(logrec->eventLog.jobStatus2Log.userName)) {
		addStringToObject(objHost, FIELD_USER_NAME,
						logrec->eventLog.jobStatus2Log.userName);

	} else {
		addStringToObject(objHost, FIELD_USER_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_SAMPLE_INTERVAL,
				logrec->eventLog.jobStatus2Log.sampleInterval);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
				logrec->eventLog.jobStatus2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_EXEC_PROCESSORS,
			logrec->eventLog.jobStatus2Log.num_processors);

	addNumberToObject(objHost, FIELD_NUM_JOBS,
			logrec->eventLog.jobStatus2Log.numJobs);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobStatus2Log.jStatus);
	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	if (logrec->eventLog.jobStatus2Log.queue
			&& strlen(logrec->eventLog.jobStatus2Log.queue)) {
		addStringToObject(objHost, FIELD_QUEUE_NAME,
				logrec->eventLog.jobStatus2Log.queue);
	} else {
		addStringToObject(objHost, FIELD_QUEUE_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.resReq
			&& strlen(logrec->eventLog.jobStatus2Log.resReq)) {
		addStringToObject(objHost, FIELD_RES_REQ,
						logrec->eventLog.jobStatus2Log.resReq);

	} else {
		addStringToObject(objHost, FIELD_RES_REQ, "-");
	}
	if (logrec->eventLog.jobStatus2Log.projectName
			&& strlen(logrec->eventLog.jobStatus2Log.projectName)) {
		addStringToObject(objHost, FIELD_PROJECT_NAME,
						logrec->eventLog.jobStatus2Log.projectName);
	} else {
		addStringToObject(objHost, FIELD_PROJECT_NAME, "-");
	}
#if !defined(LSF6)
	if (logrec->eventLog.jobStatus2Log.jgroup
			&& strlen(logrec->eventLog.jobStatus2Log.jgroup)) {
		addStringToObject(objHost, FIELD_JOB_GROUP,
						logrec->eventLog.jobStatus2Log.jgroup);
	} else {
		addStringToObject(objHost, FIELD_JOB_GROUP, "-");
	}
#endif
#if !defined(LSF6)
	/* put the application_tag into hashmap*/
	if (logrec->eventLog.jobStatus2Log.app
			&& strlen(logrec->eventLog.jobStatus2Log.app)) {
		addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobStatus2Log.app);
	} else {
		addStringToObject(objHost, FIELD_APP_PROFILE, "-");
	}
#endif
	if (logrec->eventLog.jobStatus2Log.clusterName
			&& strlen(logrec->eventLog.jobStatus2Log.clusterName)) {
		addStringToObject(objHost, FIELD_CLUSTER_NAME,
						logrec->eventLog.jobStatus2Log.clusterName);
	} else {
		addStringToObject(objHost, FIELD_CLUSTER_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.userGroup
			&& strlen(logrec->eventLog.jobStatus2Log.userGroup)) {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME,
						logrec->eventLog.jobStatus2Log.userGroup);
	} else {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_REMOTE_ATT,
				logrec->eventLog.jobStatus2Log.jobRmtAttr);

	addNumberToObject(objHost, FIELD_PEND_REASON, logrec->eventLog.jobStatus2Log.reason);
	addInstanceToArray(execHostsArray, objHost);
	free(jstatsstr);
	return execHostsArray;
}

static void putJobStatus2Pend(Json4c *objHost, struct eventRec *logrec) {
	char *jstatsstr = NULL;
	putJobHEAD(objHost, logrec);
	addNumberToObject(objHost, FIELD_JOB_ID, logrec->eventLog.jobStatus2Log.jobId);

	if (logrec->eventLog.jobStatus2Log.userName
			&& strlen(logrec->eventLog.jobStatus2Log.userName)) {
		addStringToObject(objHost, FIELD_USER_NAME,
						logrec->eventLog.jobStatus2Log.userName);

	} else {
		addStringToObject(objHost, FIELD_USER_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_SAMPLE_INTERVAL,
			logrec->eventLog.jobStatus2Log.sampleInterval);

	addNumberToObject(objHost, FIELD_NUM_PROCESSORS,
			logrec->eventLog.jobStatus2Log.numProcessors);

	addNumberToObject(objHost, FIELD_NUM_EXEC_PROCESSORS,
			logrec->eventLog.jobStatus2Log.num_processors);
			
	addNumberToObject(objHost, FIELD_NUM_JOBS,
				logrec->eventLog.jobStatus2Log.numJobs);

	/* add jstatus string*/
	jstatsstr = transformJstatus(logrec->eventLog.jobStatus2Log.jStatus);
	addStringToObject(objHost, FIELD_JOB_STATUS, jstatsstr);

	if (logrec->eventLog.jobStatus2Log.queue
			&& strlen(logrec->eventLog.jobStatus2Log.queue)) {
		addStringToObject(objHost, FIELD_QUEUE_NAME,
				logrec->eventLog.jobStatus2Log.queue);
	} else {
		addStringToObject(objHost, FIELD_QUEUE_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.resReq
			&& strlen(logrec->eventLog.jobStatus2Log.resReq)) {
		addStringToObject(objHost, FIELD_RES_REQ,
				logrec->eventLog.jobStatus2Log.resReq);
	} else {
		addStringToObject(objHost, FIELD_RES_REQ, "-");
	}
	if (logrec->eventLog.jobStatus2Log.projectName
			&& strlen(logrec->eventLog.jobStatus2Log.projectName)) {
		addStringToObject(objHost, FIELD_PROJECT_NAME,
				logrec->eventLog.jobStatus2Log.projectName);
	} else {
		addStringToObject(objHost, FIELD_PROJECT_NAME, "-");
	}
#if !defined(LSF6)
	if (logrec->eventLog.jobStatus2Log.jgroup
			&& strlen(logrec->eventLog.jobStatus2Log.jgroup)) {
		addStringToObject(objHost, FIELD_JOB_GROUP,
				logrec->eventLog.jobStatus2Log.jgroup);
	} else {
		addStringToObject(objHost, FIELD_JOB_GROUP, "-");
	}
#endif
#if !defined(LSF6)
	/* put the application_tag into hashmap*/
	if (logrec->eventLog.jobStatus2Log.app
			&& strlen(logrec->eventLog.jobStatus2Log.app)) {
		addStringToObject(objHost, FIELD_APP_PROFILE, logrec->eventLog.jobStatus2Log.app);
	} else {
		addStringToObject(objHost, FIELD_APP_PROFILE, "-");
	}
#endif
	if (logrec->eventLog.jobStatus2Log.clusterName
			&& strlen(logrec->eventLog.jobStatus2Log.clusterName)) {
		addStringToObject(objHost, FIELD_CLUSTER_NAME,
				logrec->eventLog.jobStatus2Log.clusterName);
	} else {
		addStringToObject(objHost, FIELD_CLUSTER_NAME, "-");
	}
	if (logrec->eventLog.jobStatus2Log.userGroup
			&& strlen(logrec->eventLog.jobStatus2Log.userGroup)) {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME,
					logrec->eventLog.jobStatus2Log.userGroup);
	} else {
		addStringToObject(objHost, FIELD_USER_GROUP_NAME, "-");
	}
	addNumberToObject(objHost, FIELD_REMOTE_ATT,
			logrec->eventLog.jobStatus2Log.jobRmtAttr);

	addNumberToObject(objHost, FIELD_PEND_REASON, logrec->eventLog.jobStatus2Log.reason);
	free(jstatsstr);
}

/*
 *-----------------------------------------------------------------------
 *
 * getJobRunTimeHostsHashmapArray -- jguo, 2011-04-18
 *
 * ARGUMENTS:
 *
 * env[IN]: JNI handle.
 * type[IN]: event type.
 * eventTime[IN]: event time.
 * jobStatus2Log[IN]: struct jobStatus2Log pointer.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * create hashmap object array according execHosts field in struct eventRec.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap array object on success, NULL on failure.
 *
 *-----------------------------------------------------------------------
 */
static Json4c *getJobRunTimeHostsHashmapArray(char *eventType, time_t eventTime,
		struct jobStatus2Log *jobStatus2Log) {
	int i, j;
	long lsfArrayIdx;
	long lsfJobId;
	int numExecProcessors = 0;
	/* Job array index and job id */
	lsfArrayIdx = LSB_ARRAY_IDX(jobStatus2Log->jobId);
	lsfJobId = LSB_ARRAY_JOBID(jobStatus2Log->jobId);
	/* create new array instance. */
	Json4c *execHostsArray = NULL;
	Json4c *objHost = NULL;
	char *time;
	/*Number of exec processors*/
	numExecProcessors = getNumExecProc(jobStatus2Log->numExHosts, jobStatus2Log->slotUsages);

	/* put dynamic field to hashmap. */
	for (i = 0; i < jobStatus2Log->numExHosts; i++) {
		long gmt;
		long expGmt;
		Json4c *objHost = NULL;
		if (NULL == objHost) {
//			throw_exception_by_key(env, logger, "perf.lsf.events.nullObject", NULL);
			return NULL;
		}
		addStringToObject(objHost, FIELD_HOST_NAME, jobStatus2Log->execHosts[i]);

		addNumberToObject(objHost, FIELD_EXECHOST_SLOT_NUM, jobStatus2Log->slotUsages[i]);

		if (jobStatus2Log->numhRusages > 0) {
			for (j = 0; j < jobStatus2Log->numhRusages; j++) {
				if (jobStatus2Log->execHosts[i] != NULL
						&& jobStatus2Log->hostRusage[j].name != NULL
						&& 0
								== strcmp(jobStatus2Log->execHosts[i],
										jobStatus2Log->hostRusage[j].name)) {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
											jobStatus2Log->hostRusage[j].mem);

					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
												jobStatus2Log->hostRusage[j].swap);

					addNumberToObject(objHost, FIELD_RU_UTIME,
							jobStatus2Log->hostRusage[j].utime);

					addNumberToObject(objHost, FIELD_RU_STIME,
							jobStatus2Log->hostRusage[j].stime);

					addNumberToObject(objHost, FIELD_CPU_TIME,
							jobStatus2Log->hostRusage[j].utime
									+ jobStatus2Log->hostRusage[j].stime);
					break;
				}
			}
		} else {
			if (numExecProcessors == 0) {
				addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE, 0);

				addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE, 0);

				addNumberToObject(objHost, FIELD_RU_UTIME, 0);
				addNumberToObject(objHost, FIELD_RU_STIME, 0);

				addNumberToObject(objHost, FIELD_CPU_TIME, 0);
			} else {
				double precent = (double) jobStatus2Log->slotUsages[i]
						/ numExecProcessors;
				if (jobStatus2Log->lsfRusage.ru_maxrss < 0) {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
											jobStatus2Log->lsfRusage.ru_maxrss);

				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_MEM_USAGE,
												jobStatus2Log->lsfRusage.ru_maxrss * precent);
				}
				if (jobStatus2Log->lsfRusage.ru_nswap < 0) {
					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
											jobStatus2Log->lsfRusage.ru_nswap);
				} else {
					addNumberToObject(objHost, FIELD_EXECHOST_SWAP_USAGE,
												jobStatus2Log->lsfRusage.ru_nswap * precent);
				}

				double utime = 0;
				double stime = 0;
				if (jobStatus2Log->lsfRusage.ru_utime < 0) {
					addNumberToObject(objHost, FIELD_RU_UTIME,
							jobStatus2Log->lsfRusage.ru_utime);
					utime = jobStatus2Log->lsfRusage.ru_utime;
				} else {
					addNumberToObject(objHost, FIELD_RU_UTIME,
							jobStatus2Log->lsfRusage.ru_utime * precent);
					utime = jobStatus2Log->lsfRusage.ru_utime * precent;
				}
				if (jobStatus2Log->lsfRusage.ru_stime < 0) {
					addNumberToObject(objHost, FIELD_RU_STIME,
							jobStatus2Log->lsfRusage.ru_stime);
					stime = jobStatus2Log->lsfRusage.ru_stime;
				} else {
					addNumberToObject(objHost, FIELD_RU_STIME,
							jobStatus2Log->lsfRusage.ru_stime * precent);
					stime = jobStatus2Log->lsfRusage.ru_stime * precent;
				}

				addNumberToObject(objHost, FIELD_CPU_TIME, utime + stime);
			}
		}
		addInstanceToArray(execHostsArray, objHost);
	}

	return execHostsArray;
}

/*
 *-----------------------------------------------------------------------
 *
 * readlsbStream
 *
 * ARGUMENTS:
 *
 * record[IN]: event data string.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * parse record string and return hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap object on success, NULL on failue.
 *
 *-----------------------------------------------------------------------
 */
char *readlsbStream(char *record) {
	// char *recstr = NULL;
	char *ret;
	struct eventRec *logrec = NULL;

	Json4c *objHeadHashmap, *objRangeHashmap;

	int i, iRet;
	Json4c *askedHostsArray, *execHostsArray, *reserHostsArray, *hRusagesArray;

	char eventType[1024] = { '\0' };
	char *p = NULL;
	char *msg = NULL;


	/* init egostream library. */
	iRet = initstream(&msg);
	if (0 != iRet) {
		if (msg == NULL) {
			msg = "Unknown";
		}
		TRACE("init error\n");
		return NULL;
	}

	/* convert jstring to char*. */
	if (record == NULL) {
		return NULL;
	}

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	logrec = (*stream.lsb_readstreamlineMT)(record);
#else
	/* invoke LSF parse function. */
	logrec = (*stream.lsb_readstreamline)(record);
#endif

	if (logrec == NULL) {
		TRACE("read error\n");
		return NULL;
	}

	/* backup the original record to get event type string. */
	strncpy(eventType, record, 1023);
	eventType[1023] = '\0';
	p = strchr(eventType, ' ');
	if (NULL == p) {
		goto end;
	}
	*p = '\0';
	/* remove ". */
	p = strrchr(eventType, '\"');
	if (NULL == p) {
		goto end;
	}
	*p = '\0';
	p = strchr(eventType, '\"');
	if (NULL == p) {
		goto end;
	}

	objHeadHashmap = jCreateObject();

	TRACE("stream file record type: %d\n", logrec->type);
	/* handle each event type. */
	switch (logrec->type) {

	case EVENT_JOB_RUN_RUSAGE:
		/* put event type string to hashmap. */
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putRunRusage(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_NEW:
		TRACE("JOB_NEW\n");
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobNew(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_START:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStart(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobStartLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobStartLog.jobId,
					logrec->eventLog.jobStartLog.numExHosts,
					logrec->eventLog.jobStartLog.execHosts,
					logrec->eventLog.jobStartLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
	case EVENT_JOB_STATUS:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStatus(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SWITCH:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobSwitch(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_MOVE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobMove(objHeadHashmap, logrec);
		break;
	case EVENT_MBD_UNFULFILL:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putUnfulfillLog(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FINISH:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobFinish(objHeadHashmap, logrec);
		if (logrec->eventLog.jobFinishLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					logrec->eventLog.jobFinishLog.submitTime,
					logrec->eventLog.jobFinishLog.jobId,
					logrec->eventLog.jobFinishLog.numExHosts,
					logrec->eventLog.jobFinishLog.execHosts,
					logrec->eventLog.jobFinishLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
#if defined(LSF8) || defined(LSF9) || defined(LSF10)
		case EVENT_JOB_FINISH2:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));


		putJobFinish2(objHeadHashmap, logrec);
		if (logrec->eventLog.jobFinish2Log.numExHosts > 0) {
			execHostsArray = getJobExecHostsHashmapArray(
					p + 1, logrec->eventTime, &(logrec->eventLog.jobFinish2Log));
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}

		break;
		case EVENT_JOB_STARTLIMIT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStartLimit(objHeadHashmap, logrec);
		break;
#endif
	case EVENT_MIG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putMig(objHeadHashmap, logrec);
		break;
	case EVENT_PRE_EXEC_START:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStart(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobStartLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobStartLog.jobId,
					logrec->eventLog.jobStartLog.numExHosts,
					logrec->eventLog.jobStartLog.execHosts,
					logrec->eventLog.jobStartLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
		/*********** hard code the: since hte JobModlog struct have no idx , so hard
		 * code is :0*/
	case EVENT_JOB_MODIFY2:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobModify2(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SIGNAL:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));
		putJobSignal(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FORWARD:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobForward(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_ACCEPT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobAccept(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_START_ACCEPT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStartAccept(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SIGACT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobSigact(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXECUTE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExecute(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_REQUEUE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobRequeue(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_CLEAN:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobClean(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXCEPTION:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobException(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXT_MSG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExtMsg(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_ATTA_DATA:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExtMsg(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_CHUNK:
		putJobChunk(objHeadHashmap, logrec);
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobChunkLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobChunkLog.membJobId[0],
					logrec->eventLog.jobChunkLog.numExHosts,
					logrec->eventLog.jobChunkLog.execHosts, 0);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
	case EVENT_SBD_UNREPORTED_STATUS:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putSBDUnreportedStatus(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FORCE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobForce(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime, 0,
				logrec->eventLog.jobForceRequestLog.jobId,
				logrec->eventLog.jobForceRequestLog.numExecHosts,
				logrec->eventLog.jobForceRequestLog.execHosts,
				logrec->eventLog.jobForceRequestLog.idx);
		addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		break;
#if !defined(LSF6)
	case EVENT_METRIC_LOG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putMetricLog(objHeadHashmap, logrec);
		break;
#endif
	default:
		TRACE("unknown event type\n");
		return NULL;
	}
	/* add clusterName */
	addStringToObject(objHeadHashmap, FIELD_CLUSTER_NAME, ls_getclustername());

	end:
	/* relase memory. */
// (*env)->ReleaseStringUTFChars(env, record, recstr);
#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	(*stream.lsb_freelogrec)(logrec);
	logrec = NULL;
#endif

	ret = jToString(objHeadHashmap);
	jFree(objHeadHashmap);

	return ret;
	// return objHeadHashmap;
}

/*
 *-----------------------------------------------------------------------
 *
 * readlsbEvents
 *
 * ARGUMENTS:
 *
 * record[IN]: event data string.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * parse record string and return hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap object on success, NULL on failue.
 *
 *-----------------------------------------------------------------------
 */
char *readlsbEvents(char *record) {
	// char *recstr = NULL;
	char *ret;
	struct eventRec *logrec = NULL;

	Json4c *objHeadHashmap, *objRangeHashmap;

	int i, iRet;
	Json4c *askedHostsArray, *execHostsArray, *reserHostsArray, *hRusagesArray;

	char eventType[1024] = { '\0' };
	char *p = NULL;
	char *msg = NULL;

	/* init egostream library. */
	iRet = initstream(&msg);
	if (0 != iRet) {
		if (msg == NULL) {
			msg = "Unknown";
		}
		return NULL;
	}

	/* convert jstring to char*. */
	if (record == NULL) {
		return NULL;
	}


	/* invoke LSF parse function. */
	logrec = (struct eventRec *) calloc(1, sizeof(struct eventRec));
	if (logrec == NULL) {
		return NULL;
	}

	iRet = lsb_geteventrecbyline(record, logrec);

	if (iRet == -1) {
		return NULL;
	}
	/*#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	 logrec = (*stream.lsb_readstreamlineMT)(record);
	 #else
	 logrec = (*stream.lsb_readstreamline)(record);
	 #endif
	 if (logrec == NULL) {
	 return NULL;
	 }*/

	/* backup the original record to get event type string. */
	strncpy(eventType, record, 1023);
	eventType[1023] = '\0';
	p = strchr(eventType, ' ');
	if (NULL == p) {
		goto end;
	}
	*p = '\0';
	/* remove ". */
	p = strrchr(eventType, '\"');
	if (NULL == p) {
		goto end;
	}
	*p = '\0';
	p = strchr(eventType, '\"');
	if (NULL == p) {
		goto end;
	}

	objHeadHashmap = jCreateObject();

	TRACE("event file record type: %d\n", logrec->type);

	/* handle each event type. */
	switch (logrec->type) {

	case EVENT_JOB_RUN_RUSAGE:
		/* put event type string to hashmap. */
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putRunRusage(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_NEW:
		TRACE("JOB_NEW\n");
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobNew(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_START:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStart(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobStartLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobStartLog.jobId,
					logrec->eventLog.jobStartLog.numExHosts,
					logrec->eventLog.jobStartLog.execHosts,
					logrec->eventLog.jobStartLog.idx);

			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
	case EVENT_JOB_STATUS:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStatus(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SWITCH:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobSwitch(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_MOVE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobMove(objHeadHashmap, logrec);
		break;
	case EVENT_MBD_UNFULFILL:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putUnfulfillLog(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FINISH:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobFinish(objHeadHashmap, logrec);
		if (logrec->eventLog.jobFinishLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					logrec->eventLog.jobFinishLog.submitTime,
					logrec->eventLog.jobFinishLog.jobId,
					logrec->eventLog.jobFinishLog.numExHosts,
					logrec->eventLog.jobFinishLog.execHosts,
					logrec->eventLog.jobFinishLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
#if defined(LSF8) || defined(LSF9) || defined(LSF10)
		case EVENT_JOB_FINISH2:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobFinish2(objHeadHashmap, logrec);
		if (logrec->eventLog.jobFinish2Log.numExHosts > 0) {
			execHostsArray = getJobExecHostsHashmapArray(
					p + 1, logrec->eventTime, &(logrec->eventLog.jobFinish2Log));
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS,execHostsArray);
		}

		break;
		case EVENT_JOB_STARTLIMIT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStartLimit(objHeadHashmap, logrec);
		break;
#endif
	case EVENT_MIG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putMig(objHeadHashmap, logrec);
		break;
	case EVENT_PRE_EXEC_START:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStart(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobStartLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobStartLog.jobId,
					logrec->eventLog.jobStartLog.numExHosts,
					logrec->eventLog.jobStartLog.execHosts,
					logrec->eventLog.jobStartLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
		/*********** hard code the: since hte JobModlog struct have no idx , so hard
		 * code is :0*/
	case EVENT_JOB_MODIFY2:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobModify2(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SIGNAL:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobSignal(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FORWARD:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobForward(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_ACCEPT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobAccept(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_START_ACCEPT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobStartAccept(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_SIGACT:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobSigact(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXECUTE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExecute(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_REQUEUE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobRequeue(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_CLEAN:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobClean(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXCEPTION:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobException(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_EXT_MSG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExtMsg(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_ATTA_DATA:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobExtMsg(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_CHUNK:
		putJobChunk(objHeadHashmap, logrec);
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		/* handle execHosts sub-table. */
		if (logrec->eventLog.jobChunkLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					0, logrec->eventLog.jobChunkLog.membJobId[0],
					logrec->eventLog.jobChunkLog.numExHosts,
					logrec->eventLog.jobChunkLog.execHosts, 0);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
	case EVENT_SBD_UNREPORTED_STATUS:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putSBDUnreportedStatus(objHeadHashmap, logrec);
		break;
	case EVENT_JOB_FORCE:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobForce(objHeadHashmap, logrec);
		/* handle execHosts sub-table. */
		execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime, 0,
				logrec->eventLog.jobForceRequestLog.jobId,
				logrec->eventLog.jobForceRequestLog.numExecHosts,
				logrec->eventLog.jobForceRequestLog.execHosts,
				logrec->eventLog.jobForceRequestLog.idx);
		addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		break;
#if !defined(LSF6)
	case EVENT_METRIC_LOG:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putMetricLog(objHeadHashmap, logrec);
		break;
#endif
	default:
		return NULL;
	}
	/* add clusterName */
	addStringToObject(objHeadHashmap, FIELD_CLUSTER_NAME, ls_getclustername());

	end:
	/* relase memory. */

	ret = jToString(objHeadHashmap);
	jFree(objHeadHashmap);

#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	if(logrec != NULL) {
		//(*stream.lsb_freelogrec)(logrec);
		/* relase memory. */
		FREEUP(logrec);
		logrec = NULL;
	}
#endif
	return ret;
	// return objHeadHashmap;
}

char *readlsbAcct(char *record) {
	char *ret;
	struct eventRec *logrec = NULL;

	Json4c *objHeadHashmap;
	int iRet;
	Json4c *execHostsArray;
	char eventType[1024] = { '\0' };
	char *p = NULL;
	char *msg = NULL;
	/* init egostream library. */
	iRet = initstream(&msg);
	if (0 != iRet) {
		if (msg == NULL) {
			msg = "Unknown";
		}
		return NULL;
	}
	/* convert jstring to char*. */
	if (record == NULL) {
		return NULL;
	}
	/* invoke LSF parse function. */
	logrec = (struct eventRec *) calloc(1, sizeof(struct eventRec));
	if (logrec == NULL) {
		return NULL;
	}
	iRet = lsb_geteventrecbyline(record, logrec);
	if (iRet == -1) {
		return NULL;
	}
	/* backup the original record to get event type string. */
	strncpy(eventType, record, 1023);
	eventType[1023] = '\0';
	p = strchr(eventType, ' ');
	if (NULL == p) {
		return NULL;
	}
	*p = '\0';
	/* remove ". */
	p = strrchr(eventType, '\"');
	if (NULL == p) {
		return NULL;
	}
	*p = '\0';
	p = strchr(eventType, '\"');
	if (NULL == p) {
		return NULL;
	}

	objHeadHashmap = jCreateObject();

	/* handle each event type. */
	switch (logrec->type) {
	case EVENT_JOB_FINISH:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		putJobFinish(objHeadHashmap, logrec);
		if (logrec->eventLog.jobFinishLog.numExHosts > 0) {
			execHostsArray = getExecHostsHashmapArray(p + 1, logrec->eventTime,
					logrec->eventLog.jobFinishLog.submitTime,
					logrec->eventLog.jobFinishLog.jobId,
					logrec->eventLog.jobFinishLog.numExHosts,
					logrec->eventLog.jobFinishLog.execHosts,
					logrec->eventLog.jobFinishLog.idx);
			addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
		}
		break;
	default:
		return NULL;
	}
	/* add clusterName */
	addStringToObject(objHeadHashmap, FIELD_CLUSTER_NAME, ls_getclustername());

	ret = jToString(objHeadHashmap);
	jFree(objHeadHashmap);

	return ret;
}

/*
 *-----------------------------------------------------------------------
 *
 * readlsbStatus
 *
 * ARGUMENTS:
 *
 * record[IN]: event data string.
 *
 * PRE-CONDITION:
 *
 * NULL.
 *
 * DESCRIPTION:
 *
 * parse record string and return hashmap object.
 *
 * SIDE_EFFECTS:
 *
 * NULL.
 *
 * RETURN:
 *
 * hashmap object on success, NULL on failue.
 *
 *-----------------------------------------------------------------------
 */
char *readlsbStatus(char *record) {
	char *ret;
	struct eventRec *logrec = NULL;
	Json4c *objHeadHashmap;
	int iRet;
	Json4c *execHostsArray;
	char eventType[1024] = { '\0' };
	char *p = NULL;
	char *msg = NULL;

#if defined(LSF8) || defined(LSF9) || defined(LSF10)


	/* init egostream library. */
	iRet = initstream(&msg);
	if (0 != iRet) {
		if (msg == NULL) {
			msg = "Unknown";
		}
		return NULL;
	}
	/* convert jstring to char*. */
	if (record == NULL) {
		return NULL;
	}
	/* invoke LSF parse function. */
#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	logrec = (*stream.lsb_readstreamlineMT)(record);
#else
	/* invoke LSF parse function. */
	logrec = (*stream.lsb_readstreamline)(record);
#endif

	if (logrec == NULL) {
		return NULL;
	}
	/* backup the original record to get event type string. */
	strncpy(eventType, record, 1023);
	eventType[1023] = '\0';
	p = strchr(eventType, ' ');
	if (NULL == p) {
		return NULL;
	}
	*p = '\0';
	/* remove ". */
	p = strrchr(eventType, '\"');
	if (NULL == p) {
		return NULL;
	}
	*p = '\0';
	p = strchr(eventType, '\"');
	if (NULL == p) {
		return NULL;
	}
	/* create hashmap object. */
	objHeadHashmap = jCreateObject();

	/* handle each event type. */
	switch (logrec->type) {
		case EVENT_JOB_STATUS2:
		addStringToObject(objHeadHashmap, FIELD_EVENT_TYPE, (p + 1));

		if (logrec->eventLog.jobStatus2Log.jobId > 0) {
			putJobStatus2(objHeadHashmap, logrec);
			if (logrec->eventLog.jobStatus2Log.numExHosts > 0) {
				execHostsArray = getJobRunTimeHostsHashmapArray(p + 1,
						logrec->eventTime, &(logrec->eventLog.jobStatus2Log));
				addInstanceToObject(objHeadHashmap, FIELD_EXEC_HOSTS, execHostsArray);
			}
		} else if (logrec->eventLog.jobStatus2Log.jobId == 0) {
			putJobStatus2Pend(objHeadHashmap, logrec);
		}

		break;
		default:
		return NULL;
	}
#if defined(LSB_EVENT_VERSION9_1) || defined(LSB_EVENT_VERSION10_1)
	(*stream.lsb_freelogrec)(logrec);
	logrec = NULL;
#endif

	ret = jToString(objHeadHashmap);
	jFree(objHeadHashmap);

	return ret;
#else
	return NULL;
#endif

}
