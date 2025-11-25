#include <map>
#include <set>
#include <string>

using namespace std;

extern "C" {

void __handle_init(int *argc_ptr, char **argv);
void __record_func_entry(const char *file_name, const char *func_name);
void __record_func_ret(const char *file_name, const char *func_name);
void __cov_fini();
}