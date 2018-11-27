package unsafemap

import (
	"encoding/json"
	"io/ioutil"
	"strings"

	"github.com/elastic/beats/libbeat/logp"
)

type UnsafeMap struct {
	Map      map[string]interface{}
	Modified bool
}

func NewUnsafeMap(size int, file string) *UnsafeMap {
	usm := new(UnsafeMap)
	usm.Map = make(map[string]interface{}, size)
	usm.Modified = false
	if len(file) > 0 {
		usm.LoadFile(file)
	}
	return usm
}

func (usm *UnsafeMap) ReadMap(key string) (interface{}, bool) {
	value, ok := usm.Map[key]
	return value, ok
}

func (usm *UnsafeMap) DelMap(key string) {
	delete(usm.Map, key)
	usm.Modified = true
}

func (usm *UnsafeMap) WriteMap(key string, value interface{}) {
	usm.Map[key] = value
	usm.Modified = true
}

func (usm *UnsafeMap) SyncFile(f string) {
	str := jsonToString(usm.Map)
	writeStringToFile(str, f)
}

func (usm *UnsafeMap) LoadFile(f string) {
	jsonStr := readStringFromFile(f)
	tmpMap := stringToJson(&jsonStr)
	if tmpMap != nil {
		usm.Map = tmpMap
	}
}

func writeStringToFile(s, f string) {
	data := []byte(s)
	if err := ioutil.WriteFile(f, data, 0777); err != nil {
		logp.Err("Fail writing data to file %s, error is %s", f, err.Error())
	}
}

func (usm *UnsafeMap) GetModifiedFlag() bool {
	return usm.Modified
}

func (usm *UnsafeMap) SetModified() {
	usm.Modified = true
}

func (usm *UnsafeMap) UnsetModified() {
	usm.Modified = false
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
