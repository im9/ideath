.PHONY: build test clean

build:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j8

test: build
	./build/ideath_tests

clean:
	rm -rf build
