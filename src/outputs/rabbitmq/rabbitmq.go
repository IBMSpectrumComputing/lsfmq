package rabbitmq

import (
	"github.com/elastic/beats/libbeat/beat"
	"github.com/elastic/beats/libbeat/common"
	"github.com/elastic/beats/libbeat/logp"
	"github.com/elastic/beats/libbeat/outputs"
	"github.com/elastic/beats/libbeat/outputs/codec"
)

var debugf = logp.MakeDebug("rabbitmq")

func init() {
	outputs.RegisterType("rabbitmq", makeRabbitmq)
}

func makeRabbitmq(
	beat beat.Info,
	observer outputs.Observer,
	cfg *common.Config,
) (outputs.Group, error) {
	debugf("initialize rabbitmq output")

	config := defaultConfig()
	if err := cfg.Unpack(&config); err != nil {
		return outputs.Fail(err)
	}

	codec, err := codec.CreateEncoder(beat, config.Codec)
	if err != nil {
		return outputs.Fail(err)
	}

	client, err := newRabbitmqClient(observer, &config, beat.Beat, codec)
	if err != nil {
		return outputs.Fail(err)
	}

	retry := 0
	if config.MaxRetries < 0 {
		retry = -1
	}

	return outputs.Success(config.BulkMaxSize, retry, client)
}
