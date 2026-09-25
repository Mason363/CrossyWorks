// Runs the game's save code against the real Epsilon file system inside the
// native simulator: build as a .nwb and launch with --nwb.
#define main crossy_main
#include "../src/crossy.c"
#undef main
#include <stdio.h>
#include <dlfcn.h>

typedef int (*count_fn)(void *);
typedef size_t (*size_fn)(void *);
typedef uint32_t (*sum_fn)(void *);

int main(void) {
  memcpy(&G.rom, &rom_init, sizeof(rom_init));  // as crossy_main does first
  void *fs = dlsym(RTLD_DEFAULT, "_ZN3Ion7Storage10FileSystem16sharedFileSystemE");
  count_fn nrec = (count_fn)dlsym(RTLD_DEFAULT, "_ZNK3Ion7Storage10FileSystem15numberOfRecordsEv");
  size_fn avail = (size_fn)dlsym(RTLD_DEFAULT, "_ZNK3Ion7Storage10FileSystem13availableSizeEv");
  sum_fn sum = (sum_fn)dlsym(RTLD_DEFAULT, "_ZNK3Ion7Storage10FileSystem8checksumEv");
  printf("fs=%p records=%d avail=%zu sum=%08x\n", fs, nrec(fs), avail(fs), sum(fs));
  load_save();
  printf("loaded top=%d coins=%d (fs_buf=%p)\n", top_score, coins, (void *)fs_buf);
  top_score = 123; coins = 45;
  write_save();
  printf("after write: records=%d avail=%zu sum=%08x\n", nrec(fs), avail(fs), sum(fs));
  top_score = 0; coins = 0;
  load_save();
  printf("reloaded top=%d coins=%d\n", top_score, coins);
  top_score = 4567; coins = 89;
  write_save();
  printf("after rewrite: records=%d avail=%zu sum=%08x\n", nrec(fs), avail(fs), sum(fs));
  top_score = 0; coins = 0;
  load_save();
  printf("reloaded top=%d coins=%d\n", top_score, coins);
  // dump the record list as Epsilon stores it
  uint8_t *p = fs_buf;
  while (rd16(p)) { printf("  record %-24s size %u\n", (char *)p + 2, rd16(p)); p += rd16(p); }
  return 0;
}
