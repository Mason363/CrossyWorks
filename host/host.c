// Desktop harness: emulates the EADK API, scripted input, frame dumps (PPM).
#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int game_main(int argc, char *argv[]);

static uint16_t screen[EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT];
static long frame = 0, max_frames = 1;
static double now_ms = 0;
static const char *outdir = ".";
static char shots[4096] = "";
static char keys[8192] = "";
static uint32_t rng = 2463534242u;

void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) {
      int sx = r.x + x, sy = r.y + y;
      if (sx < EADK_SCREEN_WIDTH && sy < EADK_SCREEN_HEIGHT) screen[sy * EADK_SCREEN_WIDTH + sx] = p[y * r.width + x];
    }
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) screen[(r.y + y) * EADK_SCREEN_WIDTH + r.x + x] = c;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *p) { (void)r; (void)p; }
void eadk_display_draw_string(const char *t, eadk_point_t p, bool l, eadk_color_t a, eadk_color_t b) {
  (void)t; (void)p; (void)l; (void)a; (void)b;
}

static int in_list(const char *list, long f) {
  char buf[32];
  snprintf(buf, sizeof buf, ",%ld,", f);
  char tmp[4200];
  snprintf(tmp, sizeof tmp, ",%s,", list);
  return strstr(tmp, buf) != NULL;
}

static void dump(long f) {
  char path[512];
  snprintf(path, sizeof path, "%s/f%05ld.ppm", outdir, f);
  FILE *fp = fopen(path, "wb");
  fprintf(fp, "P6\n%d %d\n255\n", EADK_SCREEN_WIDTH, EADK_SCREEN_HEIGHT);
  for (int i = 0; i < EADK_SCREEN_WIDTH * EADK_SCREEN_HEIGHT; i++) {
    uint16_t c = screen[i];
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                            (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, fp);
  }
  fclose(fp);
}

bool eadk_display_wait_for_vblank(void) {
  // the previous frame is complete at this point
  if (frame > 0 && (in_list(shots, frame) || strcmp(shots, "all") == 0)) dump(frame);
  frame++;
  now_ms += 1000.0 / 60.0;
  if (frame > max_frames) exit(0);
  return true;
}

// keys: "f0-f1:key,..." where key is one of U D L R O B E
eadk_keyboard_state_t eadk_keyboard_scan(void) {
  eadk_keyboard_state_t s = 0;
  const char *p = keys;
  while (*p) {
    long a = strtol(p, (char **)&p, 10), b = a;
    if (*p == '-') b = strtol(p + 1, (char **)&p, 10);
    if (*p == ':') p++;
    char k = *p ? *p++ : 0;
    if (*p == ',') p++;
    if (frame >= a && frame <= b) {
      int key = k == 'U' ? eadk_key_up : k == 'D' ? eadk_key_down : k == 'L' ? eadk_key_left : k == 'R' ? eadk_key_right
              : k == 'O' ? eadk_key_ok : k == 'B' ? eadk_key_back : k == 'E' ? eadk_key_exe : -1;
      if (key >= 0) s |= (eadk_keyboard_state_t)1 << key;
    }
  }
  return s;
}

uint64_t eadk_timing_millis(void) { return (uint64_t)now_ms; }
void eadk_timing_msleep(uint32_t ms) { now_ms += ms; }
void eadk_timing_usleep(uint32_t us) { now_ms += us / 1000.0; }
uint32_t eadk_random(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}
bool eadk_usb_is_plugged(void) { return false; }
uint8_t eadk_battery_level(void) { return 100; }
const char *eadk_external_data = NULL;
size_t eadk_external_data_size = 0;

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) snprintf(shots, sizeof shots, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) snprintf(keys, sizeof keys, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) rng = (uint32_t)atol(argv[++i]);
  }
  char *args[] = {"crossy", NULL};
  return game_main(1, args);
}
