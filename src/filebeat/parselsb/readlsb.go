package parselsb

/*
#include <stdlib.h>
#include <time.h>
#include "lsbevent_parse.h"

*/
import "C"

import (
	"fmt"
	"strings"
	"sync"
	"unsafe"

	"github.com/elastic/beats/libbeat/logp"
)

const (
	EventFile = iota
	StreamFile
	AcctFile
	StatusFile
)

// LsfRec contains lsf event related info
type LsfRec struct {
	RawContent string
	Type       int
	RetChan    chan []MessageWithTopic
	Topics     []Topic
}

// MessageWithTopic contains message content, related topic name and routing key
type MessageWithTopic struct {
	Text       string
	Topic      string
	RoutingKey string
	Props      map[string]string
}

type parser struct {
	counter int
	rawChan chan LsfRec
}

var singleton *parser
var once sync.Once
var sh StateHandler

// StopSyncTask stops job status snapshot sync task
func StopSyncTask() {
	sh.stopSyncTask()
}

// NewLsbParser creates a singleton parser instance
func NewLsbParser() *parser {
	once.Do(func() {
		// initiate a mapTransfer as StateHandler
		sh = newMapTransfer()
		// start the scheduled sync task
		sh.registerSyncTask()

		if singleton == nil {
			singleton = new(parser)
			singleton.counter = 0
			singleton.rawChan = make(chan LsfRec, 100)
			go func() { // listen for events on the raw channel
				for {
					rec := <-singleton.rawChan
					logp.Debug("lsf", "Parser received raw record %s", rec.RawContent)
					singleton.counter++
					var output *C.char
					switch rec.Type {
					case EventFile:
						output = C.readlsbEvents(C.CString(rec.RawContent))
						logp.Debug("Done parsing event %s, result is %s ", rec.RawContent, C.GoString(output))
					case StreamFile:
						output = C.readlsbStream(C.CString(rec.RawContent))
						logp.Debug("lsf", "Done parsing stream")
					case AcctFile:
						output = C.readlsbAcct(C.CString(rec.RawContent))
						logp.Debug("lsf", "Done parsing acct")
					case StatusFile:
						output = C.readlsbStatus(C.CString(rec.RawContent))
						logp.Debug("lsf", "Done parsing event")
					default:
						logp.Info("lsbparser - unknown type %s", rec.Type)
					}
					res := C.GoString(output)

					var msgs []MessageWithTopic
					// subsequent data processing
					mjsonRaw := stringToJson(&res)

					for _, tp := range rec.Topics {
						mjson := addFields(mjsonRaw, tp.AddFields)
						switch tp.Type {
						case "job.raw":
							// filter the fields by options
							res = selectFields(mjson, &tp)

							logp.Debug("lsf", "Parsed content: %s\n", res)
							msgs = append(msgs, MessageWithTopic{
								Text:       res,
								Topic:      tp.TopicName,
								RoutingKey: getRoutingKey(mjson, &tp),
								Props:      getProperties(mjson, &tp),
							})
						case "job.status.trace":
							// add job state message if needed
							newMsg := sh.processJobEvent(mjson, &tp)
							if newMsg != nil {
								logp.Debug("lsf", "Added content: %s\n", newMsg.Text)
								msgs = append(msgs, *newMsg)
							}
						default:
							logp.Err("lsf", "Unsupported topic type: %s\n", tp.Type)
						}
					}

					rec.RetChan <- msgs

					C.free(unsafe.Pointer(output))
				}
			}()
		}
	})
	return singleton
}

func (p *parser) Post(rec LsfRec) {
	logp.Debug("lsf", "Parser.Post()")
	p.rawChan <- rec
}

// getProperties generates rocketmq property map according to Topic.RoutingKeys
func getProperties(json map[string]interface{}, tp *Topic) map[string]string {
	if tp.RoutingKeys == nil || len(tp.RoutingKeys) <= 0 {
		return nil
	}

	propMap := make(map[string]string)

	for _, key := range tp.RoutingKeys {
		if val, ok := json[key]; ok {
			propMap[key] = fmt.Sprintf("%v", val)
		}
	}

	return propMap
}

// getRoutingKey generates rabbitmq routing key according to Topic.RoutingKeys
func getRoutingKey(json map[string]interface{}, tp *Topic) string {
	if tp.RoutingKeys == nil || len(tp.RoutingKeys) <= 0 {
		return ""
	}

	routingKey := ""

	for i, key := range tp.RoutingKeys {
		if val, ok := json[key]; ok {
			if i > 0 {
				routingKey = routingKey + "." + fmt.Sprintf("%v", val)
			} else {
				routingKey = fmt.Sprintf("%v", val)
			}
		} else {
			if i > 0 {
				routingKey = routingKey + "."
			} else {
				routingKey = ""
			}
		}
	}

	length := strings.Count(routingKey, "") - 1

	if length > 255 {
		logp.Warn("lsf", "routingKey [%v] is too long [%v]", routingKey, length)
		return ""
	} else {
		return routingKey
	}
}
