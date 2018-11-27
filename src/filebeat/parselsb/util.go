package parselsb

import (
	"fmt"
)

func getString(m map[string]interface{}, key string) string {
	val, ok := m[key]
	if ok {
		return fmt.Sprintf("%v", val)
	} else {
		return ""
	}
}

func getInt(m map[string]interface{}, key string) int {
	val, ok := m[key]
	if ok {
		if intVal, isInt := val.(int); isInt {
			return intVal
		} else if floatVal, isFloat := val.(float64); isFloat {
			return int(floatVal)
		} else if floatVal, isFloat := val.(float32); isFloat {
			return int(floatVal)
		} else {
			return -1
		}
	} else {
		return -1
	}
}
