
RELEASE_BUILD=`date '+%y%m%d'`
BUILD_MONTH=$(shell date +%b)
BUILD_DAY=$(shell date +%e)
BUILD_YEAR=$(shell date +%Y)
BUILD_TIME=${BUILD_DAY} ${BUILD_MONTH}, ${BUILD_YEAR}
CURRENT_DIR=$(shell pwd)

BEATS_VERSION=6.4.2

_arch=x86_64
_os=linux-${_arch}

# env variable to build lsfeventsbeat
LSFEVENTSBEAT_PACKAGE=lsfeventsbeat-${BEATS_VERSION}-${LSF_VERSION}-${BNAME}

FILEBEAT_DIR=${BUILDTMP}/src/github.com/elastic/beats/filebeat

prepare:clean
	@echo "====================================================="
	@echo "Prepare the building..."
	@echo "====================================================="
	@mkdir -p ${BUILDTMP} 

build-lsfeventsbeat:	prepare
    # get source code of filebeat to build lsfeventsbeat
	@mkdir -p ${BUILDTMP}/src/github.com/elastic
	@cd ${BUILDTMP}/src/github.com/elastic; git clone -b v6.4.2 https://github.com/elastic/beats.git
    # overwrite some files with our modified versions
	@cp -rf src/filebeat/parselsb ${FILEBEAT_DIR}/
	@cp -rf src/filebeat/log/* ${FILEBEAT_DIR}/input/log/
	@cp -rf src/lsfeventsparser ${FILEBEAT_DIR}/
	@cp -rf src/outputs/rabbitmq ${FILEBEAT_DIR}/../libbeat/outputs/
	@cp -rf src/outputs/rocketmq ${FILEBEAT_DIR}/../libbeat/outputs/
	@cp -rf src/outputs/includes.go ${FILEBEAT_DIR}/../libbeat/publisher/includes/
    # get dependent 3rd package 
	@go get github.com/robfig/cron
	@go get github.com/streadway/amqp
	@go get github.com/sevenNt/rocketmq
	
    # copy lsbatch.h from include/lsf to include/
	@cp -f ${LSF_LIB_PATH}/../../include/lsf/lsbatch.h ${LSF_LIB_PATH}/../../include/
	
    # rebuild filebeat with events parser
	@cd ${FILEBEAT_DIR}/lsfeventsparser; make all
	@cp -rf ${FILEBEAT_DIR}/lsfeventsparser/lib ${FILEBEAT_DIR}/
	@cd ${FILEBEAT_DIR}; env CGO_CFLAGS="-I${FILEBEAT_DIR}/lib" CGO_LDFLAGS="-L${FILEBEAT_DIR}/lib -lreadlsbevents" make; mv filebeat lsfeventsbeat

	# generate package
	@mkdir ${BUILDTMP}/${LSFEVENTSBEAT_PACKAGE}
	@cp ${FILEBEAT_DIR}/lsfeventsbeat ${BUILDTMP}/${LSFEVENTSBEAT_PACKAGE}
	@cp -f README* ${BUILDTMP}/${LSFEVENTSBEAT_PACKAGE}/
	@cp -f conf/* ${BUILDTMP}/${LSFEVENTSBEAT_PACKAGE}/	
	@cp -Rf ${FILEBEAT_DIR}/lib ${BUILDTMP}/${LSFEVENTSBEAT_PACKAGE}	
	@cd ${BUILDTMP}; tar cvf $(LSFEVENTSBEAT_PACKAGE).tar $(LSFEVENTSBEAT_PACKAGE); gzip $(LSFEVENTSBEAT_PACKAGE).tar; mv $(LSFEVENTSBEAT_PACKAGE).tar.gz ${BUILDTMP}/../	
	@rm -rf  ${BUILDTMP}/src ${BUILDTMP}
	
clean:
	@rm -rf ${BUILDTMP} 
