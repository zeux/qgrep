CC=gcc
CFLAGS=-c -Wall -fPIC -O3 -msse2
CXXFLAGS=$(CFLAGS) -std=c++0x -Iextern -Iextern/re2 -DHAVE_PTHREAD -DHAVE_RWLOCK
LDFLAGS=-pie -lpthread -lstdc++

ifeq ($(shell uname),Darwin)
# Use gcc from MacPorts on OS X (clang from Xcode crashes on lambdas)
CC=gcc-mp-4.7
else
LDFLAGS+=-Wl,--dynamic-list=src/qgrep.dynlist
endif

SOURCES=

SOURCES+=extern/re2/re2/bitstate.cc extern/re2/re2/compile.cc extern/re2/re2/dfa.cc extern/re2/re2/filtered_re2.cc extern/re2/re2/mimics_pcre.cc extern/re2/re2/nfa.cc extern/re2/re2/onepass.cc extern/re2/re2/parse.cc extern/re2/re2/perl_groups.cc extern/re2/re2/prefilter.cc extern/re2/re2/prefilter_tree.cc extern/re2/re2/prog.cc extern/re2/re2/re2.cc extern/re2/re2/regexp.cc extern/re2/re2/set.cc extern/re2/re2/simplify.cc extern/re2/re2/tostring.cc extern/re2/re2/unicode_casefold.cc extern/re2/re2/unicode_groups.cc extern/re2/util/arena.cc extern/re2/util/hash.cc extern/re2/util/rune.cc extern/re2/util/stringpiece.cc extern/re2/util/stringprintf.cc extern/re2/util/strutil.cc
SOURCES+=extern/lz4/lz4.c extern/lz4/lz4hc.c

SOURCES+=src/blockpool.cpp src/build.cpp src/encoding.cpp src/entrypoint.cpp src/files.cpp src/fileutil.cpp src/fileutil_posix.cpp src/fileutil_win.cpp src/info.cpp src/init.cpp src/main.cpp src/orderedoutput.cpp src/project.cpp src/regex.cpp src/search.cpp src/stringutil.cpp src/update.cpp src/workqueue.cpp

OBJECTS=$(SOURCES:%=obj/%.o)
EXECUTABLE=qgrep

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

obj/%.cc.o: %.cc
	mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) $< -o $@

obj/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) $< -o $@

obj/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf obj

.PHONY: all clean
