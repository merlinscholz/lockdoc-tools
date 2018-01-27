TARGET=make-full-linux-history

all: make-full-linux-history
make-full-linux-history:
	ocamlc str.cma unix.cma common.ml make-full-linux-history.ml -o $(TARGET)

build: make-full-linux-history
	./make-full-linux-history -build

FILES=common.ml make-full-linux-history.ml \
      history-dave-annotated.log history-tglx-annotated-smallversion.log \
      Makefile


tar: 
	tar cvfz $(TARGET).tgz $(FILES)

clean: 
	rm *.cm* $(TARGET)

.PHONY: all tar clean build
