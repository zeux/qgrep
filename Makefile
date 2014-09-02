CCFLAGS=-c -g -Wall -fPIC -O3 -msse2 -DUSE_SSE2 -DHAVE_PTHREAD -DHAVE_RWLOCK -D_FILE_OFFSET_BITS=64 -Iextern -Iextern/re2
CXXFLAGS=-std=c++0x
LDFLAGS=-lpthread -lstdc++

ifeq ($(shell uname),Darwin)
CCFLAGS+=-force_cpusubtype_ALL -mmacosx-version-min=10.7 -arch i386 -arch x86_64 -stdlib=libc++
LDFLAGS+=-force_cpusubtype_ALL -mmacosx-version-min=10.7 -arch i386 -arch x86_64 -stdlib=libc++
else
LDFLAGS+=-pie -Wl,--dynamic-list=src/qgrep.dynlist
endif

SOURCES=

SOURCES+=extern/re2/re2/bitstate.cc extern/re2/re2/compile.cc extern/re2/re2/dfa.cc extern/re2/re2/filtered_re2.cc extern/re2/re2/mimics_pcre.cc extern/re2/re2/nfa.cc extern/re2/re2/onepass.cc extern/re2/re2/parse.cc extern/re2/re2/perl_groups.cc extern/re2/re2/prefilter.cc extern/re2/re2/prefilter_tree.cc extern/re2/re2/prog.cc extern/re2/re2/re2.cc extern/re2/re2/regexp.cc extern/re2/re2/set.cc extern/re2/re2/simplify.cc extern/re2/re2/tostring.cc extern/re2/re2/unicode_casefold.cc extern/re2/re2/unicode_groups.cc extern/re2/util/arena.cc extern/re2/util/hash.cc extern/re2/util/rune.cc extern/re2/util/stringpiece.cc extern/re2/util/stringprintf.cc extern/re2/util/strutil.cc
SOURCES+=extern/lz4/lz4.c extern/lz4/lz4hc.c

SOURCES+=src/blockpool.cpp src/build.cpp src/compression.cpp src/encoding.cpp src/entrypoint.cpp src/files.cpp src/filestream.cpp src/fileutil.cpp src/fileutil_posix.cpp src/fileutil_win.cpp src/filter.cpp src/filterutil.cpp src/fuzzymatch.cpp src/highlight.cpp src/info.cpp src/init.cpp src/main.cpp src/orderedoutput.cpp src/project.cpp src/regex.cpp src/search.cpp src/stringutil.cpp src/update.cpp src/workqueue.cpp

OBJECTS=$(SOURCES:%=build/%.o)
EXECUTABLE=qgrep

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -rf build

build/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -MMD -MP $< -o $@

build/%.o: %
	@mkdir -p $(dir $@)
	$(CXX) $(CCFLAGS) $(CXXFLAGS) -MMD -MP $< -o $@

-include $(OBJECTS:.o=.d)

.PHONY: all clean
