#!/bin/bash
for f in *.map; do
	for g in *.map; do
		if [ $f = $g ]; then
			continue
		fi
		echo $f $(wc -l <$f) $g $(wc -l <$g) : $($(dirname $0)/set-intersect $f $g | wc -l)
	done
done
