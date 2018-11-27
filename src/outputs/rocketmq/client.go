package rocketmq

import (
	"errors"
	"fmt"
	"strings"

	"github.com/elastic/beats/libbeat/logp"
	"github.com/elastic/beats/libbeat/outputs"
	"github.com/elastic/beats/libbeat/outputs/codec"
	"github.com/elastic/beats/libbeat/publisher"
	"github.com/sevenNt/rocketmq"
)

const (
	producerGroup = "lsfmq"
	instanceName  = "lsfmq.rocketmq"
)

type client struct {
	observer outputs.Observer
	config   *rocketmqConfig
	index    string
	codec    codec.Codec

	// rocketmq producer
	producer rocketmq.Producer
}

func newRocketmqClient(
	observer outputs.Observer,
	cfg *rocketmqConfig,
	idx string,
	writer codec.Codec,
) (*client, error) {
	c := &client{
		observer: observer,
		config:   cfg,
		index:    idx,
		codec:    writer,
	}
	return c, nil
}

func (c *client) Connect() error {
	debugf("connect: %v", c.config.Hosts)

	// create producer instance
	conf := &rocketmq.Config{
		Namesrv:      strings.Join(c.config.Hosts, ";"),
		InstanceName: "lsfmq.rocketmq",
	}

	producer, err := rocketmq.NewDefaultProducer(producerGroup, conf)
	if err != nil {
		logp.Err("rocketmq", "Failed with creating producer. Error is [%v]", err)
		return err
	}

	c.producer = producer
	producer.Start()

	return nil
}

func (c *client) Close() error {
	debugf("close rocketmq client")
	c.producer.Shutdown()
	return nil
}

func (c *client) Publish(batch publisher.Batch) error {
	if c == nil {
		panic("no client")
	}

	if batch == nil {
		panic("no batch")
	}

	events := batch.Events()
	c.observer.NewBatch(len(events))

	failedEvents, err := c.publishEvents(events)

	if failedEvents != nil {
		c.observer.Failed(len(failedEvents))
		batch.RetryEvents(failedEvents)
		return err
	}

	batch.ACK()
	return err
}

// publishEvents returns nil if there is no error
// or failed events if there are failed ones
func (c *client) publishEvents(events []publisher.Event) ([]publisher.Event, error) {
	isErr := false
	var failed []publisher.Event = nil

	for _, event := range events {
		msg := c.getMessage(event)
		if msg == nil {
			isErr = true
			failed = append(failed, event)
		} else {
			_, err := c.producer.Send(msg)
			if err != nil {
				isErr = true
				failed = append(failed, event)
			}
		}
	}

	if isErr {
		return failed, errors.New("Fail to send data to rocketmq")
	} else {
		return nil, nil
	}
}

func (c *client) getMessage(event publisher.Event) *rocketmq.Message {
	data := event.Content
	if data.Meta == nil {
		logp.Err("rocketmq", "No specified topic for %v", data.Fields)
		return nil
	}

	topicV, ok := data.Meta["topic"]
	if !ok {
		logp.Err("rocketmq", "No specified topic for %v", data.Fields)
		return nil
	}

	topic := fmt.Sprintf("%v", topicV)

	serializedEvent, err := c.codec.Encode(c.index, &data)
	if err != nil {
		logp.Err("rocketmq", "Error [%v] occured during event serializing", err)
		return nil
	}

	buf := make([]byte, len(serializedEvent))
	copy(buf, serializedEvent)

	msg := rocketmq.NewMessage(topic, buf)

	// data.Meta["properties"] should be map[string]string
	props, ok := data.Meta["properties"]
	if ok {
		msg.Properties = props.(map[string]string)
	}

	return msg
}

func (c *client) String() string {
	return "rocketmq(" + strings.Join(c.config.Hosts, ",") + ")"
}
