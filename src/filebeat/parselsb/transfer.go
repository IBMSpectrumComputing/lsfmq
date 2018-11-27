package parselsb

type StateHandler interface {
	processJobEvent(event map[string]interface{}, topic *Topic) *MessageWithTopic
	registerSyncTask()
	stopSyncTask()
}
