./preprocess-kcov.sh kcov-maps out
./kernel-get-all-bbs.sh vmlinux out/all.map
./subsystem-bbs.sh vmlinux out/all.map '/fs/|/mm/|fs\.h|mm\.h' > out/all-fs.map
./coverage-climber.sh  out/all-maps/ out/drop-all.txt out/all-fs.map
./coverage-climber.sh  out/unique/kcov-maps/syscalls out/drop-syscalls.txt out/all-fs.map
./compose-testsuite.sh out/drop-all.txt ltp-src/ out/all-maps/ > out/lockdoc-testsuite-all
./compose-testsuite.sh out/drop-syscalls.txt ltp-src/ out/unique-maps/kcov-maps/syscalls > out/lockdoc-testsuite-syscalls
