package parselsb

// Topic defines lsf event related options
type Topic struct {
	TopicName     string                 `config:"topic_name"`
	Type          string                 `config:"type"`
	IncludeFields []string               `config:"include_fields"`
	ExcludeFields []string               `config:"exclude_fields"`
	AddFields     map[string]interface{} `config:"add_fields"`
	RoutingKeys   []string               `config:"routing_keys"`
}

// Validate validates the topic option for lsf events filter
func (c *Topic) Validate() error {
	return nil
}
