#!/bin/bash
mkdir demo
cd demo

seq 1 65536 > domain.map
seq 1 10000 > on1.map
seq 10000 20000 > on2.map
seq 20000 30000 > on3.map
seq 30000 40000 > on4.map
seq 30000 40000 > on5.map
seq 40000 50000 > on6.map
seq 50000 60000 > on7.map
seq 2 2 65536 > input.map

../sfctool -t lines \
-1 on1.map --color 00ffff \
-1 on2.map --color 22ddff \
-1 on3.map --color 44bbff \
-1 on4.map --color 6699ff \
-1 on5.map --color 8877ff \
-1 on6.map --color aa55ff \
-1 on7.map --color cc33ff \
domain.map input.map out.png
