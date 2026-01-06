#include <stdint.h>

#include <map>
#include <set>
#include <string>

#define OUTPUT_FN "BB_COV_OUTPUT_FN"

struct CBBEntry {
  const char      *bb_name;
  struct CBBEntry *next;
  char             is_covered;
};

struct CFuncEntry {
  struct CBBEntry         *bbs[sizeof(unsigned char) * 256];
  const char              *func_name;
  const struct CFuncEntry *next;
};

struct CFileEntry {
  const struct CFuncEntry *funcs[sizeof(unsigned char) * 256];
  const char              *filename;
  const struct CFileEntry *next;
};

extern "C" {

// entire bb list generated at compile time
// We use simple hashmap for fast lookup
// The size of the map is 256 (size of unsigned char in binary).
extern struct CFileEntry *__file_func_map[sizeof(unsigned char) * 256];

extern const uint32_t __num_bbs;

void __handle_init(int *argc_ptr, char **argv);
void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const uint32_t bb_id);

static void __cov_read_prev_cov();
static void __write_cov();
void        __cov_fini();
}