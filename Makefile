.PHONY: build test clean repl

build:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j8

test: build
	./build/ideath_tests

repl:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -DTN_DSP_BUILD_REPL=ON
	cmake --build build -j8 --target ideath_repl
	@if command -v rlwrap >/dev/null 2>&1; then \
		rlwrap ./build/tools/repl/ideath_repl; \
	else \
		./build/tools/repl/ideath_repl; \
	fi

clean:
	rm -rf build
