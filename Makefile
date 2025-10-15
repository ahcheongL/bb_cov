ifeq (, $(shell which llvm-config))
$(error "No llvm-config in $$PATH")
endif

LLVMVER  = $(shell llvm-config --version 2>/dev/null | sed 's/git//' | sed 's/svn//' )
LLVM_MAJOR = $(shell llvm-config --version 2>/dev/null | sed 's/\..*//' )
LLVM_MINOR = $(shell llvm-config --version 2>/dev/null | sed 's/.*\.//' | sed 's/git//' | sed 's/svn//' | sed 's/ .*//' )
$(info Detected LLVM VERSION : $(LLVMVER))

CC=clang
CXX=clang++
CFLAGS=`llvm-config --cflags` -fPIC -O2
AR=ar

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS=`llvm-config --cxxflags` -fPIC -ggdb -O0 -DDEBUG
else
	CXXFLAGS=`llvm-config --cxxflags` -fPIC -g -O2
endif

CXXFLAGS += -DLLVM_MAJOR=$(LLVM_MAJOR)

LDFLAGS = `llvm-config --ldflags --system-libs --libs core passes`

all: bb_cov

bb_cov: build/bb_cov_pass.so build/bb_cov_rt.a

build/hash.o: src/hash.cc include/hash.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/pass_bb_map.o: src/pass_bb_map.cc include/pass_bb_map.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/bb_cov_pass.o: src/bb_cov_pass.cc include/bb_cov_pass.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/bb_cov_pass.so: build/bb_cov_pass.o build/hash.o build/pass_bb_map.o
	$(CXX) $(CXXFLAGS) -I include -shared -o $@ $^

build/bb_cov_rt.a: src/bb_cov_rt.cc include/bb_cov_rt.hpp build/hash.o
	$(CXX) $(CXXFLAGS) -I include -c $< -o build/bb_cov_rt.o
	$(AR) rsv $@ build/bb_cov_rt.o build/hash.o

clean:
	rm -rf build/*

$(info $(shell mkdir -p build))
