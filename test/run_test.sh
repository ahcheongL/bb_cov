#!/bin/bash
set -e 

cd "$(dirname "$0")"

rm -f out void_main crash *.bc *.cov *.path *.bb *.func

clang -g -c -emit-llvm main.cc -o main.bc
clang -g -c -emit-llvm void_main.cc -o void_main.bc
clang -g -c -emit-llvm crash.cc -o crash.bc

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

opt -load-pass-plugin=../build/func_seq_pass.so -passes=funcseq main.bc -o func.bc
clang++ func.bc -o func -L../build -l:func_seq_rt.a
time ./func func.cov

echo ""
echo "Function Sequence Coverage result:"
cat func.cov
echo ""

opt -load-pass-plugin=../build/bb_cov_pass.so -passes=bbcov void_main.bc -o void_main.bb.bc
clang++ void_main.bb.bc -O0 -o void_main.bb -L../build -l:bb_cov_rt.a
time ./void_main.bb void_main.cov

echo ""
echo "Coverage result for int main(void):"
cat void_main.cov
echo ""

opt -load-pass-plugin=../build/bb_cov_pass.so -passes=bbcov crash.bc -o crash.bb.bc

clang++ crash.bb.bc -O0 -o crash.bb -L../build -l:bb_cov_rt.a

set +e

time ./crash.bb crash.cov

echo ""
echo "Coverage result for crash program (using normal runtime):"
cat crash.cov
echo ""


clang++ crash.bb.bc -O0 -o crash.bb -L../build -l:bb_cov_instant_rt.a

time ./crash.bb crash.cov

echo ""
echo "Coverage result for crash program:"
cat crash.cov
echo ""
