.PHONY: build test clean repl bench

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

bench:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -DTN_DSP_BUILD_BENCH=ON
	cmake --build build -j8 --target ideath_bench
	./build/ideath_bench --benchmark-samples 100

clean:
	rm -rf build
