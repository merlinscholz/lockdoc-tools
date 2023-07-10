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
	echo "Running ${@}" | tee ${OUTDEV}
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
		echo "Remount RW" | tee ${OUTDEV}
		/bin/mount -o remount,rw /

		LOCKDOC_TEST_CTL="/proc/lockdoc/control"
		LOCKDOC_TEST_ITER="/proc/lockdoc/iterations"
		DEFAULT_ITERATIONS=`cat ${LOCKDOC_TEST_ITER}`
	fi
	LTP_CMD="${LTPROOT}/runltp -q"
	DEVICE=/dev/sdb
elif [ ${OS} == "FreeBSD" ];
then
	stty -f /dev/ttyu0.lock gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu0.init gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu1.lock gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	stty -f /dev/ttyu1.init gfmt1:cflag=cb00:iflag=0:lflag=0:oflag=6:discard=f:dsusp=19:eof=4:eol=ff:eol2=ff:erase=7f:erase2=8:intr=3:kill=15:lnext=16:min=1:quit=1c:reprint=12:start=11:status=14:stop=13:susp=1a:time=0:werase=17:ispeed=9600:ospeed=9600
	INDEV=/dev/ttyu1
	OUTDEV=/dev/ttyu0
	if [ ${GATHER_COV} -eq 0 ];
	then
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
	LTP_CMD="chroot /compat/ubuntu ${LTPROOT}/runltp -q"
	DEVICE=/dev/ada1

	# Copied from /etc/rc.d/linuc -- BEGIN
	_emul_path="/compat/ubuntu"
	if [ -x ${_emul_path}/sbin/ldconfigDisabled ]; then
		_tmpdir=`mktemp -d -t linux-ldconfig`
		${_emul_path}/sbin/ldconfig -C ${_tmpdir}/ld.so.cache
		if ! cmp -s ${_tmpdir}/ld.so.cache ${_emul_path}/etc/ld.so.cache; then
			cat ${_tmpdir}/ld.so.cache > ${_emul_path}/etc/ld.so.cache
		fi
		rm -rf ${_tmpdir}
	fi

	# Handle unbranded ELF executables by defaulting to ELFOSABI_LINUX.
#	if [ `sysctl -ni kern.elf64.fallback_brand` -eq "-1" ]; then
#		sysctl kern.elf64.fallback_brand=3 > /dev/null
#	fi

	if [ `sysctl -ni kern.elf32.fallback_brand` -eq "-1" ]; then
		sysctl kern.elf32.fallback_brand=3 > /dev/null
	fi

	/sbin/mount -t linprocfs linprocfs "${_emul_path}/proc"
	/sbin/mount -t linsysfs linsysfs "${_emul_path}/sys"
	/sbin/mount -t devfs devfs "${_emul_path}/dev"
	/sbin/mount -o linrdlnk -t fdescfs fdescfs "${_emul_path}/dev/fd"
	/sbin/mount -o mode=1777 -t tmpfs tmpfs "${_emul_path}/dev/shm"
	# Copied from /etc/rc.d/linuc -- END
	export LTPROOT_UBUNTU=/compat/ubuntu/opt/kernel/ltp/bin
	export PATH=${LTPROOT_UBUNTU}/testcases/bin:${LTPROOT_UBUNTU}/bin:$PATH
elif [ ${OS} == "NetBSD" ];
then
	INDEV=/dev/tty01 # TODO Check numbering
	OUTDEV=/dev/tty00
	if [ ${GATHER_COV} -eq 0 ];
	then
		echo "Remount RW" | tee ${OUTDEV}
		mount -u -o rw /

		LOCKDOC_TEST_CTL="/dev/lockdoc/control"
		LOCKDOC_TEST_ITER="/dev/lockdoc/iterations"
		DEFAULT_ITERATIONS=`cat ${LOCKDOC_TEST_ITER}`
		DEFAULT_USER=1001
		DEFAULT_GROUP=20
	fi
	LTP_CMD="chroot /compat/ubuntu ${LTPROOT}/runltp -q"
	DEVICE=/dev/wd1 # TODO Change

	mount_procfs -o linux procfs /compat/ubuntu/proc

	export LTPROOT_UBUNTU=/compat/ubuntu/opt/kernel/ltp/bin
	export PATH=${LTPROOT_UBUNTU}/testcases/bin:${LTPROOT_UBUNTU}/bin:$PATH
fi

if [ ! -e ${DEVICE} ];
then
	echo "Aborting due to missing secondary device: ${DEVICE}" | tee ${OUTDEV}
	echo "terminate_" > ${OUTDEV}
fi
export LTP_DEV=${DEVICE}
export LTP_DEV_FS_TYPE=ext4
export LTP_BIG_DEV=${DEVICE}
export LTP_BIG_DEV_FS_TYPE=ext4
export TMPDIR=`mktemp -d /tmp/ltp.XXX`
chmod 0777 ${TMPDIR}
env > ${OUTDEV}

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

if [[ "${BENCH}" =~ ^.*-verbose$ ]];
then
	BENCH=${BENCH/%-verbose/}
	echo "Verbose mode on" | tee ${OUTDEV}
	if [ ${OS} == "Linux" ];
	then
		BENCH_OUTDEV=/dev/ttyS0
	elif [ ${OS} == "FreeBSD" ];
	then
		BENCH_OUTDEV=/dev/ttyu0
	elif [ ${OS} == "NetBSD" ];
	then
		BENCH_OUTDEV=/dev/tty00
	fi
fi

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
	# TODO Broken
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
elif [[ ${BENCH} =~ ^ltp-.*$ ]];
then
	TESTSUITE=${BENCH#ltp-}
	run_cmd ${LTP_CMD} -f ${TESTSUITE}
elif [ "${BENCH}" == "startup" ];
then
	echo "Doing nothing!" | tee ${OUTDEV}
elif [[ ${BENCH} == "atf" ]];
then
	if [ ${OS} != "NetBSD" ];
	then
		echo "ATF benchmark is only supported on NetBSD!"
	else
		cd /usr/tests/
		run_cmd atf-run
	fi
elif [[ ${BENCH} =~ ^atf-.*$ ]];
then
	if [ ${OS} != "NetBSD" ];
	then
		echo "ATF benchmark is only supported on NetBSD!"
	else
		TESTSUITE=${BENCH#atf-}
		cd /usr/tests/
		run_cmd atf-run ${TESTSUITE}
	fi
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
elif [ ${OS} == "NetBSD" ];
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
elif [ ${OS} == "NetBSD" ];
then
	/sbin/poweroff
fi
