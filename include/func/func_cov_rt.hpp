#include <stdint.h>

#define OUTPUT_FN "FUNC_COV_OUTPUT_FN"

struct CFuncEntry {
  const char *func_name;
  struct CFuncEntry *next;
  char is_covered;
};

struct CFileEntry {
  struct CFuncEntry *funcs[sizeof(unsigned char) * 256];
  const char *filename;
  const struct CFileEntry *next;
};

extern "C" {

// entire func list generated at compile time
// We use simple hashmap for fast lookup
// The size of the map is 256 (size of unsigned char in binary).
extern struct CFileEntry *__file_func_map[sizeof(unsigned char) * 256];

extern const uint32_t __num_funcs;

void __handle_init(int *argc_ptr, char **argv);
void __record_func_cov(const char *file_name, const char *func_name,
                       const uint32_t func_id);

static void __cov_read_prev_cov();
static void __write_cov();
void __cov_fini();
}