#!/bin/bash
DIR=/lockdoc/bench-out
CWD=`pwd`
BENCH_OUTDEV=/dev/null
BENCH="dummy"
GATHER_COV=${GATHER_COV:-0}
# Var setup from <ltp>/runltp setup()
export TMPBASE="/tmp"
export LTP_DEV_FS_TYPE="ext2"
export LTPROOT=/opt/kernel/ltp/bin/
export PATH=${LTPROOT}/testcases/bin:${LTPROOT}/bin:$PATH

OS=`uname`
DEFAULT_USER=1000
DEFAULT_GROUP=1000

function run_cmd {
	echo "Running ${1}" | tee ${OUTDEV}
	${@} 2>&1 | tee ${BENCH_OUTDEV}
	if [ ${PIPESTATUS[0]} -ne 0 ];
	then
		echo "Error running \"${1}\"" | tee ${OUTDEV}
	fi
}

if [ ${OS} == "Linux" ];
then
	INDEV=/dev/ttyS1
	OUTDEV=/dev/ttyS0
	if [ ${GATHER_COV} -eq 0 ];
	then
		KERNEL_VERSION=`cat /proc/version-git`
		echo "kernelversion=${KERNEL_VERSION}" | tee ${OUTDEV}

		PREEMPT_COUNT_ADDR=`cat /proc/preemptcount-addr`
		echo "preemptcountaddr=${PREEMPT_COUNT_ADDR}" | tee ${OUTDEV}

		echo "Remount RW" | tee ${OUTDEV}
		/bin/mount -o remount,rw /

		LOCKDOC_TEST_CTL="/proc/lockdoc/control"
		LOCKDOC_TEST_ITER="/proc/lockdoc/iterations"
		DEFAULT_ITERATIONS=`cat ${LOCKDOC_TEST_ITER}`
	fi
elif [ ${OS} == "FreeBSD" ];
then
	stty -f /dev/ttyu0.lock gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu0.init gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu1.lock gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu1.init gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	INDEV=/dev/cuau1
	OUTDEV=/dev/ttyu0
	BENCH_OUTDEV=/dev/ttyu0
	if [ ${GATHER_COV} -eq 0 ];
	then
		KERNEL_VERSION=`cat /dev/lockdoc/version`
		echo "kernelversion=${KERNEL_VERSION}" | tee ${OUTDEV}
		#POOL=`zpool list -o name -H`
		echo "Remount RW" | tee ${OUTDEV}
		mount -u -o rw /
		#zfs set readonly=off ${POOL}/ROOT/default

		LOCKDOC_TEST_CTL="/dev/lockdoc/control"
		LOCKDOC_TEST_ITER="/dev/lockdoc/iterations"
		DEFAULT_ITERATIONS=`cat ${LOCKDOC_TEST_ITER}`
		DEFAULT_USER=1001
		DEFAULT_GROUP=20
	fi
fi

if [ ! -d ${DIR} ];
then
	mkdir -p ${DIR}
fi

cd ${DIR}

if [ ${GATHER_COV} -eq 0 ];
then
	echo "requestbench" | tee ${OUTDEV}
	BENCH=`head -n1 ${INDEV} | sed 's/^.*&//g'`
	#BENCH=mixed-fs
else
	if [ -z ${1} ];
	then
		echo "No benchmark given" >&2
		exit 1
	fi
	BENCH=${1}
fi
echo "Running "${BENCH} | tee ${OUTDEV}

if [[ ${BENCH} =~ ^lockdoc-test.*$ ]];
then
	if [ ! -e ${LOCKDOC_TEST_CTL} ] || [ ! -e ${LOCKDOC_TEST_ITER} ];
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
		if [ -e ${LOCKDOC_TEST_CTL} ];
		then
			echo 1 > ${LOCKDOC_TEST_CTL}
		else
			echo "${LOCKDOC_TEST_CTL} does not exists" | tee ${OUTDEV}
		fi
	fi
elif [ "${BENCH}" == "sysbench" ];
then
	PREPARED_TEST=0
	if [ ! -f ${DIR}/test_file.0 ];
	then
		PREPARED_TEST=1
		echo "Sysbench Preparation" | tee ${OUTDEV}
		run_cmd /usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw prepare 
	fi

	run_cmd /usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw run
	if [ ${PREPARED_TEST} -eq 1 ];
	then
		echo "Sysbench Cleanup" | tee ${OUTDEV}
		run_cmd /usr/bin/sysbench --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw cleanup
	fi
elif [ "${BENCH}" == "mixed-fs" ];
then
	run_cmd fs-bench-test2.sh
	run_cmd fsstress -d bar -l 1 -n 20 -p 10 -s 4711 -v
	if [ ! -d foo ];
	then
		mkdir foo
	fi
	run_cmd fs_inod foo 10 10 1
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
elif [ "${BENCH}" == "ltp-fs" ];
then
	runltp -f fs
elif [ "${BENCH}" == "ltp-syscall-custom" ];
then
	# Content from <ltp>/runtest/syscalls
	run_cmd access01
	run_cmd access02
	run_cmd access03
	run_cmd access04
 
	run_cmd chdir01
	run_cmd chdir02
	run_cmd chdir03
	run_cmd chdir04

	run_cmd chmod01
	run_cmd chmod02
	run_cmd chmod03
	run_cmd chmod04
	run_cmd chmod05
	run_cmd chmod06
	run_md chmod07

	run_cmd chown01
	run_cmd chown02
	run_cmd chown03
	run_cmd chown04
	run_cmd chown05

	run_cmd close01
	run_cmd close02
	run_cmd close08

	run_cmd creat01
	run_cmd creat03
	run_cmd creat04
	run_cmd creat05
	run_cmd creat06
	run_cmd creat07
	run_cmd creat08

	run_cmd flock01
	run_cmd flock02
	run_cmd flock03
	run_cmd flock04
	run_cmd flock06

	run_cmd getxattr01
	run_cmd getxattr02
	run_cmd getxattr03
	# Uses XFS which is not support / of intereset
