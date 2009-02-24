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
	echo
	echo "*************"
	echo "TEST omx_info"
	echo "*************"
	${TOOLS_DIR}/omx_info
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi

	echo
	echo "**************************"
	echo "CHECK board #0 is loopback"
	echo "**************************"
	${TOOLS_DIR}/omx_info | grep "board #0 name lo addr 00:00:00:00:00:00" >/dev/null 2>&1 || (echo "No" && false)
fi

if [ $doloopback -eq 1 ] ; then
	# check-loopback
	echo
	echo "************************************"
	echo "TEST loopback with native networking"
	echo "************************************"
	${TESTS_DIR}/omx_loopback_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi

	echo
	echo "************************************"
	echo "TEST loopback with shared networking"
	echo "************************************"
	${TESTS_DIR}/omx_loopback_test -s

	echo
	echo "**********************************"
	echo "TEST loopback with self networking"
	echo "**********************************"
	${TESTS_DIR}/omx_loopback_test -S
fi

is_process_running() {
    res=$(ps -p $1 | wc -l)
    if [ $res -eq 1 ] ; then echo 0 ; else echo 1; fi
}

start_double_application() {
	application=$1
	disable_shared=$2
	OMX_DISABLE_SHARED=$disable_shared $application & _pid=$! ; sleep 1
	if [ $(is_process_running $_pid) -eq 0 ] ; then
		echo "Open-MX not started" && exit 1
	fi
	OMX_DISABLE_SHARED=$disable_shared $application -e 3 -d localhost ; err=$? ; sleep 1
	kill -9 $_pid 2>/dev/null ; sleep 1
	if [ $err -ne 0 ] ; then
		echo "Failed" && exit 1
	fi
}

start_double_application_with_stop() {
	application=$1
	disable_shared=$2
	OMX_DISABLE_SHARED=$disable_shared $application & _pid=$! ; sleep 1 ; kill -STOP $_pid ; sleep 1
	if [ $(is_process_running $_pid) -eq 0 ] ; then
		echo "Open-MX not started" && exit 1
	fi
	OMX_DISABLE_SHARED=$disable_shared $application -e 3 -d localhost ; err=$? ; sleep 1
	kill -9 $_pid ; kill -CONT $_pid ; sleep 1
	if [ $err -ne 0 ] ; then
		echo "Failed" && exit 1
	fi
}

if [ $domisc -eq 1 ] ; then
	# check-misc
	echo
	echo "***************"
	echo "TEST unexpected"
	echo "***************"
	${TESTS_DIR}/omx_unexp_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi

	echo
	echo "***************************"
	echo "TEST unexpected with ctxids"
	echo "***************************"
	OMX_CTXIDS=10,10 ${TESTS_DIR}/omx_unexp_test

	echo
	echo "***********************"
	echo "TEST unexpected handler"
	echo "***********************"
	${TESTS_DIR}/omx_unexp_handler_test

	echo
	echo "*************"
	echo "TEST wait_any"
	echo "*************"
	start_double_application ${TESTS_DIR}/mx/mx_wait_any_test 1

	echo
	echo "***********"
	echo "TEST cancel"
	echo "***********"
	start_double_application_with_stop ${TESTS_DIR}/omx_cancel_test 0

	echo
	echo "***********"
	echo "TEST wakeup"
	echo "***********"
	if [ -e ${TESTS_DIR}/mx/mx_wakeup_test ] ; then
		start_double_application_with_stop ${TESTS_DIR}/mx/mx_wakeup_test 0
	else
		echo "Not built"
	fi
fi

if [ $dovect -eq 1 ] ; then
	# check-vect
	echo
	echo "**************************************"
	echo " vectorials with native networking"
	echo "**************************************"
	${TESTS_DIR}/omx_vect_test
	if [ $? -ne 0 ] ; then
	    echo "Open-MX not started" && exit 1
	fi

	echo
	echo "**************************************"
	echo "TEST vectorials with shared networking"
	echo "**************************************"
	${TESTS_DIR}/omx_vect_test -s

	echo
	echo "************************************"
	echo "TEST vectorials with self networking"
	echo "************************************"
	${TESTS_DIR}/omx_vect_test -S
fi

if [ $dopingpong -eq 1 ] ; then
	# check-pingpong
	echo
	echo "************************************"
	echo "TEST pingpong with native networking"
	echo "************************************"
	start_double_application ${TESTS_DIR}/omx_perf 1

	echo
	echo "************************************"
	echo "TEST pingpong with shared networking"
	echo "************************************"
	start_double_application ${TESTS_DIR}/omx_perf 0
fi

if [ $dorandomloop -eq 1 ] ; then
	# check-random-loop
	echo
	echo "******************************************************"
	echo "TEST random msg loop with native networking during 20s"
	echo "******************************************************"
	${TESTS_DIR}/mx/mx_msg_loop -R -P 11 & _pid=$! ; sleep 20
	kill -9 $_pid 2>/dev/null ; sleep 1
fi
