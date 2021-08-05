#!/bin/bash

# run this with a list of .map files, piped into "| Rscript -"
#
# prerequisite: R package "eulerr" (R console: install.packages("eulerr"))

set -e

# some R
cat <<EOT
# prerequisite: R package "eulerr" (R console: install.packages("eulerr"))
library(eulerr)
fit <- euler(c(
EOT

# transfer parameters into a Bash array
suites=()
for f in "$@"; do
	suites+=($f)
done

# generate power set by iterating from 1 to 2^n-1
n=${#suites[@]}
powersize=$((1 << $n))

i=1 # we're not interested in the empty set
while [ $i -lt $powersize ]; do
	if [ $i -ne 1 ]; then
		echo ","
	fi

	subset=()
	j=0
	while [ $j -lt $n ]; do
        	if [ $(((1 << $j) & $i)) -gt 0 ]; then
			subset+=("${suites[$j]}")
		fi
		j=$(($j + 1))
	done

	if [ ${#subset[@]} -eq 1 ]; then
		EULERR_OVERLAP=$( wc -l < ${subset[@]} )
	else
		EULERR_OVERLAP=$( $(dirname $0)/set-intersect ${subset[@]} | wc -l )
	fi
	# Frickel
	EULERR_NAME=$( echo ${subset[@]} | sed -e 's@[a-z-]*/@@g' -e 's/\s/\&/g' -e 's/-all\.map//g' -e 's/all-fs.map/VFS/g' -e s/syzkaller-continuous/syzkaller/g -e s/ltp/LTP/g)
	echo "'$EULERR_NAME' = $EULERR_OVERLAP"

	i=$(($i + 1))
done

# some more R
cat <<EOT
), shape = 'ellipse', input = 'union', control = list('extraopt' = FALSE))
pdf(file = "euler.pdf", width = 3, height = 3, family = "Helvetica", title = "euler")
plot(fit, quantities = TRUE)
dev.off()
embedFonts("euler.pdf", "pdfwrite")
EOT
