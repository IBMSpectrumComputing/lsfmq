#ifndef _lsbevent_parse_h_
#define _lsbevent_parse_h_

#ifdef __cplusplus
extern "C" {
#endif

char *readlsbStream(char *);

char *readlsbEvents(char *);

char *readlsbAcct(char *);

char *readlsbStatus(char *);

#ifdef __cplusplus
}
#endif

#define JOB_TYPE_BATCH 0
#define JOB_TYPE_INTERACTIVE 0x01
#define JOB_TYPE_PARALLEL 0x02

/* time fields (UNIX time in seconds) */
#define FIELD_BEGIN_TIME "begin_time"
#define FIELD_END_TIME "end_time"
#define FIELD_EVENT_TIME "event_time"
#define FIELD_FORWARD_TIME "forward_time"
#define FIELD_POST_TIME "post_time"
#define FIELD_START_TIME "start_time"
#define FIELD_SUBMIT_TIME "submit_time"
#define FIELD_TERM_TIME "term_time"

/* String time -- ISO formatted */
#define FIELD_BEGIN_TIME_STR "begin_time_str"
#define FIELD_END_TIME_STR "end_time_str"
#define FIELD_EVENT_TIME_STR "event_time_str"
#define FIELD_START_TIME_STR "start_time_str"
#define FIELD_SUBMIT_TIME_STR "submit_time_str"
#define FIELD_TERM_TIME_STR "term_time_str"

#define FIELD_AVG_MEM "avg_mem"
#define FIELD_MAX_MEM "max_mem"

/* Epoch (event time stamp as UNIX time) in seconds (possibly fractional) */

#define FIELD_EPOCH "epoch"

#define FIELD_EXEC_HOSTS "exec_hosts"
#define FIELD_EVENT_TYPE "event_type"

#define FIELD_JOB_TYPE "job_type"
#define FIELD_JOB_STATUS "job_status"
#define FIELD_JSTATUS "jstatus"

#define FIELD_MAX_RMEM "max_rmem"
#define FIELD_QUEUE "queue"
#define FIELD_RECORD_SRC "record_src"
#define FIELD_RES_REQ "res_req"
#define FIELD_STATUS "status"
#define FIELD_STATUS_STR "status_str"
#define FIELD_VERSION "version"
#define FIELD_EFFECTIVE_RES_REQ "effective_res_req"
#define FIELD_PROV_TIME "total_provision_time"
#define FIELD_EXEC_HOST_LIST_STR "exec_host_list_str"


#endif /* _lsbevent_parse_h_ */
