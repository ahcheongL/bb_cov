set -e 

rm -f out out.bc main.cc.cov main.bc

clang -g -c -emit-llvm main.cc -o main.bc

opt -load-pass-plugin=../build/bb_cov_pass.so -passes=bbcov < main.bc -o out.bc
clang++ out.bc -o out -L../build -l:bb_cov_rt.a 
./out

echo ""
echo "Coverage result:"
cat main.cc.cov