#	run_cmd getxattr04
#	run_cmd getxattr05

	run_cmd inotify01
	run_cmd inotify02
	run_cmd inotify03
	run_cmd inotify04
	run_cmd inotify05
	run_cmd inotify06
	run_cmd inotify07
	run_cmd inotify08
	run_cmd inotify09

	run_cmd link02
	run_cmd link03
	run_cmd link04
	run_cmd link05
	run_cmd link06
	run_cmd link07
	run_cmd link08

	run_cmd listxattr01
	run_cmd listxattr02
	run_cmd listxattr03

	run_cmd llistxattr01
	run_cmd llistxattr02
	run_cmd llistxattr03

	run_cmd lremovexattr01

	run_cmd mkdir02
	run_cmd mkdir03
	run_cmd mkdir04
	run_cmd mkdir05
	run_cmd mkdir09

	run_cmd mount01
	run_cmd mount02
	run_cmd mount03
	run_cmd mount04
	run_cmd mount05
	run_cmd mount06

	run_cmd open01
	run_cmd open02
	run_cmd open03
	run_cmd open04
	run_cmd open05
	run_cmd open06
	run_cmd open07
	run_cmd open08
	run_cmd open09
	run_cmd open10
	run_cmd open11
	run_cmd open12
	run_cmd open13
	run_cmd open14

	run_cmd pipe01
	run_cmd pipe02
	run_cmd pipe03
	run_cmd pipe04
	run_cmd pipe05
	run_cmd pipe06
	run_cmd pipe07
	run_cmd pipe08
	run_cmd pipe09
	run_cmd pipe10
	run_cmd pipe11

	run_cmd pipe2_01
	run_cmd pipe2_02

	run_cmd poll01
	run_cmd poll02

	run_cmd read01
	run_cmd read02
	run_cmd read03
	run_cmd read04

	run_cmd readlink01
	run_cmd readlink03

	run_cmd rmdir01
	run_cmd rmdir02
	run_cmd rmdir03

	run_cmd symlink01
	run_cmd symlink02
	run_cmd symlink03
	run_cmd symlink04
	run_cmd symlink05

	run_cmd umount01
	run_cmd umount02
	run_cmd umount03

	run_cmd unlink05
	run_cmd unlink07
	run_cmd unlink08

	run_cmd write01
	run_cmd write02
	run_cmd write03
	run_cmd write04
	run_cmd write05
elif [ "${BENCH}" == "ltp-fs-custom" ];
then
	if [ ! -d foo ];
	then
		mkdir foo
	fi
	# Content from <ltp>/runtest/fs
	# Original parameter settings: linktest.sh 1000 1000
	run_cmd linktest.sh 10 10
	# Original parameter settings: fs_inod foo 10 10 10
	run_cmd fs_inod foo 10 10 1
	run_cmd openfile -f10 -t10
	run_cmd inode01
	run_cmd inode02
	run_cmd stream01
	run_cmd stream02
	run_cmd stream03
	run_cmd stream04
	run_cmd stream05
	run_cmd ftest01
	run_cmd ftest02
	run_cmd ftest03
	run_cmd ftest04
	run_cmd ftest05
	run_cmd ftest06
	run_cmd ftest07
	run_cmd ftest08
	run_cmd lftest 10
	run_cmd writetest
	# Content from <ltp>/runtest/fs_perms_simple
	run_cmd fs_perms 005 99 99 12 100 x 0
	run_cmd fs_perms 050 99 99 200 99 x 0
	run_cmd fs_perms 500 99 99 99 500 x 0
	run_cmd fs_perms 002 99 99 12 100 w 0
	run_cmd fs_perms 020 99 99 200 99 w 0
	run_cmd fs_perms 200 99 99 99 500 w 0
	run_cmd fs_perms 004 99 99 12 100 r 0
	run_cmd fs_perms 040 99 99 200 99 r 0
	run_cmd fs_perms 400 99 99 99 500 r 0
	run_cmd fs_perms 000 99 99 99 99  r 1
	run_cmd fs_perms 000 99 99 99 99  w 1
	run_cmd fs_perms 000 99 99 99 99  x 1
	run_cmd fs_perms 010 99 99 99 500 x 1
	run_cmd fs_perms 100 99 99 200 99 x 1
	run_cmd fs_perms 020 99 99 99 500 w 1
	run_cmd fs_perms 200 99 99 200 99 w 1
	run_cmd fs_perms 040 99 99 99 500 r 1
	run_cmd fs_perms 400 99 99 200 99 r 1
	if [ -d foo ];
	then
		rm -r foo
	fi
elif [ "${BENCH}" == "startup" ];
then
	echo "Doing nothing!" | tee ${OUTDEV}
else
	echo "Unknown benchmark: "${BENCH} | tee ${OUTDEV}
fi

if [ ${GATHER_COV} -ne 0 ];
then
	exit 0
fi
echo "Remount RO" | tee ${OUTDEV}
if [ ${OS} == "Linux" ];
then
	/bin/mount -o remount,ro /
elif [ ${OS} == "FreeBSD" ];
then
	#POOL=`zpool list -o name -H`
	#zfs set readonly=on ${POOL}/ROOT/default
	mount -u -o ro /
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
