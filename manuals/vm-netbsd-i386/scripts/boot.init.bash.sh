#!/usr/local/bin/bash
echo "PRE-INIT SCRIPT"
if uname -v | grep "LOCKDOC";
then
	echo "Please press a character key to start normal"
	sleep 1
	read -d "\n" -n 1 -t 5 answer
	if [ "$answer" == "" ]; then
		echo "Starting Bench"
		/usr/pkg/bin/bash /lockdoc/run-bench.sh
	else
		echo ""
		echo "Starting normal!"
	fi
	sleep 1
else
	echo "Found normal kernel. Continuing boot process..."
fi
