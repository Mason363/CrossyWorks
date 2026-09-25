// Renders the app icon with the game's own renderer (host build, K overridden).
#define main crossy_main
#include "../src/crossy.c"
#undef main
#include <stdio.h>

int main(void) {
  init_palette();
  memcpy(pal, base_pal, sizeof(base_pal));
  build_models();
  memset(fb, C_WATER_O, sizeof(fb));
  ox = 24; oy = 67;
  draw_model(MD_CHICKEN + 2, 0, 0, 0);
  FILE *f = fopen("output/icon_big.ppm", "wb");
  fprintf(f, "P6\n%d %d\n255\n", SW, SH);
  for (int i = 0; i < SW * SH; i++) {
    uint16_t c = pal[fb[i]];
    unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                            (unsigned char)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}
uint32_t eadk_random(void) { return 1; }
uint64_t eadk_timing_millis(void) { return 0; }
bool eadk_display_wait_for_vblank(void) { return true; }
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) { (void)r; (void)p; }
uint64_t eadk_keyboard_scan(void) { return 0; }
