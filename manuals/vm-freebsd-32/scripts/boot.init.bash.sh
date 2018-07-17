#!/bin/bash
echo "PRE-INIT SCRIPT"
echo "Please press a character key to start normal"
sleep 1
read -d "\n" -n 1 -t 5 answer
if [ "$answer" == "" ]; then
	echo "Starting Bench"
	bash /home/al/run-bench.sh
else
	echo ""
	echo "Starting normal!"
fi
sleep 1

