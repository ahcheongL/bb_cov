#include <stdint.h>

extern "C" {
extern const uint32_t __num_bbs;
extern uint32_t __path_hash_val;

void __get_output_fn(int *argc_ptr, char **argv_ptr);

void __cov_fini();
}