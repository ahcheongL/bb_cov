#include <map>
#include <set>
#include <string>

using namespace std;

struct CBBEntry {
  const char      *bb_name;
  struct CBBEntry *next;
  char             is_covered;
};

struct CFuncEntry {
  struct CBBEntry   *bbs[sizeof(unsigned char) * 256];
  const char        *func_name;
  struct CFuncEntry *next;
};

struct CFileEntry {
  struct CFuncEntry *funcs[sizeof(unsigned char) * 256];
  const char        *filename;
  struct CFileEntry *next;
};

extern "C" {

// entire bb list generated at compile time
// We use simple hashmap for fast lookup
// The size of the map is 256 (size of unsigned char in binary).
extern struct CFileEntry *__file_func_map[sizeof(unsigned char) * 256];

extern const unsigned int __num_bbs;

void __handle_init(int *argc_ptr, char **argv);
void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const unsigned int bb_id);

static void __write_cov(const map<string, map<string, set<string>>> &prev_cov);
void        __cov_fini();
}