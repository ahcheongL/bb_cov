#include <map>
#include <set>
#include <string>

using namespace std;

struct FuncBBMap {
  const char        *func_name;
  const char       **bb_names;
  const unsigned int size;
};

struct FileFuncMap {
  const char        *filename;
  struct FuncBBMap  *entries;
  const unsigned int size;
};

extern "C" {

// entire bb list generated at compile time
extern struct FileFuncMap __bb_map[];
extern unsigned int       __bb_map_size;
extern unsigned int       __num_bbs;

void __get_output_fn(int *argc_ptr, char ***argv_ptr);
void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const unsigned int bb_id);

void __write_cov(const map<string, map<string, set<string>>> &prev_cov);
void __cov_fini();
}