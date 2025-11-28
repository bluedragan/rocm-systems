#!/usr/bin/bash


ROCRRT_DIR="/home/rocm/src/rocm-systems/projects/rocr-runtime"
FILES="$ROCRRT_DIR/runtime/hsa-runtime/core/runtime/amd_aql_queue.cpp
$ROCRRT_DIR/runtime/hsa-runtime/core/runtime/amd_gpu_agent.cpp
$ROCRRT_DIR/libhsakmt/src/queues.c"


if [ $# -eq 0 ]; then
  echo "Error: No arguments provided."
  echo "Usage: $0 64k|4k"
  exit 1 
fi

if [[ "$1" == "64k" || "$1" == "4k" ]]; then
  SUFFIX="$1"
else 
  echo "Error: Please specify either 64k or 4k"
  echo "Usage: $0 64k|4k"
  exit 1
fi

echo "Switching to $1 build"

RECOMPILE=no
for f in $FILES;do
	#echo "diff $f $f.$SUFFIX"
	diff $f $f.$SUFFIX >/dev/null 2>&1
	if  [ "$?" -eq 1 ];then
		cp $f.$SUFFIX $f
		RECOMPILE=yes
	fi
	# echo $f
done

if [[ $RECOMPILE == "yes" ]];then
	BUILD_DIR="$ROCRRT_DIR"/build
	if [ -d "$BUILD_DIR" ]; then
		echo "Rebuilding now..."
		. /opt/rh/gcc-toolset-14/enable
		export PATH=/home/rocm/opt/rocm/bin:$PATH
		cd $BUILD_DIR
		make 
		make install
	else
		echo "Build directory $BUILD_DIR doesn't exit!"
	fi

else
	echo "No rebuild is neccessary"
fi
