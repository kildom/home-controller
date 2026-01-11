

OBJS=\
	build/gtest-all.o \
	build/gmock-all.o \
	$(patsubst %.cc,build/%.cc.o,$(wildcard *.cc))

CPPFLAGS=\
	-g -O0 \
	-fprofile-arcs -ftest-coverage \
	-Igtest/googletest/include \
	-Igtest/googlemock/include \
	-Igtest/googletest \
	-Igtest/googlemock \
	-I.. \
	-DGTEST_HAS_PTHREAD=0

LCOV=~/Downloads/lcov-1.15/bin/lcov
LCOV_GENHTML=~/Downloads/lcov-1.15/bin/genhtml

all: build/test_main

clean:
	rm -Rf build

test: all
	rm -f build/*.gcda
	build/test_main

cov: test
	gcov -o build test_PacketInQueue.cc.o
	$(LCOV) --capture --directory . --output-file build/cov-all.info
	$(LCOV) --extract build/cov-all.info '*/src/*' --output-file build/cov-src.info
	$(LCOV) --remove build/cov-src.info '*google*' --output-file build/cov.info
	rm *.gcov
	$(LCOV_GENHTML) build/cov.info --output-directory build/cov-report
	echo The report is in build/cov-report/index.html

build/gtest-all.o : gtest/googletest/src/gtest-all.cc Makefile
	mkdir -p $(dir $@)
	g++ -m32 -MMD -c $(CPPFLAGS) $< -o $@

build/gmock-all.o : gtest/googlemock/src/gmock-all.cc Makefile
	mkdir -p $(dir $@)
	g++ -m32 -MMD -c $(CPPFLAGS) $< -o $@

build/%.cc.o : %.cc	Makefile
	mkdir -p $(dir $@)
	g++ -m32 -MMD -c $(CPPFLAGS) -DTEST_FILE_NAME=$(notdir $(basename $<)) $< -o $@

build/test_main: $(OBJS) Makefile
	g++ -m32 $(CPPFLAGS) -o $@ $(OBJS)

-include build/*.d
