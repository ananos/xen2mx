#!/bin/sh

path=$(dirname $0)
case $(basename $path) in
bin)
	# running inside installdir
	TOOLS_DIR="${path}/"
	TESTS_DIR="${path}/tests/"
	;;
tools)
	if [ ! -e "${path}/../config.log" ]; then
		echo "Cannot run from source tree"
		exit -1
	fi
	# running inside builddir
	TOOLS_DIR="${path}/"
	TESTS_DIR="${path}/../tests/"
	;;
*)
	echo "Unrecognized path '$path'"
	exit -1
	;;
esac
echo "Running tools from '$TOOLS_DIR' and tests from '$TESTS_DIR'"

dobase=0
doloopback=0
domisc=0
dovect=0
dopingpong=0
dorandomloop=0
if [ $# -gt 0 ]; then
	while [ $# -gt 0 ]; do
		case $1 in
		all)
			dobase=1
			doloopback=1
			domisc=1
			dovect=1
			dopingpong=1
			dorandomloop=1
			;;
		base)
			dobase=1
			;;
		loopback|-lo)
			doloopback=1
			;;
		misc)
			domisc=1
			;;
		vect)
			dovect=1
			;;
		pingpong)
			dopingpong=1
			;;
		randomloop|random-loop)
			dorandomloop=1
			;;
		*)
			echo "Unrecognized option '$1'"
			exit -1
		esac
		shift
	done
else
	echo "Running ALL tests by default"
	dobase=1
	doloopback=1
	domisc=1
	dovect=1
	dopingpong=1
	dorandomloop=1
fi

if [ $dobase -eq 1 ] ; then
	# check base
	echo "  *************"
	echo "  TEST omx_info"
	${TOOLS_DIR}/omx_info
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi
	echo "  **************************"
	echo "  CHECK board #0 is loopback"
	${TOOLS_DIR}/omx_info | grep "board #0 name lo addr 00:00:00:00:00:00" >/dev/null 2>&1 || (echo "No" && false)
fi

if [ $doloopback -eq 1 ] ; then
	# check-loopback
	echo "  ************************************"
	echo "  TEST loopback with native networking"
	${TESTS_DIR}/omx_loopback_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi
	echo "  ************************************"
	echo "  TEST loopback with shared networking"
	${TESTS_DIR}/omx_loopback_test -s
	echo "  **********************************"
	echo "  TEST loopback with self networking"
	${TESTS_DIR}/omx_loopback_test -S
fi

if [ $domisc -eq 1 ] ; then
	# check-misc
	echo "  ***************"
	echo "  TEST unexpected"
	${TESTS_DIR}/omx_unexp_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi
	echo "  ***************************"
	echo "  TEST unexpected with ctxids"
	OMX_CTXIDS=10,10 ${TESTS_DIR}/omx_unexp_test
	echo "  ***********************"
	echo "  TEST unexpected handler"
	${TESTS_DIR}/omx_unexp_handler_test
	echo "  *************"
	echo "  TEST wait_any"
	OMX_DISABLE_SHARED=1 ${TESTS_DIR}/mx/mx_wait_any_test & _pid=$! ; sleep 1
	OMX_DISABLE_SHARED=1 ${TESTS_DIR}/mx/mx_wait_any_test -e 3 -d localhost ; sleep 1
	kill -9 $_pid 2>/dev/null ; sleep 1
	echo "  ***********"
	echo "  TEST cancel"
	${TESTS_DIR}/omx_cancel_test & _pid=$! ; sleep 1 ; kill -STOP $_pid ; sleep 1
	${TESTS_DIR}/omx_cancel_test -e 3 -d localhost ; sleep 1
	kill -9 $_pid ; kill -CONT $_pid ; sleep 1
	echo "  ***********"
	echo "  TEST wakeup"
	if [ -e ${TESTS_DIR}/mx/mx_wakeup_test ] ; then
		${TESTS_DIR}/mx/mx_wakeup_test & _pid=$! ; sleep 1 ; kill -STOP $_pid ; sleep 1
		${TESTS_DIR}/mx/mx_wakeup_test -e 3 -d localhost ; sleep 1
		kill -9 $_pid ; kill -CONT $_pid ; sleep 1
	else
		echo "Not built"
	fi
fi

if [ $dovect -eq 1 ] ; then
	# check-vect
	echo "  **************************************"
	echo "  TEST vectorials with native networking"
	${TESTS_DIR}/omx_vect_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi
	echo "  **************************************"
	echo "  TEST vectorials with shared networking"
	${TESTS_DIR}/omx_vect_test -s
	echo "  ************************************"
	echo "  TEST vectorials with self networking"
	${TESTS_DIR}/omx_vect_test -S
fi

if [ $dopingpong -eq 1 ] ; then
	# check-pingpong
	echo "  ************************************"
	echo " 	TEST pingpong with native networking"
	OMX_DISABLE_SHARED=1 ${TESTS_DIR}/omx_perf & _pid=$! ; sleep 1
	OMX_DISABLE_SHARED=1 ${TESTS_DIR}/omx_perf -e 3 -d localhost ; sleep 1
	kill -9 $_pid 2>/dev/null ; sleep 1
	echo "  ************************************"
	echo "  TEST pingpong with shared networking"
	${TESTS_DIR}/omx_perf & _pid=$! ; sleep 1
	${TESTS_DIR}/omx_perf -e 3 -d localhost ; sleep 1
	kill -9 $_pid 2>/dev/null ; sleep 1
fi

if [ $dorandomloop -eq 1 ] ; then
	# check-random-loop
	echo "  ******************************************************"
	echo "  TEST random msg loop with native networking during 20s"
	${TESTS_DIR}/mx/mx_msg_loop -R -P 11 & _pid=$! ; sleep 20
	kill -9 $_pid 2>/dev/null ; sleep 1
fi
