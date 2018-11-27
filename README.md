# lsfmq

This project is intended to provide one way for lsf users to integrate different kinds of messge queues with lsf. 

lsfeventsbeat is provided as a lsf event publisher which is enhanced from elastic filebeat, loading and parsing latest lsf events from lsf log files (lsb.acct/lsb.stream) and publishing data into message queues such as kafka, RabbitMQ, etc. Three kinds of data are published into message queue:

+ "lsf_acct":        Job finish events (from lsb.acct file)
+ "lsf_events":      All job events and LSF performance metrics (from lsb.stream file)
+ "lsf_job_status":  Job status tracing, published whenever a job status is changed, including current status, previous status, failed reason if job exit (transformed from lsb.stream file)

Sample lsf data consumer is provided for each kind of messge queue to faciliate lsf users to customize their own data consumer based on their own business need.


# Build the lsf publisher (lsfeventsbeat)

__Note__
+ Make sure network is available
+ Make sure git has been installed
+ Download Go installation package from [Go download](https://golang.org/dl/) and set up your go environment.

__Build Steps__
1. Copy the codes into target directory.
2. Specifiy below parameters in build.sh.
    + LSF_VERSION - lsf version. Default value is LSF10
    + BNAME - os platform. Defalut value is linux-x86_64
    + LSF_LIB_PATH - LSF library path. e.g. /opt/lsf10_1_0_7/10.1/linux2.6-glibc2.3-x86_64/lib
3. Run build.sh. If all things go well, package like lsfeventsbeat-6.4.2-${LSF_VERSION}-${BNAME}.tar.gz will be generated.
``` bash
    sh build.sh
```

# Setup the lsf publisher for your message queue

__Note__: The package can be installed on any server either inside or outside lsf cluster. 

1. Uncompress the tar.gz package generated above.
``` bash
    tar -zxvf lsfeventsbeat-6.4.2-LSF10-linux-x86_64.tar.gz
```
2. Copy libreadlsbevents.so to LSF_LIB_PATH specified in __Build__ step
``` bash
    cp lsfeventsbeat-6.4.2-LSF10-linux-x86_64/lib/libreadlsbevents.so ${LSF_LIB_PATH}
```
3. Add LSF_LIB_PATH to LD_LIBRARY_PATH
``` bash
    export LD_LIBRARY_PATH=${LSF_LIB_PATH}:${LD_LIBRARY_PATH}
```

# Config the lsf publisher for your message queue

Ensure specify correct values for parameters below:
+ In "filebeat.inputs" section: 
    - 'paths' is the absolute path of the latest lsf event file:  lsb.stream file for topic "lsf_events" and lsb.acct file for topic "lsf_acct".  To guarantee event order sent to message queue, only the latest lsb.events and lsb.acct file should be harvested. 
    - 'cluster_name' is the name of your lsf cluster. 

+ In "output.*" section:
    - hosts: specify correct "$ip:$port" of your message queue broker server(s).
  
__Note__: 
You can also refer to [Configuring Filebeat](https://www.elastic.co/guide/en/beats/filebeat/current/configuring-howto-filebeat.html) for common file beat configuration.   
    
A option named lsf_topics has been added in filebeat.inputs section for lsf specific configuration. Take the below sample as an example.
``` yml
lsf_topics: 
  - topic_name: "lsf_events"
    type: "job.raw"
    include_fields:
      - version
      - event_type
      - event_time
    exclude_fields:
      - job_description
    add_fields: {cluster_name: "lsf_cluster"}
```

+ topic_name - Output message queue topic name
    - for Kafka, it means topic name
    - for RabbitMQ, it means exchange name
+ type - Parsed data type
    - "job.raw" represents raw job event data
    - "job.status.trace" represents generated job status trace data based on raw job data
+ include_fields - A list of fields name you want lsfeventsbeat to include
+ exclude_fields - A list of fields name you want lsfeventsbeat to exclude
    - if both include_fields and exclude_fields are defined, lsfeventsbeat executes include_fields first and then executes exclude_fields. The order in which the two options are defined doesn’t matter. The include_fields option will always be executed before the exclude_fields option, even if exclude_fields appears before include_fields in the config file.
+ add_fields - Optional fields that you can specify to add additional information to the output


# Run the lsf publisher for Kafka

Enter the target directory and run lsfeventsbeat as below.
``` bash
    ./lsfeventsbeat -c lsfeventsbeat.yml
```

# Consume lsf events data in Kafka

For example, use Kafka built-in consumer tool "kafka-console-consumer.sh" to subscribe Kafka topic "lsf_job_status"

``` bash
bin/kafka-console-consumer.sh --bootstrap-server 9.21.51.241:9092 --topic lsf_job_status --from-beginning
{"app_profile":"","begin_time":0,"cluster_name":"test_cluster1","command":"sleep 10","cwd":"/env/lsf/work/cluster1/logdir/stream","depend_cond":"","event_time":"2018-10-18T05:47:38-0400","event_time_utc":1539856058,"event_type":"JOB_NEW","job_arr_idx":0,"job_description":"","job_group":"","job_id":101,"job_name":"yytest","num_arr_elements":1,"out_file":"","project_name":"default","queue_name":"normal","req_num_procs_max":1,"res_req":"","sla":"","src_cluster_name":"","submission_host_name":"icp5x1","submit_time":1539856058,"user_group_name":"","user_name":"u1","version":"10.1"}
{"change_reason":"new job submitted","cluster_name":"","current_status":"PEND","job_arr_idx":0,"job_id":101}
{"cluster_name":"test_cluster1","event_time":1539856059,"event_time_utc":1539856059,"event_type":"JOB_START_ACCEPT","job_arr_idx":0,"job_id":101,"start_time":1539856059,"version":"10.1"}
{"change_reason":"job starts","cluster_name":"","current_status":"RUN","job_arr_idx":0,"job_id":101}
{"cluster_name":"test_cluster1","cpu_time":0.076,"end_time":1539856070,"event_time":"2018-10-18T05:47:50-0400","event_time_utc":1539856070,"event_type":"JOB_STATUS","exit_info":0,"exit_status":0,"job_arr_idx":0,"job_id":101,"job_status":"DONE","job_status_code":64,"max_mem":0,"stime":0.06,"utime":0.016,"version":"10.1"}
{"change_reason":"","cluster_name":"","current_status":"DONE","job_arr_idx":0,"job_id":101,"last_status":"RUN"}
{"cluster_name":"test_cluster1","cpu_time":0.076,"end_time":1539856070,"event_time":"2018-10-18T05:47:50-0400","event_time_utc":1539856070,"event_type":"JOB_STATUS","exit_info":0,"exit_status":0,"job_arr_idx":0,"job_id":101,"job_status":"DONE+PDONE","job_status_code":192,"max_mem":0,"stime":0,"utime":0,"version":"10.1"}
{"change_reason":"","cluster_name":"","current_status":"DONE+PDONE","job_arr_idx":0,"job_id":101}
```