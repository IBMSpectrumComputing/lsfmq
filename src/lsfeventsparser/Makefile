#1 the include make.misc
#LIB = -lnsl -lrpcsvc -ldl -lrt -lpthread

TOP=$(shell pwd)
include $(TOP)/Make.misc

BUILD_OUT=$(TOP)/lib

#3 windows lib
NTLIB = kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib Ws2_32.lib Mpr.lib ActiveDS.lib ADSIID.lib

NTINCLUDE = ${LSF_INCLUDE} ${JNI_INC} -I. ${COMM_INC} -I $(HOME)/includeNT

# 4 obj files
OBJS = lsbevent_parse.$(OEXT) job_array.$(OEXT) json4c.$(OEXT)

# 5 build  obj file and lib file

#all: $(BNAME)
linux-x86: libreadlsbevents.so
linux-x86_64: libreadlsbevents.so
linux-ppc64: libreadlsbevents.so
linux-ppc64le: libreadlsbevents.so
rhel7_ppcle: linux-ppc64le
ub14_ppcle: linux-ppc64le
windows: readlsbevents.dll
win-x64: readlsbevents.dll
win-ia64: readlsbevents.dll
linux-ia64: libreadlsbevents.so
solaris: libreadlsbevents.so
sol10_64: libreadlsbevents.so
hpuxia64: libreadlsbevents.so
aix5_64: libreadlsbevents.so

libreadlsbevents.so: $(OBJS)
	$(SHLD)   -o $@  $^   ${LSF_LIB}  $(SYS_LIB)

lsbevent_parse.o:   lsbevent_parse.c
	@$(CC)  -D${LSF_VERSION} ${JNI_INC} ${LSF_INCLUDE} ${COMM_INC} -I. -c -o $@ ${EXTRA_CFLAGS} $^; \

lsbevent_parse_test:    lsbevent_parse_test.c libreadlsbevents.so
	@$(CC)  -D${LSF_VERSION} ${JNI_INC} ${LSF_INCLUDE} ${COMM_INC} -I. -L. -lreadlsbevents -o $@ ${EXTRA_CFLAGS} lsbevent_parse_test.c; \

json4c.o:json4c.c
	@$(CC)  -D${LSF_VERSION} ${JNI_INC} ${LSF_INCLUDE} ${COMM_INC} -I. -c -o $@ ${EXTRA_CFLAGS} $^; \

job_array.o: job_array.c
	@$(CC) -D${LSF_VERSION} ${JNI_INC} ${LSF_INCLUDE} ${COMM_INC} -I. -c -o $@ ${EXTRA_CFLAGS} $^; \

readlsbevents.dll: $(OBJS)
	@$(LD) $(LFLAGS) -dll /out:$@ ${LSF_LIB} ${NTLIBS} $^;

lsbevent_parse.obj: lsbevent_parse.c
	@$(CC) -D${LSF_VERSION} -DW2K ${NTINCLUDE} -c ${EXTRA_CFLAGS} $^; \

job_array.obj: job_array.c
	@$(CC) -D${LSF_VERSION} -DW2K ${NTINCLUDE}  -c ${EXTRA_CFLAGS} $^; \

${JNI_UTIL_LIB}: $(COMMON_HEADER)/jni_util.c $(COMMON_HEADER)/jni_util.h
	@cd $(COMMON_HEADER) ;\
	@gmake ;
clean:
	@rm -rf *.$(OEXT) *.$(LEXT) *.$(SOEXT) *.exp ${BUILD_OUT}

test: lsbevent_parse_test
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"METRIC_LOG" "10.1" 1474037117 1473954354 60 0 0 0 0 0 0 0 0 0 0 4074 22 1 0 0 0'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_STATUS" "10.1" 1473960506 601 192 0 0 0.0620 1473960506 0 0 0 0 "" -1 "" -1 -1 0 0'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_CLEAN" "10.1" 1473964115 601 0 0'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_START" "10.1" 1473960504 601 4 0 0 86.0 1 "nickjm3.eng.platformlab.ibm.com" "" "" 0 "" 0 "" 2147483647 "select[type == local] order[r15s:pg] " "" -1 "" -1 0 "" -1 0 0 -1 0 0'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_START_ACCEPT" "10.1" 1473960504 601 16562 16562 0 "" -1 "" -1 -1'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_EXECUTE" "10.1" 1473960504 601 1000473 16562 "/home/nicki/lsfeventsbeat" "/home/nicki" "nicki" 16562 0 "" -1 "" 0 2147483647 "" -1 "" -1 "" -1'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_FINISH" "10.1" 1473960506 601 1000473 33554434 1 1473960503 0 0 1473960504 "nicki" "normal" "" "" "" "nickjm2" "lsfeventsbeat" "" "" "" "1473960503.601" 0 1 "nickjm3.eng.platformlab.ibm.com" 64 86.0 "" "sleep 1" 0.011998 0.049992 1568 0 -1 0 0 975 3 0 1400 0 -1 0 0 0 28 14 -1 "" "default" 0 1 "" "" 0 2048 228352 "" "" "" "" 0 "" 0 "" -1 "/nicki" "" "" "" -1 "" "" 1040  "" 2 1032 "0" 1033 "0" 0 -1 0 2048 "select[type == local] order[r15s:pg] " "" -1 "" -1 0 "" "" 2 "lsfeventsbeat" 0 1 "nickjm3.eng.platformlab.ibm.com" -1 0'
	env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:`pwd` ./lsbevent_parse_test '"JOB_FINISH2" "10.1" 1473960506 601 39 "userId" "1000473" "userName" "nicki" "numProcessors" "1" "options" "33554434" "jStatus" "64" "submitTime" "1473960503" "termTime" "0" "startTime" "1473960504" "endTime" "1473960506" "queue" "normal" "fromHost" "nickjm2" "cwd" "lsfeventsbeat" "jobFile" "1473960503.601" "numExHosts" "1" "execHosts" "nickjm3.eng.platformlab.ibm.com" "slotUsages" "1" "cpuTime" "0.061990" "command" "sleep 1" "ru_utime" "0.011998" "ru_stime" "0.049992" "ru_maxrss" "2048" "ru_nswap" "228352" "projectName" "default" "exitStatus" "0" "maxNumProcessors" "1" "exitInfo" "0" "chargedSAAP" "/nicki" "numhRusages" "0" "runtime" "2" "maxMem" "2048" "avgMem" "2048" "effectiveResReq" "select[type == local] order[r15s:pg] " "subcwd" "lsfeventsbeat" "serial_job_energy" "0.000000" "numAllocSlots" "1" "allocSlots" "nickjm3.eng.platformlab.ibm.com" "ineligiblePendingTime" "-1" "options2" "1040" "hostFactor" "86.000000"'

all:
	@make clean
	@make ${BNAME}
	@mkdir -p ${BUILD_OUT}
	@cp *.$(SOEXT) ${BUILD_OUT}/
	@cp lsbevent_parse.h ${BUILD_OUT}/
