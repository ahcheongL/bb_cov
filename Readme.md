# BasicBlock coverage measurement

## Prerequisite

1. Clang/LLVM 20.
    * Use apt package downloader (https://apt.llvm.org) or manualy built from source (https://releases.llvm.org/download.html).
    * It assumes `llvm-config, clang, clang++, opt, ...` are on `PATH`

2. [gllvm](https://github.com/SRI-CSL/gllvm), needed to get whole program bitcode

3. Python 3.6+, make

## Build
  `make`

## 1. Target subject build with gllvm

1. Build target program using `make` or `cmake` as usual, but set `CC=gclang` and `CXX=gclang++` to let gllvm to compile the target program. It varies how to set compiler to use for different programs, but most popular open source programs support building with non-default compiler.
    * It is recommend to use `--disable-shared` flags.
        * Codes that is build in shared libraries won't be instrumented for carving, so you won't able to get carved objects.
    * It is recommend to turn on debug options, but it is not necessary.
    * Example : ``CC=gclang CXX=gclang++ CFLAGS="-O0 -g" CXXFLAGS="-O0 -g" ./configure --prefix=`pwd`/gclang_install --disable-shared``
2. `get-bc <target executable>` You can get bitcode of the executable file.
3. (optional) `llvm-dis <target.bc>` will make human-readable LLVM IR code of the target program.

## 2. Coverage instrumentation

1. `opt -load-pass-plugin {$PROJECT_PATH}/build/bb_cov_pass.so -passes=bbcov <target.bc> -o <out.bc>`

2. `clang++ <out.bc> <compile flags> -o <target.cov> -L {$PROJECT_PATH}/build -l:bb_cov_rt.a`
    * `<compile flags>` are usually shared libraries that are linked to the original target executable.
    * You can get list of shared linked shared libraries by running `ldd <target executable>`, if libpthread.so is linked, you need to put `-lpthread` as compile flags

3. It will generate an executable named as like `target.cov`.

## 3. Run coverage instrumented executable

1. `<target.cov> <args...> <output_fn>` ; Run it just same as usual executable, just add an output file path at the end.
2. It will generate file `<output_fn>`
Each line indicates whether a function or a basic block is covered.  
F means function, and B means basic block.
3. If `<output_fn>` already exists, the program reads the coverage and record accumulated coverage.

## 4. See results

1. You can use bin/collect_cov.py script.
