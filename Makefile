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

SMALL ?= 0
ifeq ($(SMALL), 1)
	CXXFLAGS += -DSMALL
endif

CXXFLAGS += -DLLVM_MAJOR=$(LLVM_MAJOR)

all: bb_cov

bb_cov: lib/bb_cov_pass.so lib/bb_cov_rt.a

lib/bb_cov_pass.so: src/bb_cov_pass.cc include/bb_cov_pass.hpp
	$(CXX) $(CXXFLAGS) -I include -shared $< -lLLVMDemangle -o $@

lib/bb_cov_rt.a: src/bb_cov_rt.cc
	$(CXX) $(CXXFLAGS) -I include -c $< -o src/bb_cov_rt.o
	$(AR) rsv $@ src/bb_cov_rt.o

clean:
	rm -f lib/*.so lib/*.a src/*.o
