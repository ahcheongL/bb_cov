#include <map>
#include <set>
#include <string>

using namespace std;

extern "C" {
extern const unsigned int __num_bbs;
extern unsigned int       __path_hash_val;

void __get_output_fn(int *argc_ptr, char ***argv_ptr);
void __cov_fini();
}