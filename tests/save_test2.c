// Save code vs. a file system that already holds records (created and
// destroyed through Epsilon's own API), inside the native simulator.
#define main crossy_main
#include "../src/crossy.c"
#undef main
#include <stdio.h>
#include <dlfcn.h>

typedef int (*count_fn)(void *);
typedef size_t (*size_fn)(void *);
typedef int (*create_fn)(void *, const char *, const char *, const void *, size_t, bool);
typedef int (*destroy_all_fn)(void *);

static void dump(void) {
  uint8_t *p = fs_buf;
  while (rd16(p)) { printf("    %-20s %u\n", (char *)p + 2, rd16(p)); p += rd16(p); }
}

int main(void) {
  memcpy(&G.rom, &rom_init, sizeof(rom_init));  // as crossy_main does first
  void *fs = dlsym(RTLD_DEFAULT, "_ZN3Ion7Storage10FileSystem16sharedFileSystemE");
  count_fn nrec = (count_fn)dlsym(RTLD_DEFAULT, "_ZNK3Ion7Storage10FileSystem15numberOfRecordsEv");
  size_fn avail = (size_fn)dlsym(RTLD_DEFAULT, "_ZNK3Ion7Storage10FileSystem13availableSizeEv");
  create_fn create = (create_fn)dlsym(RTLD_DEFAULT, "_ZN3Ion7Storage10FileSystem25createRecordWithExtensionEPKcS3_PKvmb");
  static char big[20000];
  memset(big, 'x', sizeof big);
  printf("create a.py -> %d\n", create(fs, "a", "py", "print(1)\n", 10, false));
  printf("create b.py -> %d\n", create(fs, "b", "py", big, 3000, false));
  fs_locate();
  load_save();
  printf("load on foreign storage: top=%d coins=%d records=%d\n", top_score, coins, nrec(fs));
  top_score = 77; coins = 5;
  write_save();
  printf("after write records=%d avail=%zu\n", nrec(fs), avail(fs));
  printf("create c.py -> %d\n", create(fs, "c", "py", big, 5000, false));
  dump();
  top_score = coins = 0;
  load_save();
  printf("reload top=%d coins=%d\n", top_score, coins);
  // fill the storage almost completely, then a new save must not overflow
  printf("create d.py -> %d (avail %zu)\n", create(fs, "d", "py", big, avail(fs) - 20, false), avail(fs));
  top_score = 99; coins = 6;
  write_save();  // record exists: rewrite in place
  top_score = coins = 0;
  load_save();
  printf("in-place update with full storage: top=%d coins=%d records=%d avail=%zu\n", top_score, coins, nrec(fs), avail(fs));
  // simulate a missing record with a full storage: corrupt our name and try to save
  uint8_t *f; fs_walk(&f); f[2] = 'X';
  top_score = 5; write_save();
  printf("append refused when full: records=%d avail=%zu\n", nrec(fs), avail(fs));
  dump();
  return 0;
}
