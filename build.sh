#!/bin/sh

#-----------------------------------------------------------------------------
#
# required parameters
#
#-----------------------------------------------------------------------------

# LSF version
LSF_VERSION=LSF10

# os platform name
BNAME=linux-x86_64

# LSF library path $LSF_LIBDIR, under which the header file and lib are placed and used to compile the so file
# e.g. /opt/lsf10/10.1/linux2.6-glibc2.3-x86_64/lib
LSF_LIB_PATH=/home/yyhe/env/lsf/10.1/linux2.6-glibc2.3-x86_64/lib
#-----------------------------------------------------------------------------

curr_dir=`pwd`

# set up env and complier for lsfeventsbeat
dirname=$(cd "$(dirname "$0")"; pwd)
cd $dirname

# check whether git is available
command -v git >/dev/null 2>&1 || { echo "git is required but it's not installed.  Exiting."; exit 1; }  

# validate and export required parameters
if [ -z $LSF_VERSION ]; then 
	echo "LSF_VERSION is required. Exiting..."
	exit 1
else
	export LSF_VERSION
fi

if [ -z $BNAME ]; then 
	echo "BNAME is required. Exiting..."
	exit 1
else
	export BNAME
fi

if [ -z $LSF_LIB_PATH ] || [ ! -d $LSF_LIB_PATH ]; then 
	echo "LSF_LIB_PATH is not a valid path. Exiting..."
	exit 1
else
	export LSF_LIB_PATH
fi

# install Go if there is no available go environment
if [ -z "$GOROOT" ] || [ ! -x "$GOROOT"/bin/go ]; then
	echo "GOROOT is not set. Exiting..."
	exit 1
fi
which go >/dev/null 2>&1 || export PATH=${GOROOT}/bin:$PATH

BUILDTMP=${dirname}/buildtmp; export BUILDTMP
GOPATH=$BUILDTMP; export GOPATH

# build rpm package
make build-lsfeventsbeat

cd $curr_dir
