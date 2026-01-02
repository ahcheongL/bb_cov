#include <map>
#include <set>
#include <string>

extern "C" {

void __handle_init(int32_t *argc_ptr, char **argv);
void __record_func_entry(const char *file_name, const char *func_name);
void __record_func_external(const char *func_name);
void __record_func_ret(const char *file_name, const char *func_name);
void __cov_fini();
}