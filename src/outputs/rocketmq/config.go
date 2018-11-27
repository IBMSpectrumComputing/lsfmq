package rocketmq

import (
	"github.com/elastic/beats/libbeat/outputs/codec"
)

type rocketmqConfig struct {
	Hosts       []string     `config:"hosts" validate:"required"`
	Codec       codec.Config `config:"codec"`
	BulkMaxSize int          `config:"bulk_max_size"`
	MaxRetries  int          `config:"max_retries"         validate:"min=-1,nonzero"`
}

func defaultConfig() rocketmqConfig {
	return rocketmqConfig{
		Hosts:       nil,
		BulkMaxSize: 2048,
		MaxRetries:  3,
	}
}
