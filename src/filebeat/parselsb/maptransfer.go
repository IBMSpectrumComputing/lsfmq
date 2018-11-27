package parselsb

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/elastic/beats/filebeat/parselsb/safemap"
	"github.com/elastic/beats/libbeat/logp"
	"github.com/robfig/cron"
)

const (
	MapSize       = 1024
	SyncInterval  = 1
	ClusterKey    = "cluster_name"
	JobIdKey      = "job_id"
	JobIdxKey     = "job_arr_idx"
	LastStatusKey = "last_status"
	CurStatusKey  = "current_status"
	ChangeReason  = "change_reason"
)

var extraFields = []string{"user_name", "queue_name", "job_name", "project_name", "user_group_name", "job_group", "app_profile"}

type mapTransfer struct {
	bakFile string
	mmap    *safemap.SafeMap
	c       *cron.Cron
	pmap    *safemap.SafeMap
}

func newMapTransfer() *mapTransfer {
	path := getFilePath()
	mt := new(mapTransfer)
	mt.bakFile = path
	mt.mmap = safemap.NewSafeMap(MapSize, path)
	mt.pmap = safemap.NewSafeMap(MapSize, "")
	return mt
}

func (t *mapTransfer) registerSyncTask() {
	t.c = cron.New()
	spec := "*/1 * * * * ?"

	t.c.AddFunc(spec, func() {
		if flg := t.mmap.GetModifiedFlag(); flg {
			logp.Info("Run sync task at %v", time.Now())
			t.mmap.UnsetModified()
			t.mmap.SyncFile(t.bakFile)
		} else {
			logp.Info("Job state snapshot is not changed at %v", time.Now())
		}
	})

	t.c.Start()
	logp.Info("Register sync task by %v\n", spec)
}

func (t *mapTransfer) stopSyncTask() {
	if t.c != nil {
		t.mmap.SyncFile(t.bakFile)
		logp.Info("Stop sync task at %v\n", time.Now())
		t.c.Stop()
	} else {
		logp.Info("There is no active cron job\n")
	}
}

// ProcessJobEvent processes raw job event and generate job status info if possible
func (t *mapTransfer) processJobEvent(event map[string]interface{}, topic *Topic) *MessageWithTopic {

	// record job properties from JOB_NEW event
	t.recordJobProperty(event)

	var ret *MessageWithTopic = nil

	state := getState(event)
	if state == nil {
		return nil
	}

	uid := getUid(event)

	var prop map[string]string

	propRaw, propOK := t.pmap.ReadMap(uid)
	if propOK {
		prop = propRaw.(map[string]string)
	}

	val, ok := t.mmap.ReadMap(uid)
	if ok == false {
		if state.state < 6 {
			t.mmap.WriteMap(uid, state.state)
		}
		sm := new(stateMessage)
		sm.cluster = getString(event, ClusterKey)
		sm.jobId = getInt(event, JobIdKey)
		sm.jobIdx = getInt(event, JobIdxKey)
		sm.lastState = -1
		sm.currentState = state.state
		sm.reason = state.reason
		ret = stateToMessageWithTopic(sm, event, topic, prop)
	} else {
		var stateVal = -1
		if intVal, isInt := val.(int); isInt {
			stateVal = intVal
		} else if floatVal, isFloat := val.(float64); isFloat {
			stateVal = int(floatVal)
		} else if floatVal, isFloat := val.(float32); isFloat {
			stateVal = int(floatVal)
		}

		if state.state != stateVal {
			t.mmap.WriteMap(uid, state.state)
			sm := new(stateMessage)
			sm.cluster = getString(event, ClusterKey)
			sm.jobId = getInt(event, JobIdKey)
			sm.jobIdx = getInt(event, JobIdxKey)
			sm.lastState = stateVal
			sm.currentState = state.state
			sm.reason = state.reason
			ret = stateToMessageWithTopic(sm, event, topic, prop)

			// delete finished job from status snapshot
			if state.state >= 6 {
				t.mmap.DelMap(uid)
				t.pmap.DelMap(uid)
			}
		}
	}

	return ret
}

func (t *mapTransfer) recordJobProperty(event map[string]interface{}) {
	eventType := getString(event, "event_type")
	if eventType == "JOB_NEW" {
		uid := getUid(event)
		prop := make(map[string]string)
		for _, fld := range extraFields {
			prop[fld] = getString(event, fld)
		}
		t.pmap.WriteMap(uid, prop)
	}
}

func stateToMessageWithTopic(sm *stateMessage, event map[string]interface{}, topic *Topic, prop map[string]string) *MessageWithTopic {
	topicName := topic.TopicName
	m := make(map[string]interface{})
	m[ClusterKey] = sm.cluster
	m[JobIdKey] = sm.jobId
	m[JobIdxKey] = sm.jobIdx
	if sm.lastState > 0 {
		m[LastStatusKey] = getStateName(sm.lastState)
	}
	m[CurStatusKey] = getStateName(sm.currentState)
	m[ChangeReason] = sm.reason

	// add user specified fields
	if topic.IncludeFields != nil && len(topic.IncludeFields) > 0 {
		// add user specified fields
		for _, fld := range topic.IncludeFields {
			if v, ok := event[fld]; ok {
				m[fld] = v
			} else if topic.AddFields != nil {
				if addedVal, addedOK := topic.AddFields[fld]; addedOK {
					m[fld] = addedVal
				}
			} else {
				// skip the invalid field name
			}
		}
	}

	txt := jsonToString(m)

	// add job property from JOB_NEW event
	if prop != nil {
		for _, efld := range extraFields {
			m[efld] = prop[efld]
		}
	}

	return &MessageWithTopic{
		Text:       txt,
		Topic:      topicName,
		RoutingKey: getRoutingKey(m, topic),
		Props:      getProperties(m, topic),
	}
}

func getUid(event map[string]interface{}) string {
	cluster := getString(event, ClusterKey)
	id := getInt(event, JobIdKey)
	idx := getInt(event, JobIdxKey)
	uid := fmt.Sprintf("%v_%v_%v", cluster, id, idx)
	return uid
}

func getFilePath() string {
	var prefix string = ""

	BAK_PATH_KEY := "LSF_BAK_PATH"
	BAK_FILE_NAME := "job.states.snapshot"
	bakPath := os.Getenv(BAK_PATH_KEY)

	if len(bakPath) > 0 {
		if strings.HasSuffix(bakPath, "/") == false {
			prefix = bakPath + "/"
		} else {
			prefix = bakPath
		}
	} else {
		path, _ := exec.LookPath(os.Args[0])
		if idx := strings.LastIndex(path, "/"); idx >= 0 {
			prefix = string(path[0:(idx + 1)])
		}
	}
	return prefix + BAK_FILE_NAME
}
