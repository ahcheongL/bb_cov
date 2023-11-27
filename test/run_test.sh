rm out out.bc main.cc.cov

opt -enable-new-pm=0 -load ../lib/bb_cov_pass.so --bbcov < main.bc -o out.bc
clang++ out.bc -o out -L../lib -l:bb_cov_rt.a 
./out

echo ""
echo "Coverage result:"
cat main.cc.cov