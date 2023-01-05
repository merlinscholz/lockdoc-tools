ltp_build: src/VERSION
	cp -r ../../vm-linux-32/scripts ./scripts
	podman build -t ltp .
	podman rm ltp || true
	podman create -ti --name ltp ltp true
	rm -rf ltp/bin
	podman cp ltp:/opt/ output
	podman rm -f ltp

src/VERSION:
	git clone ssh://git@git.cs.tu-dortmund.de:2222/LockDoc/ltp.git src
