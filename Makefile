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

all: bb_cov path_cov func_seq

bb_cov: build/bb_cov_pass.so build/bb_cov_rt.a
path_cov: build/path_cov_pass.so build/path_cov_rt.a
func_seq: build/func_seq_pass.so build/func_seq_rt.a

build/hash.o: src/utils/hash.cc include/utils/hash.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/pass_bb_map.o: src/bb/bb_map.cc include/bb/bb_map.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/bb_cov_pass.o: src/bb/bb_cov_pass.cc include/bb/bb_cov_pass.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/bb_cov_pass.so: build/bb_cov_pass.o build/hash.o build/pass_bb_map.o
	$(CXX) $(CXXFLAGS) -I include -shared -o $@ $^

build/bb_cov_rt.a: src/bb/bb_cov_rt.cc include/bb/bb_cov_rt.hpp build/hash.o
	$(CXX) $(CXXFLAGS) -I include -c $< -o build/bb_cov_rt.o
	$(AR) rsv $@ build/bb_cov_rt.o build/hash.o

build/path_cov_pass.o: src/path/path_cov_pass.cc include/path/path_cov_pass.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/path_cov_pass.so: build/path_cov_pass.o
	$(CXX) $(CXXFLAGS) -I include -shared -o $@ $^

build/path_cov_rt.a: src/path/path_cov_rt.cc include/path/path_cov_rt.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o build/path_cov_rt.o
	$(AR) rsv $@ build/path_cov_rt.o


build/func_seq_pass.o: src/func/func_seq_pass.cc include/func/func_seq_pass.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o $@

build/func_seq_pass.so: build/func_seq_pass.o
	$(CXX) $(CXXFLAGS) -I include -shared -o $@ $^

build/func_seq_rt.a: src/func/func_seq_rt.cc include/func/func_seq_rt.hpp
	$(CXX) $(CXXFLAGS) -I include -c $< -o build/func_seq_rt.o
	$(AR) rsv $@ build/func_seq_rt.o

clean:
	rm -rf build/*

$(info $(shell mkdir -p build))
