package rabbitmq

import (
	"errors"
	"fmt"
	"strings"
	"sync"

	"github.com/elastic/beats/libbeat/logp"
	"github.com/elastic/beats/libbeat/outputs"
	"github.com/elastic/beats/libbeat/outputs/codec"
	"github.com/elastic/beats/libbeat/publisher"
	"github.com/streadway/amqp"
)

type client struct {
	observer outputs.Observer
	config   *rabbitmqConfig
	index    string
	codec    codec.Codec

	// amqp connection
	conn *amqp.Connection

	// channel for publishing events
	ch *amqp.Channel

	wg sync.WaitGroup
}

var (
	errNoDeclaredExchange = errors.New("no exchange could be selected")
	exchangeMap           = make(map[string]bool)
)

func newRabbitmqClient(
	observer outputs.Observer,
	cfg *rabbitmqConfig,
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

	// select the first host as target host
	// TODO - backoff based on RR algorithm
	url, err := getRabbitmqURL(c.config.Hosts[0], c.config.Username, c.config.Password, c.config.Vhost)

	if err != nil {
		logp.Err("rabbitmq", "Failed to get rabbitmq url with error [%v]", err)
		return err
	}

	// establish amqp connection
	conn, err := amqp.Dial(url)
	if err != nil {
		logp.Err("rabbitmq", "Failed to connect to RabbitMQ [%v] with %v", url, err)
		return err
	}
	c.conn = conn

	// open a channel
	ch, err := conn.Channel()
	if err != nil {
		logp.Err("rabbitmq", "Failed to open a channel")
		return err
	}
	c.ch = ch

	// declare exchanges according to config
	for _, exchange := range c.config.Exchanges {
		err := ch.ExchangeDeclare(
			exchange, // name
			"topic",  // type
			true,     // durable
			false,    // auto-deleted
			false,    // internal
			false,    // no-wait
			nil,      // arguments
		)
		if err != nil {
			logp.Err("rabbitmq", "Failed to declare exchange [%v]", exchange)
			return err
		} else {
			exchangeMap[exchange] = true
		}
	}

	// register listeners for success/fail publish
	c.wg.Add(1)
	go c.errorWorker(ch.NotifyReturn(make(chan amqp.Return, 1)))

	return nil
}

func (c *client) Close() error {
	debugf("close rabbitmq client")
	c.ch.Close()
	c.conn.Close()
	c.wg.Wait()
	c.conn = nil
	c.ch = nil
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
		err := c.ch.Publish(
			c.getExchange(event),   // exchange
			c.getRoutingKey(event), // routing key
			false,                  // mandatory
			false,                  // immediate
			*c.getPublishing(event),
		)
		if err != nil {
			isErr = true
			failed = append(failed, event)
		}
	}

	if isErr {
		return failed, errNoDeclaredExchange
	} else {
		return nil, nil
	}
}

// TODO - generate routing key from event
func (c *client) getRoutingKey(event publisher.Event) string {
	data := event.Content
	if data.Meta == nil {
		logp.Err("rabbitmq", "No routing key for %v", data.Fields)
		return ""
	}

	routingV, ok := data.Meta["routing"]
	if !ok {
		logp.Err("rabbitmq", "No routing key for %v", data.Fields)
		return ""
	}

	routing := fmt.Sprintf("%v", routingV)

	return routing
}

func (c *client) getExchange(event publisher.Event) string {
	data := event.Content
	if data.Meta == nil {
		logp.Err("rabbitmq", "No specified exchange for %v", data.Fields)
		return ""
	}

	topicV, ok := data.Meta["topic"]
	if !ok {
		logp.Err("rabbitmq", "No specified exchange for %v", data.Fields)
		return ""
	}

	topic := fmt.Sprintf("%v", topicV)

	if _, ok := exchangeMap[topic]; !ok {
		// logp.Err("rabbitmq", "exchange [%v] is not declared", topic)
		// declare new exchange according to topic in metadata

		err := c.ch.ExchangeDeclare(
			topic,   // name
			"topic", // type
			true,    // durable
			false,   // auto-deleted
			false,   // internal
			false,   // no-wait
			nil,     // arguments
		)
		if err != nil {
			logp.Err("rabbitmq", "Failed to declare exchange [%v]", topic)
			return ""
		} else {
			exchangeMap[topic] = true
		}
	}

	return topic
}

func (c *client) getPublishing(event publisher.Event) *amqp.Publishing {
	data := event.Content

	serializedEvent, err := c.codec.Encode(c.index, &data)
	if err != nil {
		logp.Err("rabbitmq", "Error [%v] occured during event serializing", err)
		return nil
	}

	buf := make([]byte, len(serializedEvent))
	copy(buf, serializedEvent)

	mode := amqp.Persistent
	if !c.config.Persistent {
		mode = amqp.Transient
	}

	return &amqp.Publishing{
		ContentType:  "text/plain",
		Body:         buf,
		DeliveryMode: mode,
	}
}

func (c *client) String() string {
	return "rabbitmq(" + strings.Join(c.config.Hosts, ",") + ")"
}

func (c *client) successWorker(ch <-chan amqp.Confirmation) {
	defer c.wg.Done()
	defer debugf("stop rabbitmq ack worker")

	// TODO - deal with confirmation
}

func (c *client) errorWorker(ch <-chan amqp.Return) {
	defer c.wg.Done()
	defer debugf("stop rabbitmq error handler")

	for ret := range ch {
		// TODO - deal with publish error
		logp.Info("rabbitmq", "Failed publish record [%v] to exchange [%v] with [%v]", ret.Body, ret.Exchange, ret.ReplyText)
	}
}
