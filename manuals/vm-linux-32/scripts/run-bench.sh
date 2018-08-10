#!/bin/bash
DIR=/home/al/bench-out
CWD=`pwd`
BENCH_OUTDEV=/dev/null
BENCH="dummy"
export PATH=/opt/kernel/ltp/bin/testcases/bin/:$PATH

cd ${DIR}

OS=`uname`
DEFAULT_USER=1000
DEFAULT_GROUP=1000

if [ ${OS} == "Linux" ];
then
	INDEV=/dev/ttyS1
	OUTDEV=/dev/ttyS0
	KERNEL_VERSION=`cat /proc/version-git`
	echo "kernelversion=${KERNEL_VERSION}" | tee ${OUTDEV}

	PREEMPT_COUNT_ADDR=`cat /proc/preemptcount-addr`
	echo "preemptcountaddr=${PREEMPT_COUNT_ADDR}" | tee ${OUTDEV}

	echo "Remount RW" | tee ${OUTDEV}
	/bin/mount -o remount,rw /

	LOCKDOC_TEST_CTL="/proc/lockdoc/control"
	LOCKDOC_TEST_ITER="/proc/lockdoc/iterations"
	DEFAULT_ITERATIONS=`cat ${LOCKDOC_TEST_ITER}`
elif [ ${OS} == "FreeBSD" ];
then
	INDEV=/dev/ttyu1
	OUTDEV=/dev/ttyu0
	KERNEL_VERSION=`uname -r`

	echo "Remount RW" | tee ${OUTDEV}
	/sbin/mount -o rw /
fi

echo "requestbench" | tee ${OUTDEV}
BENCH=`head -n1 ${INDEV} | sed 's/^.*&//g'`
#BENCH=sysbench
echo "Running "${BENCH} | tee ${OUTDEV}

