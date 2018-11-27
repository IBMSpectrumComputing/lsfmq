package parselsb

import (
	"encoding/json"

	"github.com/elastic/beats/libbeat/logp"
)

// stringToJson parses a json string to map object
func stringToJson(str *string) map[string]interface{} {
	var ret map[string]interface{}

	// replace "\" by "/" to avoid escaping issues
	//	escapedStr := strings.Replace(*str, "\\", "/", -1)

	if err := json.Unmarshal([]byte(*str), &ret); err == nil {
		return ret
	} else {
		logp.Err("Invalid json format: %s", *str)
		return nil
	}
}

func jsonToString(mjson map[string]interface{}) string {
	val, _ := json.Marshal(mjson)
	return string(val)
}

func filterByIncludeFields(json map[string]interface{}, includeFields []string) map[string]interface{} {

	if json == nil || len(includeFields) <= 0 {
		return json
	}

	var ret = make(map[string]interface{})

	for _, fld := range includeFields {
		if val, ok := json[fld]; ok {
			ret[fld] = val
		}
	}
	return ret
}

func filterByExcludeFields(json map[string]interface{}, excludeFields []string) map[string]interface{} {
	if json == nil || len(excludeFields) <= 0 {
		return json
	}

	for _, val := range excludeFields {
		if _, ok := json[val]; ok {
			delete(json, val)
		}
	}

	return json
}

func addFields(json map[string]interface{}, fields map[string]interface{}) map[string]interface{} {
	if json == nil || fields == nil {
		return json
	}

	for k, v := range fields {
		json[k] = v
	}

	return json
}

// selectFields filter the json string by the Topic options
func selectFields(json map[string]interface{}, topic *Topic) string {
	mjson := json
	if topic != nil {
		mjson = filterByIncludeFields(mjson, topic.IncludeFields)
		mjson = filterByExcludeFields(mjson, topic.ExcludeFields)
	}
	return jsonToString(mjson)
}
