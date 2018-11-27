package rabbitmq

import (
	"errors"
	"fmt"

	"github.com/elastic/beats/libbeat/outputs/codec"
)

type rabbitmqConfig struct {
	Hosts       []string     `config:"hosts" validate:"required"`
	Username    string       `config:"username" validate:"required"`
	Password    string       `config:"password" validate:"required"`
	Vhost       string       `config:"vhost"`
	Exchanges   []string     `config:"exchanges"`
	Codec       codec.Config `config:"codec"`
	Persistent  bool         `config:"persistent"`
	BulkMaxSize int          `config:"bulk_max_size"`
	MaxRetries  int          `config:"max_retries"         validate:"min=-1,nonzero"`
}

func defaultConfig() rabbitmqConfig {
	return rabbitmqConfig{
		Hosts:       nil,
		Username:    "guest",
		Password:    "guest",
		Exchanges:   nil,
		Persistent:  true,
		BulkMaxSize: 2048,
		MaxRetries:  3,
	}
}

// Validate validated whether rabbitmq related options are valid
func (c *rabbitmqConfig) Validate() error {
	if len(c.Hosts) == 0 {
		return errors.New("no hosts configured")
	}

	if c.Username != "" && c.Password == "" {
		return fmt.Errorf("password must be set when username is configured")
	}

	return nil
}

func getRabbitmqURL(host, username, password, vhost string) (string, error) {
	if host == "" {
		return "", fmt.Errorf("host is empty")
	}

	if username == "" {
		return "", fmt.Errorf("username is empty")
	}

	if password == "" {
		return "", fmt.Errorf("password is empty")
	}

	url := ""
	if vhost == "" {
		url = fmt.Sprintf("amqp://%s:%s@%s/", username, password, host)
	} else {
		url = fmt.Sprintf("amqp://%s:%s@%s/%s", username, password, host, vhost)
	}

	return url, nil
}