if [[ ${BENCH} =~ ^lockdoc-test.*$ ]];
then
	if [ ! -f ${LOCKDOC_TEST_CTL} ] || [ ! -f ${LOCKDOC_TEST_ITER} ];
	then
		echo "Either ${LOCKDOC_TEST_CTL} or ${LOCKDOC_TEST_ITER} does not exist!" >&2
	else
		ITERATIONS=${DEFAULT_ITERATIONS}
		if [[ ${BENCH} =~ ^lockdoc-test-.*$ ]];
		then
		        ITERATIONS=${BENCH#lockdoc-test-}
		        if [ -z "${ITERATIONS}" ];
		        then
				echo "Error: Variable ITERATIONS is empty. Using default value of ${DEFAULT_ITERATIONS}" | tee ${OUTDEV}
				ITERATIONS=${DEFAULT_ITERATIONS}
		        else
				echo "Writing ${ITERATIONS} to ${LOCKDOC_TEST_ITER}" | tee ${OUTDEV}
		                echo "${ITERATIONS}" > ${LOCKDOC_TEST_ITER}
		                if [ ${?} -ne 0 ];
		                then
	        	                echo "Error writing ${ITERATIONS} to ${LOCKDOC_TEST_ITER}. Using default value of ${DEFAULT_ITERATIONS}" | tee ${OUTDEV}
					ITERATIONS=${DEFAULT_ITERATIONS}
	        	        fi
		        fi
		fi
		echo "Using ${ITERATIONS} iterations" | tee ${OUTDEV}
		echo "Starting LockDoc test" | tee ${OUTDEV}
		if [ -f ${LOCKDOC_TEST_CTL} ];
		then
			echo 1 > ${LOCKDOC_TEST_CTL}
		else
			echo "${LOCKDOC_TEST_CTL} does not exists" | tee ${OUTDEV}
		fi
	fi
elif [ "${BENCH}" == "sysbench" ];
then
	echo "pipe stress test" | tee  ${OUTDEV}
	counter=1
	max=50
	while [ ${counter} -le ${max} ];
	do
		cat fork.c | grep task | grep free > /dev/null		
		if [ ${?} -ne 0 ];
		then
			echo "Error pipe stress test" | tee ${OUTDEV}
		fi
		let counter=counter+1
	done;
	sleep 1

	PREPARED_TEST=0
	if [ ! -f ${DIR}/test_file.0 ];
	then
		PREPARED_TEST=1
		echo "Preparing test" | tee ${OUTDEV}
		/usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw prepare 2>&1 | tee ${BENCH_OUTDEV}
	fi
	sleep 1

	echo "SysBench" | tee ${OUTDEV}
	/usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw run 2>&1 | tee ${BENCH_OUTDEV}
	if [ ${PREPARED_TEST} -eq 1 ];
	then
		echo "Cleaning up" | tee ${OUTDEV}
		/usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw cleanup 2>&1 | tee ${BENCH_OUTDEV}
	fi
elif [ "${BENCH}" == "mixed-fs" ];
then
	echo "FS-Bench" | tee ${OUTDEV}

	fs-bench-test2.sh 2>&1 | tee ${BENCH_OUTDEV}
	if [ ${?} -ne 0 ];
	then
		echo "Error fs-bench" | tee ${OUTDEV}
	fi

	echo "FSStress" | tee  ${OUTDEV}
#	fsstress -d bar -l 2 -n 20 -p 20 -s 4711 -v 2>&1 | tee ${OUTDEV}
	fsstress -d bar -l 1 -n 20 -p 10 -s 4711 -v 2>&1 | tee ${BENCH_OUTDEV}

	if [ ${?} -ne 0 ];
	then
		echo "Error fsstress" | tee ${OUTDEV}
	fi

	echo "FS-Inod" | tee  ${OUTDEV}
	if [ ! -d foo ];
	then
		mkdir foo
	fi
#	fs_inod foo 10 10 2 2>&1 | tee ${OUTDEV}
	fs_inod foo 10 10 1 2>&1 | tee ${BENCH_OUTDEV}
	if [ ${?} -ne 0 ];
	then
		echo "Error fs_inod" | tee ${OUTDEV}
	fi

	echo "chmod/chown stress test" | tee  ${OUTDEV}
	counter=1
	max=2
	while [ ${counter} -le ${max} ];
	do
		chmod -R 0777 .
		if [ ${?} -ne 0 ];
		then
			echo "Error chmod 0777" | tee ${OUTDEV}
		fi
		chown -R 0:0 .	
		if [ ${?} -ne 0 ];
		then
			echo "Error chown 0:0" | tee ${OUTDEV}
		fi
		chmod -R 0755 .
		if [ ${?} -ne 0 ];
		then
			echo "Error chmod 0755" | tee ${OUTDEV}
		fi
		chown -R ${DEFAULT_USER}:${DEFAULT_GROUP} .
		if [ ${?} -ne 0 ];
		then
			echo "Error chown ${DEFAULT_USER}:${DEFAULT_GROUP}" | tee ${OUTDEV}
		fi
		let counter=counter+1
	done;
	chmod -R 0755 .
	chown -R ${DEFAULT_USER}:${DEFAULT_GROUP} .

	echo "pipe stress test" | tee  ${OUTDEV}
	counter=1
	max=2
	while [ ${counter} -le ${max} ];
	do
		cat fork.c | grep task | grep free > /dev/null		
		if [ ${?} -ne 0 ];
		then
			echo "Error pipe stress test" | tee ${OUTDEV}
		fi
		let counter=counter+1
	done;

	echo "link stress test" | tee  ${OUTDEV}
	counter=1
	max=2
	target=foo.txt
	echo "Hello World!" > ${target}
	link=bar.txt
	while [ ${counter} -le ${max} ];
	do
		ln ${target} ${link}
		sleep 1
		rm ${link}
		let counter=counter+1
	done;
	rm ${target}

	if [ -d bar ];
	then
		rm -r bar
	fi
	if [ -d foo ];
	then
		rm -r foo
	fi
	if [ -d 00 ];
	then
		rm -r 00
	fi
elif [ "${BENCH}" == "startup" ];
then
	echo "Doing nothing!" | tee ${OUTDEV}
else
	echo "Unknown benchmark: "${BENCH} | tee ${OUTDEV}
fi

echo "Remount RO" | tee ${OUTDEV}
if [ ${OS} == "Linux" ];
then
	/bin/mount -o remount,ro /
elif [ ${OS} == "FreeBSD" ];
then
	/sbin/mount -o ro /
fi

echo "Syncing" | tee ${OUTDEV}
/bin/sync

echo "Telling Fail BOCHS to terminate" | tee ${OUTDEV}
echo "terminate_" > ${OUTDEV}

echo "Shutdown" | tee ${OUTDEV}
if [ ${OS} == "Linux" ];
then
	/usr/lib/klibc/bin/poweroff
elif [ ${OS} == "FreeBSD" ];
then
	/sbin/poweroff
fi
