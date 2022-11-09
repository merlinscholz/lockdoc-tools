.PHONY: all

all: output/fail-client

src/LICENSE:
	git clone ssh://git@git.cs.tu-dortmund.de:2222/LockDoc/fail.git ./src
	# The following is neccessary until https://git.cs.tu-dortmund.de/LockDoc/fail/pulls/1 is merged
	sed -i 's/const Message::Reflection \*ref = msg->GetReflection();/const Reflection \*ref = msg->GetReflection();/g' ./src/src/core/efw/DatabaseExperiment.cc

output/fail-client: src/LICENSE
	docker build -t fail .
	docker create -ti --name fail fail true
	docker cp fail:/fail/build/bin ./output
	docker rm -f fail