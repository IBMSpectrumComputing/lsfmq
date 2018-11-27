package safemap

import (
	"encoding/json"
	"io/ioutil"
	"strings"
	"sync"

	"github.com/elastic/beats/libbeat/logp"
)

type SafeMap struct {
	sync.RWMutex
	Map      map[string]interface{}
	Modified bool
}

func NewSafeMap(size int, file string) *SafeMap {
	sm := new(SafeMap)
	sm.Map = make(map[string]interface{}, size)
	sm.Modified = false
	if len(file) > 0 {
		sm.LoadFile(file)
	}
	return sm
}

func (sm *SafeMap) ReadMap(key string) (interface{}, bool) {
	sm.RLock()
	logp.Debug("safemap", "ReadMap gets r lock")
	value, ok := sm.Map[key]
	sm.RUnlock()
	logp.Debug("safemap", "ReadMap releases r lock")
	return value, ok
}

func (sm *SafeMap) DelMap(key string) {
	sm.Lock()
	logp.Debug("safemap", "DelMap gets lock")
	delete(sm.Map, key)
	sm.Modified = true
	sm.Unlock()
	logp.Debug("safemap", "DelMap releases lock")
}

func (sm *SafeMap) WriteMap(key string, value interface{}) {
	sm.Lock()
	logp.Debug("safemap", "WriteMap gets lock")
	sm.Map[key] = value
	sm.Modified = true
	sm.Unlock()
	logp.Debug("safemap", "WriteMap releases lock")
}

func (sm *SafeMap) SyncFile(f string) {
	sm.RLock()
	logp.Debug("safemap", "SyncFile gets r lock")
	defer sm.RUnlock()
	str := jsonToString(sm.Map)
	writeStringToFile(str, f)
}

func (sm *SafeMap) LoadFile(f string) {
	sm.Lock()
	logp.Debug("safemap", "LoadFile gets lock")
	defer sm.Unlock()
	jsonStr := readStringFromFile(f)
	tmpMap := stringToJson(&jsonStr)
	if tmpMap != nil {
		sm.Map = tmpMap
	}
}

func writeStringToFile(s, f string) {
	data := []byte(s)
	if err := ioutil.WriteFile(f, data, 0777); err != nil {
		logp.Err("Fail writing data to file %s, error is %s", f, err.Error())
	}
}

func (sm *SafeMap) GetModifiedFlag() bool {
	sm.RLock()
	logp.Debug("safemap", "GetModifiedFlag gets r lock")
	defer sm.RUnlock()
	return sm.Modified
}

func (sm *SafeMap) SetModified() {
	sm.Lock()
	logp.Debug("safemap", "SetModified gets lock")
	sm.Modified = true
	sm.Unlock()
}

func (sm *SafeMap) UnsetModified() {
	sm.Lock()
	logp.Debug("safemap", "UnsetModified gets lock")
	sm.Modified = false
	sm.Unlock()
	logp.Debug("safemap", "UnsetModified releases lock")
}

func readStringFromFile(f string) string {
	data, err := ioutil.ReadFile(f)
	if err != nil {
		logp.Err("Fail loading data from file %s, error is %s", f, err.Error())
		return ""
	}
	ret := strings.Replace(string(data), "\n", "", 1)
	return ret
}

// stringToJson parses a json string to map object
func stringToJson(str *string) map[string]interface{} {
	var ret map[string]interface{}
	if err := json.Unmarshal([]byte(*str), &ret); err == nil {
		return ret
	} else {
		logp.Err("Invalid json format: %s", str)
		return nil
	}
}

func jsonToString(mjson map[string]interface{}) string {
	val, _ := json.Marshal(mjson)
	return string(val)
}
