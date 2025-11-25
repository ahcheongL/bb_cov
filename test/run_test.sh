#!/bin/bash
set -e 

rm -f out bbout.bc main.cc.cov main.bc bbout.cov main.cc.path.cov pathout.bc pathout.cov

clang -g -c -emit-llvm main.cc -o main.bc

opt -load-pass-plugin=../build/bb_cov_pass.so -passes=bbcov main.bc -o bbout.bc
clang++ bbout.bc -O0 -o bbout.cov -L../build -l:bb_cov_rt.a 
time ./bbout.cov main.cc.cov

echo ""
echo "Coverage result:"
cat main.cc.cov

echo "Line Coverage result:"
python3 ../scripts/get_line_cov.py main.cc.cov


opt -load-pass-plugin=../build/path_cov_pass.so -passes=pathcov main.bc -o pathout.bc
clang++ pathout.bc -o pathout.cov -L../build -l:path_cov_rt.a
time ./pathout.cov main.cc.path.cov

echo ""
echo "Path Coverage result:"
cat main.cc.path.cov
echo ""