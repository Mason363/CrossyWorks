// Crossy Road for NumWorks
// A faithful remake of Hipster Whale's Crossy Road for the NumWorks calculator.
#include <eadk.h>
#include <stddef.h>
#include <stdint.h>

#if PLATFORM_DEVICE && !defined(HOST)
// Freestanding build: the tiny libc subset the compiler may call.
__attribute__((used, externally_visible)) void *memset(void *d, int c, size_t n) {
  uint8_t *p = d;
  if (n >= 8) {
    uint32_t w = (uint8_t)c * 0x01010101u;
    while ((uintptr_t)p & 3) { *p++ = (uint8_t)c; n--; }
    for (; n >= 4; n -= 4, p += 4) *(uint32_t *)p = w;
  }
  while (n--) *p++ = (uint8_t)c;
  return d;
}
__attribute__((used, externally_visible)) void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *p = d;
  const uint8_t *q = s;
  while (n--) *p++ = *q++;
  return d;
}
#else
#include <string.h>
#endif

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Crossy Road";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

#define SW 320
#define SH 240

// Helpers used from many places are kept out of line: smaller code.
#define NOINLINE __attribute__((noinline))

// ---------------------------------------------------------------------------
// Palette indices (the colors are in the constant data below).

#define RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

enum {
  C_GRASS, C_GRASS2, C_GRASS_O, C_GRASS2_O, C_GRASS_SH, C_GRASS_OSH,
  C_ROAD, C_ROAD_O, C_ROAD_SH, C_ROAD_OSH, C_ROAD_SIDE, C_DASH, C_DASH_O,
  C_WATER, C_WATER_O, C_WATER_SH, C_WATER_OSH, C_FOAM,
  C_RAIL, C_RAIL_S, C_TIE, C_TIE_S, C_RAIL_SH, C_TIE_SH,
  C_WHITE, C_GRAY, C_COMB, C_COMB_S, C_BEAK, C_BEAK_S, C_EYE,
  C_LEAF, C_LEAF_A, C_LEAF_B, C_TRUNK, C_TRUNK_E,
  C_ROCK, C_ROCK_S,
  C_LOG, C_LOG_BARK, C_LOG_S, C_LOG_END, C_LOG_SH,
  C_LILY, C_LILY_S,
  C_BLACK, C_CABIN_S, C_UNDER, C_HUB,
  C_BLUE_T, C_BLUE_S, C_BLUE_S2,
  C_GREEN_T, C_GREEN_S, C_GREEN_S2,
  C_YELLOW_T, C_YELLOW_S, C_YELLOW_S2,
  C_ORANGE_T, C_ORANGE_S, C_ORANGE_S2,
  C_PURPLE_T, C_PURPLE_S, C_PURPLE_S2,
  C_RED_T, C_RED_S, C_TBLUE_T, C_TBLUE_S, C_TGREEN_T, C_TGREEN_S,
  C_BOX_T, C_BOX_S, C_BOX_S2,
  C_TRAIN_T, C_TRAIN_S, C_TRAIN_S2, C_TRAIN_W, C_TRAIN_WS, C_TRAIN_Y, C_TRAIN_YS,
  C_POLE_R, C_POLE_RS, C_POLE_W, C_POLE_WS, C_LAMP_OFF, C_LAMP_ON, C_SIGNAL, C_SIGNAL_S,
  C_COIN, C_COIN_S, C_COIN_C,
  C_EAGLE, C_EAGLE_S, C_EAGLE_H, C_EAGLE_HS, C_EAGLE_B, C_EAGLE_BS,
  C_BTN, C_BTN_S, C_GOLD, C_OUTLINE,
  NCOLORS
};

// ---------------------------------------------------------------------------
// Materials: colors of the top, south and east faces of a box.

typedef struct { uint8_t t, s, e; } Mat;  // top, south, east colors

enum {
  M_WHITE, M_COMB, M_BEAK, M_EYE, M_TRUNK, M_ROCK, M_LOG, M_LOG_BARK, M_LILY,
  M_BLACK, M_CABIN, M_UNDER, M_HUB,
  M_BLUE, M_BLUE2, M_GREEN, M_GREEN2, M_YELLOW, M_YELLOW2, M_ORANGE, M_ORANGE2, M_PURPLE, M_PURPLE2,
  M_RED, M_TBLUE, M_TGREEN, M_BOX, M_BOX2,
  M_TRAIN, M_TRAIN2, M_TRAIN_W, M_TRAIN_Y,
  M_POLE_R, M_POLE_W, M_SIGNAL, M_LAMP_OFF, M_LAMP_ON,
  M_EAGLE, M_EAGLE_H, M_EAGLE_B,
  NMATS
};

// ---------------------------------------------------------------------------
// Game state. Everything lives in one block so code reaches it from a single
// base address (member order tuned for code size: hot fields get short offsets).

typedef struct { int8_t x, y, z; uint8_t w, h, d, m; } Box;  // 1/40 tile units

typedef struct { uint16_t off; uint8_t n; uint8_t reach; } Mdl;  // sorted box list
enum {
  MD_CHICKEN,                   // 4 facings
  MD_CAR = MD_CHICKEN + 4,      // 5 colors x 2 directions
  MD_TRUCK = MD_CAR + 10,       // 3 colors x 2 directions
  MD_ENGINE = MD_TRUCK + 6,     // 2 directions
  MD_WAGON = MD_ENGINE + 2,
  MD_LOG = MD_WAGON + 1,        // 2..4 tiles
  MD_LILY = MD_LOG + 3,
  MD_ROCK,
  MD_SIGNAL,                    // lamps off, left on, right on
  MD_EAGLE = MD_SIGNAL + 3,
  NMODELS
};

#define PLAY_MIN (-4)
#define PLAY_MAX 4
#define NCOLS 27  // columns -13..13 (the rest of each lane is shaded scenery)
#define COL0 13
#define NLANES 40
#define MAXOBJ 8
#define NO_COIN (-128)
#define NPARTS 40
#define STRIP 12

typedef struct {
  float x;       // center
  uint8_t kind;  // vehicle type (road) or length in tiles (river)
  int8_t coin;   // slot holding a coin (logs), -1 if none
} Obj;

typedef struct {
  int row;
  uint8_t type, nobj, alt, train;
  int8_t dir, coin, bob;
  float speed, period, timer, trainx, bobt;
  uint8_t cells[NCOLS];
  Obj obj[MAXOBJ];
} Lane;

typedef struct { float x, y, z, vx, vy, vz, life; uint8_t c, sz; } Part;

enum { L_GRASS, L_ROAD, L_RIVER, L_LILY, L_RAIL };

// Constant tables (see "Constant data" below); a copy lives in the state block.
struct Rom {
  uint16_t pal[NCOLORS];      // RGB565 of each color
  uint8_t shade_pairs[16][2]; // color, its shadowed version
  Mat mats[NMATS];
  Box shapes[101];
  uint8_t shape_at[13];
  uint8_t model_def[NMODELS][4];
  uint8_t obstacle_pct[12];
  uint8_t kinds[4];
  int8_t dx[4], dz[4];
  char font_chars[23];
  uint8_t font_w[22];
  uint16_t font[22][10];
  uint16_t hand_ol[14], hand_in[14];
  char save_name[15];
  uint32_t slots[2];
  char s_crossy[7], s_road[5], s_new_top[8];
};

struct State {
  uint16_t reach_prev;      // bits 0..8: playable columns reachable in the previous lane
  float ox, oy;             // screen position of the world origin (whole pixels)
  float lxa, lxb;           // visible x range of the lane being drawn
  float cam_x, cam_z, shake;
  float st_time, game_time, fade, restart_t;
  float eagle_x, eagle_y, eagle_z;
  int state, paused, show_logo, new_top, restarting, eagle_on;
  int score, top_score, coins;
  int gen_next, set_left, set_type, prev_dir, start_end;
  uint32_t rng_s;
  uint8_t *fs_buf;
  uint32_t fs_size;
  struct {
    float x, y, z, base;    // position; base = standing height of the surface
    int face, row, best;
    int hop;
    float t, fx, fz, fbase, tx, tz;
    int q[3], nq;
    float sx, sy, sz;       // squash / death scale
    float logoff;
    int dead, visible;
    int log;                // index of the log ridden in the current row, or -1
    float dt, idle;
    int crow, cobj;         // vehicle carrying the splatted chicken
    float cdx;
  } P;
  uint16_t fpal[NCOLORS];
  uint8_t shade[NCOLORS];
  Mdl models[NMODELS];
  Part parts[NPARTS];
  struct Rom rom;
  Lane lanes[NLANES];
  Box boxpool[420];
  uint16_t strip[SW * STRIP] __attribute__((aligned(4)));
  uint8_t fb[SW * SH] __attribute__((aligned(4)));
};

#if PLATFORM_DEVICE && !defined(HOST)
// On the calculator a reserved register holds its address.
static struct State state_mem;
register struct State *g9 __asm__("r9");
#define G (*g9)
#else
static struct State G;
#endif

#define ox G.ox
#define oy G.oy
#define lxa G.lxa
#define lxb G.lxb
#define cam_x G.cam_x
#define cam_z G.cam_z
#define shake G.shake
#define st_time G.st_time
#define game_time G.game_time
#define fade G.fade
#define restart_t G.restart_t
#define eagle_x G.eagle_x
#define eagle_y G.eagle_y
#define eagle_z G.eagle_z
#define state G.state
#define paused G.paused
#define show_logo G.show_logo
#define new_top G.new_top
#define restarting G.restarting
#define eagle_on G.eagle_on
#define score G.score
#define top_score G.top_score
#define coins G.coins
#define gen_next G.gen_next
#define set_left G.set_left
#define set_type G.set_type
#define prev_dir G.prev_dir
#define start_end G.start_end
#define reach_prev G.reach_prev
#define rng_s G.rng_s
#define fs_buf G.fs_buf
#define fs_size G.fs_size
#define P G.P
#define shade G.shade
#define fpal G.fpal
#define models G.models
#define parts G.parts
#define boxpool G.boxpool
#define lanes G.lanes
#define strip G.strip
#define fb G.fb

// ---------------------------------------------------------------------------
// Constant data

enum { M_V0 = NMATS, M_V1, M_V2 };
#define NONE 0xFF
enum { S_CHICKEN, S_CAR, S_TRUCK, S_ENGINE, S_WAGON, S_LOG, S_LILY = S_LOG + 3, S_ROCK, S_SIGNAL, S_EAGLE };

// Per model: shape | quarter turns clockwise << 4 (4: mirrored east-west), variants.
#define CAR(t, s, sign) {S_CAR, t, s, sign}, {S_CAR | 4 << 4, t, s, sign}
#define TRUCK(c) {S_TRUCK, c}, {S_TRUCK | 4 << 4, c}
// Font glyph: 10 rows of 10 pixels, stored in the top bits of 12.
#define GLYPH(a, b, c, d, e, f, g, h, i, j) {a << 2, b << 2, c << 2, d << 2, e << 2, f << 2, g << 2, h << 2, i << 2, j << 2}

#ifdef ROM_PACKED
// The calculator build stores the tables below compressed (tools/pack_rom.py).
static const uint8_t rom_packed[] = {
#include ROM_PACKED
};
#else
static const struct Rom rom_init = {
  // Palette. Colors sampled from the original game (lit top faces, ambient-only
  // side faces, and shadowed ground).
  .pal = {
    [C_GRASS] = RGB(217, 252, 130), [C_GRASS2] = RGB(208, 244, 120),
    [C_GRASS_O] = RGB(192, 235, 110), [C_GRASS2_O] = RGB(184, 227, 102),
    [C_GRASS_SH] = RGB(96, 116, 54), [C_GRASS_OSH] = RGB(84, 102, 43),
    [C_ROAD] = RGB(97, 104, 108), [C_ROAD_O] = RGB(89, 96, 100),
    [C_ROAD_SH] = RGB(40, 42, 42), [C_ROAD_OSH] = RGB(36, 38, 38),
    [C_ROAD_SIDE] = RGB(61, 64, 64), [C_DASH] = RGB(141, 151, 155), [C_DASH_O] = RGB(129, 141, 144),
    [C_WATER] = RGB(170, 252, 254), [C_WATER_O] = RGB(144, 236, 252),
    [C_WATER_SH] = RGB(75, 116, 131), [C_WATER_OSH] = RGB(64, 104, 122), [C_FOAM] = RGB(255, 255, 255),
    [C_RAIL] = RGB(170, 167, 183), [C_RAIL_S] = RGB(74, 71, 76),
    [C_TIE] = RGB(127, 85, 79), [C_TIE_S] = RGB(54, 33, 29),
    [C_RAIL_SH] = RGB(74, 71, 76), [C_TIE_SH] = RGB(54, 33, 29),
    [C_WHITE] = RGB(255, 255, 255), [C_GRAY] = RGB(144, 144, 144),
    [C_COMB] = RGB(239, 130, 131), [C_COMB_S] = RGB(117, 55, 53),
    [C_BEAK] = RGB(240, 146, 107), [C_BEAK_S] = RGB(120, 63, 43), [C_EYE] = RGB(31, 17, 13),
    [C_LEAF] = RGB(208, 234, 90), [C_LEAF_A] = RGB(100, 112, 39), [C_LEAF_B] = RGB(91, 101, 33),
    [C_TRUNK] = RGB(64, 41, 35), [C_TRUNK_E] = RGB(58, 32, 27),
    [C_ROCK] = RGB(252, 239, 254), [C_ROCK_S] = RGB(111, 103, 108),
    [C_LOG] = RGB(150, 102, 92), [C_LOG_BARK] = RGB(135, 85, 76), [C_LOG_S] = RGB(64, 41, 35),
    [C_LOG_END] = RGB(96, 62, 55), [C_LOG_SH] = RGB(66, 45, 40),
    [C_LILY] = RGB(115, 229, 135), [C_LILY_S] = RGB(46, 99, 55),
    [C_BLACK] = RGB(0, 0, 0), [C_CABIN_S] = RGB(135, 142, 144), [C_UNDER] = RGB(71, 66, 80),
    [C_HUB] = RGB(160, 166, 170),
    [C_BLUE_T] = RGB(120, 248, 253), [C_BLUE_S] = RGB(48, 107, 116), [C_BLUE_S2] = RGB(31, 78, 95),
    [C_GREEN_T] = RGB(196, 244, 124), [C_GREEN_S] = RGB(95, 114, 56), [C_GREEN_S2] = RGB(77, 102, 48),
    [C_YELLOW_T] = RGB(255, 254, 129), [C_YELLOW_S] = RGB(138, 123, 56), [C_YELLOW_S2] = RGB(118, 104, 48),
    [C_ORANGE_T] = RGB(255, 140, 84), [C_ORANGE_S] = RGB(140, 54, 34), [C_ORANGE_S2] = RGB(114, 42, 28),
    [C_PURPLE_T] = RGB(182, 160, 255), [C_PURPLE_S] = RGB(82, 64, 142), [C_PURPLE_S2] = RGB(66, 50, 118),
    [C_RED_T] = RGB(250, 92, 100), [C_RED_S] = RGB(120, 30, 38),
    [C_TBLUE_T] = RGB(64, 184, 248), [C_TBLUE_S] = RGB(8, 82, 124),
    [C_TGREEN_T] = RGB(110, 214, 198), [C_TGREEN_S] = RGB(36, 98, 90),
    [C_BOX_T] = RGB(236, 236, 240), [C_BOX_S] = RGB(128, 130, 138), [C_BOX_S2] = RGB(112, 114, 122),
    [C_TRAIN_T] = RGB(96, 200, 252), [C_TRAIN_S] = RGB(22, 116, 176), [C_TRAIN_S2] = RGB(14, 88, 142),
    [C_TRAIN_W] = RGB(200, 236, 236), [C_TRAIN_WS] = RGB(88, 118, 124),
    [C_TRAIN_Y] = RGB(252, 252, 131), [C_TRAIN_YS] = RGB(128, 124, 52),
    [C_POLE_R] = RGB(196, 60, 72), [C_POLE_RS] = RGB(89, 33, 31),
    [C_POLE_W] = RGB(238, 238, 238), [C_POLE_WS] = RGB(144, 144, 144),
    [C_LAMP_OFF] = RGB(62, 19, 24), [C_LAMP_ON] = RGB(255, 36, 36),
    [C_SIGNAL] = RGB(36, 36, 40), [C_SIGNAL_S] = RGB(14, 14, 16),
    [C_COIN] = RGB(255, 238, 70), [C_COIN_S] = RGB(176, 132, 16), [C_COIN_C] = RGB(255, 36, 16),
    [C_EAGLE] = RGB(136, 72, 64), [C_EAGLE_S] = RGB(74, 36, 34),
    [C_EAGLE_H] = RGB(250, 250, 250), [C_EAGLE_HS] = RGB(150, 150, 150),
    [C_EAGLE_B] = RGB(255, 170, 60), [C_EAGLE_BS] = RGB(140, 80, 24),
    [C_BTN] = RGB(86, 196, 248), [C_BTN_S] = RGB(36, 150, 220), [C_GOLD] = RGB(248, 232, 77),
    [C_OUTLINE] = RGB(0, 0, 0),
  },
  .shade_pairs = {
      {C_GRASS, C_GRASS_SH}, {C_GRASS2, C_GRASS_SH}, {C_GRASS_O, C_GRASS_OSH}, {C_GRASS2_O, C_GRASS_OSH},
      {C_ROAD, C_ROAD_SH}, {C_ROAD_O, C_ROAD_OSH}, {C_DASH, C_ROAD_SH}, {C_DASH_O, C_ROAD_OSH},
      {C_WATER, C_WATER_SH}, {C_WATER_O, C_WATER_OSH}, {C_FOAM, C_WATER_SH},
      {C_RAIL, C_RAIL_SH}, {C_TIE, C_TIE_SH}, {C_LOG, C_LOG_SH}, {C_LOG_BARK, C_LOG_SH}, {C_LILY, C_LILY_S},
  },
  .mats = {
    [M_WHITE] = {C_WHITE, C_GRAY, C_GRAY}, [M_COMB] = {C_COMB, C_COMB_S, C_COMB_S},
    [M_BEAK] = {C_BEAK, C_BEAK_S, C_BEAK_S}, [M_EYE] = {C_EYE, C_EYE, C_EYE},
    [M_TRUNK] = {C_TRUNK, C_TRUNK, C_TRUNK_E}, [M_ROCK] = {C_ROCK, C_ROCK_S, C_ROCK_S},
    [M_LOG] = {C_LOG, C_LOG_S, C_LOG_END}, [M_LOG_BARK] = {C_LOG_BARK, C_LOG_S, C_LOG_END},
    [M_LILY] = {C_LILY, C_LILY_S, C_LILY_S},
    [M_BLACK] = {C_BLACK, C_BLACK, C_BLACK}, [M_CABIN] = {C_WHITE, C_CABIN_S, C_CABIN_S},
    [M_UNDER] = {C_UNDER, C_UNDER, C_UNDER}, [M_HUB] = {C_HUB, C_HUB, C_HUB},
    [M_BLUE] = {C_BLUE_T, C_BLUE_S, C_BLUE_S}, [M_BLUE2] = {C_BLUE_T, C_BLUE_S2, C_BLUE_S2},
    [M_GREEN] = {C_GREEN_T, C_GREEN_S, C_GREEN_S}, [M_GREEN2] = {C_GREEN_T, C_GREEN_S2, C_GREEN_S2},
    [M_YELLOW] = {C_YELLOW_T, C_YELLOW_S, C_YELLOW_S}, [M_YELLOW2] = {C_YELLOW_T, C_YELLOW_S2, C_YELLOW_S2},
    [M_ORANGE] = {C_ORANGE_T, C_ORANGE_S, C_ORANGE_S}, [M_ORANGE2] = {C_ORANGE_T, C_ORANGE_S2, C_ORANGE_S2},
    [M_PURPLE] = {C_PURPLE_T, C_PURPLE_S, C_PURPLE_S}, [M_PURPLE2] = {C_PURPLE_T, C_PURPLE_S2, C_PURPLE_S2},
    [M_RED] = {C_RED_T, C_RED_S, C_RED_S}, [M_TBLUE] = {C_TBLUE_T, C_TBLUE_S, C_TBLUE_S},
    [M_TGREEN] = {C_TGREEN_T, C_TGREEN_S, C_TGREEN_S},
    [M_BOX] = {C_BOX_T, C_BOX_S, C_BOX_S}, [M_BOX2] = {C_BOX_T, C_BOX_S2, C_BOX_S2},
    [M_TRAIN] = {C_TRAIN_T, C_TRAIN_S, C_TRAIN_S}, [M_TRAIN2] = {C_TRAIN_T, C_TRAIN_S2, C_TRAIN_S2},
    [M_TRAIN_W] = {C_TRAIN_W, C_TRAIN_WS, C_TRAIN_WS}, [M_TRAIN_Y] = {C_TRAIN_Y, C_TRAIN_YS, C_TRAIN_YS},
    [M_POLE_R] = {C_POLE_R, C_POLE_RS, C_POLE_RS}, [M_POLE_W] = {C_POLE_W, C_POLE_WS, C_POLE_WS},
    [M_SIGNAL] = {C_SIGNAL, C_SIGNAL_S, C_SIGNAL_S},
    [M_LAMP_OFF] = {C_LAMP_OFF, C_LAMP_OFF, C_LAMP_OFF}, [M_LAMP_ON] = {C_LAMP_ON, C_LAMP_ON, C_LAMP_ON},
    [M_EAGLE] = {C_EAGLE, C_EAGLE_S, C_EAGLE_S}, [M_EAGLE_H] = {C_EAGLE_H, C_EAGLE_HS, C_EAGLE_HS},
    [M_EAGLE_B] = {C_EAGLE_B, C_EAGLE_BS, C_EAGLE_BS},
  },
  // Model shapes, facing north (creatures) or east (vehicles). Materials M_V0..M_V2
  // are filled in per model (colors, lamps); a variant of NONE drops the box.
  .shapes = {
    // chicken
    {-6, 0, -2, 3, 6, 3, M_BEAK}, {3, 0, -2, 3, 6, 3, M_BEAK},        // legs
    {-6, 0, 1, 3, 1, 2, M_BEAK}, {3, 0, 1, 3, 1, 2, M_BEAK},          // toes
    {-6, 6, -9, 12, 12, 16, M_WHITE},                                 // body
    {-9, 9, -6, 3, 6, 9, M_WHITE}, {6, 9, -6, 3, 6, 9, M_WHITE},      // wings
    {-4, 14, -12, 8, 6, 3, M_WHITE},                                  // tail
    {-6, 18, -3, 12, 14, 10, M_WHITE},                                // head
    {-2, 32, -1, 5, 4, 7, M_COMB},                                    // comb
    {-2, 24, 7, 4, 3, 3, M_BEAK},                                     // beak
    {-2, 21, 7, 4, 3, 1, M_COMB},                                     // wattle
    {-7, 26, 2, 1, 2, 2, M_EYE}, {6, 26, 2, 1, 2, 2, M_EYE},          // eyes
    // car: wheels, chassis, two-tone body, cabin with windows, taxi sign
    {-21, 0, -18, 12, 10, 4, M_BLACK}, {-21, 0, 14, 12, 10, 4, M_BLACK}, {-17, 3, -19, 4, 4, 1, M_HUB},
    {9, 0, -18, 12, 10, 4, M_BLACK}, {9, 0, 14, 12, 10, 4, M_BLACK}, {13, 3, -19, 4, 4, 1, M_HUB},
    {-25, 3, -15, 50, 5, 30, M_UNDER},
    {-27, 8, -17, 54, 7, 34, M_V1}, {-27, 15, -17, 54, 7, 34, M_V0},
    {-14, 22, -14, 26, 2, 28, M_CABIN}, {-14, 24, -14, 12, 7, 28, M_BLACK}, {-2, 24, -14, 3, 7, 28, M_CABIN},
    {1, 24, -14, 11, 7, 28, M_BLACK}, {-14, 31, -14, 26, 5, 28, M_CABIN},
    {-5, 36, -4, 8, 4, 8, M_V2},
    // truck: wheels, chassis, box, cab
    {-46, 0, -19, 13, 11, 4, M_BLACK}, {-46, 0, 15, 13, 11, 4, M_BLACK}, {-42, 4, -20, 5, 4, 1, M_HUB},
    {-30, 0, -19, 13, 11, 4, M_BLACK}, {-30, 0, 15, 13, 11, 4, M_BLACK}, {-26, 4, -20, 5, 4, 1, M_HUB},
    {30, 0, -19, 13, 11, 4, M_BLACK}, {30, 0, 15, 13, 11, 4, M_BLACK}, {34, 4, -20, 5, 4, 1, M_HUB},
    {-54, 4, -16, 108, 6, 32, M_UNDER},
    {-56, 10, -18, 80, 30, 36, M_BOX2}, {-56, 40, -18, 80, 12, 36, M_BOX},
    {26, 10, -18, 30, 14, 36, M_V0}, {28, 24, -16, 24, 9, 32, M_BLACK}, {28, 33, -16, 24, 5, 32, M_V0},
    // train engine (4 tiles)
    {-66, 0, -15, 26, 8, 30, M_BLACK}, {40, 0, -15, 26, 8, 30, M_BLACK},
    {-80, 8, -18, 154, 14, 36, M_TRAIN2}, {-80, 22, -18, 154, 12, 36, M_TRAIN_W}, {-80, 34, -18, 154, 16, 36, M_TRAIN},
    {74, 8, -18, 6, 42, 36, M_TRAIN_Y}, {60, 24, -19, 12, 8, 1, M_BLACK},
    // train wagon
    {-66, 0, -15, 26, 8, 30, M_BLACK}, {40, 0, -15, 26, 8, 30, M_BLACK},
    {-79, 8, -18, 158, 14, 36, M_TRAIN2}, {-79, 22, -18, 158, 12, 36, M_TRAIN_W}, {-79, 34, -18, 158, 16, 36, M_TRAIN},
    // logs of 2, 3 and 4 tiles with bark patches
    {-38, 0, -13, 76, 9, 26, M_LOG},
    {-34, 9, -9, 10, 1, 5, M_LOG_BARK}, {-18, 9, 3, 12, 1, 6, M_LOG_BARK},
    {6, 9, -9, 10, 1, 5, M_LOG_BARK}, {22, 9, 3, 12, 1, 6, M_LOG_BARK},
    {-58, 0, -13, 116, 9, 26, M_LOG},
    {-54, 9, -9, 10, 1, 5, M_LOG_BARK}, {-38, 9, 3, 12, 1, 6, M_LOG_BARK},
    {-14, 9, -9, 10, 1, 5, M_LOG_BARK}, {2, 9, 3, 12, 1, 6, M_LOG_BARK},
    {26, 9, -9, 10, 1, 5, M_LOG_BARK}, {42, 9, 3, 12, 1, 6, M_LOG_BARK},
    {-78, 0, -13, 156, 9, 26, M_LOG},
    {-74, 9, -9, 10, 1, 5, M_LOG_BARK}, {-58, 9, 3, 12, 1, 6, M_LOG_BARK},
    {-34, 9, -9, 10, 1, 5, M_LOG_BARK}, {-18, 9, 3, 12, 1, 6, M_LOG_BARK},
    {6, 9, -9, 10, 1, 5, M_LOG_BARK}, {22, 9, 3, 12, 1, 6, M_LOG_BARK},
    {46, 9, -9, 10, 1, 5, M_LOG_BARK}, {62, 9, 3, 12, 1, 6, M_LOG_BARK},
    // lily pad
    {-11, 0, -11, 22, 2, 22, M_LILY}, {-14, 0, -6, 3, 2, 12, M_LILY}, {11, 0, -7, 3, 2, 10, M_LILY},
    // rock
    {-15, 0, -13, 30, 14, 26, M_ROCK}, {-15, 14, -5, 22, 8, 18, M_ROCK},
    // railroad signal: striped pole, black head with two lamps
    {-2, 0, -2, 5, 4, 5, M_POLE_W}, {-2, 4, -2, 5, 10, 5, M_POLE_R}, {-2, 14, -2, 5, 4, 5, M_POLE_W},
    {-2, 18, -2, 5, 10, 5, M_POLE_R}, {-2, 28, -2, 5, 4, 5, M_POLE_W}, {-2, 32, -2, 5, 12, 5, M_POLE_R},
    {-13, 44, -3, 26, 6, 6, M_SIGNAL}, {-13, 36, -3, 8, 8, 6, M_SIGNAL}, {5, 36, -3, 8, 8, 6, M_SIGNAL},
    {-12, 37, -4, 6, 6, 1, M_V0}, {6, 37, -4, 6, 6, 1, M_V1},
    // eagle, flying south toward the camera, wings spread
    {-30, 10, -6, 22, 3, 14, M_EAGLE}, {8, 10, -6, 22, 3, 14, M_EAGLE}, {-8, 4, -12, 16, 12, 22, M_EAGLE},
    {-6, 7, 10, 12, 4, 9, M_EAGLE_H}, {-6, 10, -21, 12, 10, 9, M_EAGLE_H}, {-2, 12, -25, 4, 4, 4, M_EAGLE_B},
    {-6, 0, -8, 3, 4, 3, M_EAGLE_B}, {3, 0, -8, 3, 4, 3, M_EAGLE_B},
  },
  .shape_at = {0, 14, 29, 44, 51, 56, 61, 68, 77, 80, 82, 93, 101},
  .model_def = {
    {S_CHICKEN}, {S_CHICKEN | 1 << 4}, {S_CHICKEN | 2 << 4}, {S_CHICKEN | 3 << 4},
    CAR(M_BLUE, M_BLUE2, NONE), CAR(M_GREEN, M_GREEN2, NONE), CAR(M_YELLOW, M_YELLOW2, M_YELLOW),
    CAR(M_ORANGE, M_ORANGE2, NONE), CAR(M_PURPLE, M_PURPLE2, NONE),
    TRUCK(M_RED), TRUCK(M_TBLUE), TRUCK(M_TGREEN),
    {S_ENGINE}, {S_ENGINE | 4 << 4}, {S_WAGON},
    {S_LOG}, {S_LOG + 1}, {S_LOG + 2}, {S_LILY}, {S_ROCK},
    {S_SIGNAL, M_LAMP_OFF, M_LAMP_OFF}, {S_SIGNAL, M_LAMP_ON, M_LAMP_OFF}, {S_SIGNAL, M_LAMP_OFF, M_LAMP_ON},
    {S_EAGLE},
  },
  .obstacle_pct = {10, 10, 20, 20, 25, 25, 30, 30, 35, 40, 45, 50},  // tree density by distance (25 rows each)
  .kinds = {L_GRASS, L_ROAD, L_RIVER, L_RAIL},
  .dx = {0, 1, 0, -1},
  .dz = {1, 0, -1, 0},
  // Font: the original's blocky digits (decoded from the game) and matching
  // capitals. 10 rows; bit 11 is the leftmost column.
  .font_chars = "0123456789ACDENOPRSTWY",
  .font_w = {10, 5, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 12, 10},
  .font = {
    GLYPH(0x1FE, 0x3FF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x1FE),  // 0
    GLYPH(0x3C0, 0x3E0, 0x1E0, 0x1E0, 0x1E0, 0x1E0, 0x1E0, 0x1E0, 0x1E0, 0x1E0),  // 1
    GLYPH(0x3FE, 0x3FF, 0x00F, 0x00F, 0x1FF, 0x3FE, 0x3C0, 0x3C0, 0x3FF, 0x3FF),  // 2
    GLYPH(0x3FE, 0x3FF, 0x00F, 0x00F, 0x07E, 0x07E, 0x00F, 0x00F, 0x3FF, 0x3FE),  // 3
    GLYPH(0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x1FF, 0x00F, 0x00F, 0x00F, 0x00F),  // 4
    GLYPH(0x3FF, 0x3FF, 0x3C0, 0x3C0, 0x3FE, 0x1FF, 0x00F, 0x00F, 0x3FF, 0x3FE),  // 5
    GLYPH(0x1FF, 0x3FF, 0x3C0, 0x3C0, 0x3FE, 0x3FF, 0x3CF, 0x3CF, 0x3FF, 0x1FE),  // 6
    GLYPH(0x3FE, 0x3FF, 0x00F, 0x00F, 0x00F, 0x00F, 0x00F, 0x00F, 0x00F, 0x00F),  // 7
    GLYPH(0x1FE, 0x3FF, 0x3CF, 0x3CF, 0x1FE, 0x1FE, 0x3CF, 0x3CF, 0x3FF, 0x1FE),  // 8
    GLYPH(0x1FE, 0x3FF, 0x3CF, 0x3CF, 0x3FF, 0x1FF, 0x00F, 0x00F, 0x3FF, 0x3FE),  // 9
    GLYPH(0x1FE, 0x3FF, 0x3CF, 0x3CF, 0x3FF, 0x3FF, 0x3CF, 0x3CF, 0x3CF, 0x3CF),  // A
    GLYPH(0x1FF, 0x3FF, 0x3C0, 0x3C0, 0x3C0, 0x3C0, 0x3C0, 0x3C0, 0x3FF, 0x1FF),  // C
    GLYPH(0x3FE, 0x3FF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x3FE),  // D
    GLYPH(0x1FF, 0x3FF, 0x3C0, 0x3C0, 0x3F8, 0x3F8, 0x3C0, 0x3C0, 0x3FF, 0x1FF),  // E
    GLYPH(0x3CF, 0x3EF, 0x3FF, 0x3FF, 0x3DF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF),  // N
    GLYPH(0x1FE, 0x3FF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x1FE),  // O
    GLYPH(0x3FE, 0x3FF, 0x3CF, 0x3CF, 0x3FF, 0x3FE, 0x3C0, 0x3C0, 0x3C0, 0x3C0),  // P
    GLYPH(0x3FE, 0x3FF, 0x3CF, 0x3CF, 0x3FE, 0x3FF, 0x3CF, 0x3CF, 0x3CF, 0x3CF),  // R
    GLYPH(0x1FF, 0x3FF, 0x3C0, 0x3C0, 0x3FE, 0x1FF, 0x00F, 0x00F, 0x3FF, 0x3FE),  // S
    GLYPH(0x3FF, 0x3FF, 0x078, 0x078, 0x078, 0x078, 0x078, 0x078, 0x078, 0x078),  // T
    {0xF0F, 0xF0F, 0xF0F, 0xF6F, 0xF6F, 0xFFF, 0xFFF, 0xF9F, 0xF0F, 0xF0F},  // W
    GLYPH(0x3CF, 0x3CF, 0x3CF, 0x3CF, 0x3FF, 0x1FE, 0x078, 0x078, 0x078, 0x078),  // Y
  },
  // Pixel-art pointing hand (tap hint), 12x14: outline and fill masks.
  .hand_ol = {0x0E0, 0x1B0, 0x1B0, 0x1BE, 0x1B5, 0x7B5, 0x9D5, 0x8C1,
                                     0x801, 0x401, 0x402, 0x202, 0x1FC, 0x1FC},
  .hand_in = {0x000, 0x040, 0x040, 0x040, 0x04A, 0x04A, 0x62A, 0x73E,
                                     0x7FE, 0x3FE, 0x3FC, 0x1FC, 0x000, 0x000},
  .save_name = "crossyroad.sav",
  .slots = {0x90010000u, 0x90410000u},  // userland headers of the two firmware slots
  .s_crossy = "CROSSY",
  .s_road = "ROAD",
  .s_new_top = "NEW TOP",
};
#endif

#define pal G.rom.pal
#define shade_pairs G.rom.shade_pairs
#define mats G.rom.mats
#define shapes G.rom.shapes
#define shape_at G.rom.shape_at
#define model_def G.rom.model_def
#define obstacle_pct G.rom.obstacle_pct
#define kinds G.rom.kinds
#define DX G.rom.dx
#define DZ G.rom.dz
#define font_chars G.rom.font_chars
#define font_w G.rom.font_w
#define font G.rom.font
#define hand_ol G.rom.hand_ol
#define hand_in G.rom.hand_in
#define save_name G.rom.save_name
#define slots G.rom.slots
#define str_crossy G.rom.s_crossy
#define str_road G.rom.s_road
#define str_new_top G.rom.s_new_top

static void init_palette(void) {
  for (int i = 0; i < NCOLORS; i++) shade[i] = i;
  for (unsigned i = 0; i < sizeof(shade_pairs) / sizeof(shade_pairs[0]); i++) shade[shade_pairs[i][0]] = shade_pairs[i][1];
}

// ---------------------------------------------------------------------------
// Rasterizer: convex polygons sampled at pixel centers. c < 0 darkens (shadow).

// ceil(v - 0.5): index of the first pixel whose center is at or after v
// (a single VCVTP instruction on the Cortex-M7).
static inline int ceil_half(float v) { return (int)__builtin_lceilf(v - 0.5f); }

#ifdef HOST_STATS
long st_polys, st_lines, st_pixels;
#endif
static void raster(const float *px, const float *py, int n, int c) {
#ifdef HOST_STATS
  st_polys++;
#endif
  int it = 0, ib = 0;
  float xmin = px[0], xmax = px[0];
  for (int i = 1; i < n; i++) {
    if (py[i] < py[it]) it = i;
    if (py[i] > py[ib]) ib = i;
    if (px[i] < xmin) xmin = px[i];
    if (px[i] > xmax) xmax = px[i];
  }
  if (xmax <= 0.5f || xmin >= SW - 0.5f) return;
  int y0 = ceil_half(py[it]), y1 = ceil_half(py[ib]);
  if (y0 < 0) y0 = 0;
  if (y1 > SH) y1 = SH;
  if (y0 >= y1) return;
#ifdef HOST_STATS
  { extern long st_iters; st_iters += y1 - y0; }
#endif
  // Walk the two chains from the top vertex to the bottom one. Each edge is
  // evaluated from its upper end, so edges shared by neighbours match exactly.
  int a0 = it, a1 = it + 1 == n ? 0 : it + 1, b0 = it, b1 = it ? it - 1 : n - 1;
  float sa = 0, sb = 0, yc = y0 + 0.5f;
  int na = 1, nb = 1;  // slopes need computing
  uint8_t *row = fb + y0 * SW;
  for (int y = y0; y < y1; y++, yc += 1.0f, row += SW) {
    while (py[a1] <= yc && a1 != ib) {
      a0 = a1;
      a1 = a1 + 1 == n ? 0 : a1 + 1;
      na = 1;
    }
    while (py[b1] <= yc && b1 != ib) {
      b0 = b1;
      b1 = b1 ? b1 - 1 : n - 1;
      nb = 1;
    }
    if (na) {
      float d = py[a1] - py[a0];
      sa = d > 0 ? (px[a1] - px[a0]) / d : 0;
      na = 0;
    }
    if (nb) {
      float d = py[b1] - py[b0];
      sb = d > 0 ? (px[b1] - px[b0]) / d : 0;
      nb = 0;
    }
    float xa = px[a0] + (yc - py[a0]) * sa, xb = px[b0] + (yc - py[b0]) * sb;
    if (xa > xb) {
      float t = xa;
      xa = xb;
      xb = t;
    }
    int a = ceil_half(xa), b = ceil_half(xb);
#ifdef HOST_STATS
    { extern long st_off, st_zero, st_offc[256]; if (b <= 0 || a >= SW) { st_off++; st_offc[c & 255]++; } else if (a >= b) st_zero++; }
#endif
    if (a < 0) a = 0;
    if (b > SW) b = SW;
    if (a >= b) continue;
    uint8_t *p = row + a, *q = row + b;
#ifdef HOST_STATS
    st_lines++;
    st_pixels += b - a;
#endif
    if (c >= 0) memset(p, c, q - p);
    else
      for (; p < q; p++) *p = shade[*p];
  }
}

static void fill_rect(int x, int y, int w, int h, int c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SW) w = SW - x;
  if (y + h > SH) h = SH - y;
  if (w <= 0 || h <= 0) return;
  for (uint8_t *p = fb + y * SW + x; h--; p += SW) memset(p, c, w);
}

// ---------------------------------------------------------------------------
// Projection. World units are tiles: x east, y up, z north. Orthographic
// camera measured on screenshots of the original: yaw 19.04, pitch 42.33 deg.

#ifndef K
#define K 30.0f  // pixels per tile
#endif
#define PXX (K * 0.94528f)
#define PXY (K * 0.21969f)
#define PZX (K * 0.32622f)
#define PZY (K * -0.63658f)
#define PYY (K * -0.73924f)

static inline float sx_of(float x, float z) { return ox + PXX * x + PZX * z; }
static inline float sy_of(float x, float y, float z) { return oy + PXY * x + PZY * z + PYY * y; }

// Axis aligned box: top, south (-z) and east (+x) faces are the visible ones.
static void draw_cuboid(float x0, float y0, float z0, float x1, float y1, float z1, int ct, int cs, int ce) {
  float ax = sx_of(x0, z0), ay = sy_of(x0, y0, z0);
  float dxx = PXX * (x1 - x0), dxy = PXY * (x1 - x0);
  float dzx = PZX * (z1 - z0), dzy = PZY * (z1 - z0);
  float dy = PYY * (y1 - y0);
  float px[4], py[4];
  if (ct >= 0) {
    px[0] = ax; py[0] = ay + dy;
    px[1] = ax + dxx; py[1] = ay + dxy + dy;
    px[2] = ax + dxx + dzx; py[2] = ay + dxy + dzy + dy;
    px[3] = ax + dzx; py[3] = ay + dzy + dy;
    raster(px, py, 4, ct);
  }
  if (dy == 0) return;
  if (cs >= 0) {
    px[0] = ax; py[0] = ay;
    px[1] = ax + dxx; py[1] = ay + dxy;
    px[2] = ax + dxx; py[2] = ay + dxy + dy;
    px[3] = ax; py[3] = ay + dy;
    raster(px, py, 4, cs);
  }
  if (ce >= 0) {
    float bx = ax + dxx, by = ay + dxy;
    px[0] = bx; py[0] = by;
    px[1] = bx + dzx; py[1] = by + dzy;
    px[2] = bx + dzx; py[2] = by + dzy + dy;
    px[3] = bx; py[3] = by + dy;
    raster(px, py, 4, ce);
  }
}

// Horizontal rectangle [x0,x1]x[z0,z1] at height y (c < 0: shadow).
static void ground_quad(float x0, float x1, float z0, float z1, float y, int c) {
  float px[4], py[4];
  px[0] = sx_of(x0, z0); py[0] = sy_of(x0, y, z0);
  px[1] = sx_of(x1, z0); py[1] = sy_of(x1, y, z0);
  px[2] = sx_of(x1, z1); py[2] = sy_of(x1, y, z1);
  px[3] = sx_of(x0, z1); py[3] = sy_of(x0, y, z1);
  raster(px, py, 4, c);
}

// Vertical rectangle facing south at z, spanning [x0,x1] and [y0,y1].
static void south_quad(float x0, float x1, float y0, float y1, float z, int c) {
  float px[4], py[4];
  px[0] = px[3] = sx_of(x0, z);
  px[1] = px[2] = sx_of(x1, z);
  py[0] = sy_of(x0, y0, z); py[1] = sy_of(x1, y0, z);
  py[2] = sy_of(x1, y1, z); py[3] = sy_of(x0, y1, z);
  raster(px, py, 4, c);
}

// Vertical rectangle facing east at x, spanning [z0,z1] and [y0,y1].
static void east_quad(float x, float z0, float z1, float y0, float y1, int c) {
  float px[4], py[4];
  px[0] = px[3] = sx_of(x, z0);
  px[1] = px[2] = sx_of(x, z1);
  py[0] = sy_of(x, y0, z0); py[1] = sy_of(x, y0, z1);
  py[2] = sy_of(x, y1, z1); py[3] = sy_of(x, y1, z0);
  raster(px, py, 4, c);
}

// ---------------------------------------------------------------------------
// Box models. Units: 1/40 tile. Origin: model center, on the ground.

// Painter's order: a box goes first when it is behind (west of, north of, or
// below) another one it overlaps on screen.
static NOINLINE int behind(const Box *a, const Box *b) {
  return a->x + a->w <= b->x || a->z >= b->z + b->d || a->y + a->h <= b->y;
}

static void sort_boxes(Box *bx, int n) {
  Box out[32];
  uint32_t used = 0;
  for (int k = 0; k < n; k++)
    for (int i = 0; i < n; i++) {
      if (used >> i & 1) continue;
      int ok = 1;
      for (int j = 0; j < n && ok; j++)
        if (j != i && !(used >> j & 1) && behind(&bx[j], &bx[i]) && !behind(&bx[i], &bx[j])) ok = 0;
      if (ok) {
        out[k] = bx[i];
        used |= 1u << i;
        break;
      }
    }
  memcpy(bx, out, n * sizeof(Box));
}

static void build_models(void) {
  Box *dst = boxpool;
  for (int id = 0; id < NMODELS; id++) {
    const uint8_t *def = model_def[id];
    int s = def[0] & 15, r = def[0] >> 4, n = 0, reach = 0;
    for (int i = shape_at[s]; i < shape_at[s + 1]; i++) {
      Box b = shapes[i];
      if (b.m >= M_V0 && (b.m = def[1 + b.m - M_V0]) == NONE) continue;
      int x0 = b.x, x1 = b.x + b.w, z0 = b.z, z1 = b.z + b.d;
      if (r == 1) { b.x = z0; b.w = z1 - z0; b.z = -x1; b.d = x1 - x0; }
      else if (r == 2) { b.x = -x1; b.z = -z1; }
      else if (r == 3) { b.x = -z1; b.w = z1 - z0; b.z = x0; b.d = x1 - x0; }
      else if (r == 4) b.x = -x1;  // mirror east-west
      dst[n++] = b;
      int e = b.x < 0 ? -b.x : b.x;
      if (b.x + b.w > e) e = b.x + b.w;
      if (b.y + b.h > e) e = b.y + b.h;
      if (-b.z > e) e = -b.z;
      if (b.z + b.d > e) e = b.z + b.d;
      if (e > reach) reach = e;
    }
    sort_boxes(dst, n);
    models[id] = (Mdl){(uint16_t)(dst - boxpool), (uint8_t)n, (uint8_t)reach};
    dst += n;
  }
}

// Draw a model at a world position with per-axis scale; shapes are snapped to
// whole pixels so moving models keep a stable look.
static void draw_model_s(int id, float wx, float wy, float wz, float sx, float sy, float sz) {
  const Mdl *m = &models[id];
  float bx = sx_of(wx, wz), by = sy_of(wx, wy, wz);
  float r = m->reach * (K / 40.0f) * 1.3f;
  if (bx + r < 0 || bx - r > SW || by + r * 0.8f < 0 || by - r * 1.6f > SH) return;
  float fx = (float)(int)(bx + 16384.5f) - 16384.0f, fy = (float)(int)(by + 16384.5f) - 16384.0f;
  float sox = ox, soy = oy;
  ox += fx - bx;
  oy += fy - by;
  const float u = 1.0f / 40.0f;
  const Box *b = boxpool + m->off;
  for (int i = 0; i < m->n; i++, b++) {
    const Mat *t = &mats[b->m];
    draw_cuboid(wx + b->x * u * sx, wy + b->y * u * sy, wz + b->z * u * sz, wx + (b->x + b->w) * u * sx,
                wy + (b->y + b->h) * u * sy, wz + (b->z + b->d) * u * sz, t->t, t->s, t->e);
  }
  ox = sox;
  oy = soy;
}
static void draw_model(int id, float x, float y, float z) { draw_model_s(id, x, y, z, 1, 1, 1); }

// Shadows: light comes from the west, so a box shades the ground to its east.
#define LIGHT_SLOPE 0.9f
static void shadow_box(float x0, float x1, float z0, float z1, float ylo, float yhi, float g) {
  if (yhi <= g) return;
  if (ylo < g) ylo = g;
  ground_quad(x0 + LIGHT_SLOPE * (ylo - g), x1 + LIGHT_SLOPE * (yhi - g), z0, z1, g, -1);
}
static void shadow_model_s(int id, float wx, float wy, float wz, float g, float sx, float sy, float sz) {
  const Mdl *m = &models[id];
  float bx = sx_of(wx, wz);
  float r = m->reach * (K / 40.0f) * 2.2f;
  if (bx + r < 0 || bx - r > SW) return;
  const float u = 1.0f / 40.0f;
  const Box *b = boxpool + m->off;
  for (int i = 0; i < m->n; i++, b++)
    shadow_box(wx + b->x * u * sx, wx + (b->x + b->w) * u * sx, wz + b->z * u * sz, wz + (b->z + b->d) * u * sz,
               wy + b->y * u * sy, wy + (b->y + b->h) * u * sy, g);
}
static void shadow_model(int id, float x, float y, float z, float g) { shadow_model_s(id, x, y, z, g, 1, 1, 1); }

// Trees: trunk plus a leaf block with the original's striped layers
// (a dark band, then a light band per layer). Only visible faces are drawn.
static void draw_tree(float x, float g, float z, int layers) {
  float sx = sx_of(x, z);
  if (sx < -30 || sx > SW + 30) return;
  const float u = 1.0f / 40.0f;
  float t = 6 * u, l = 12.5f * u;
  float top = g + (10 + 5 + 15 * layers) * u;
  east_quad(x + t, z - t, z + t, g, g + 10 * u, C_TRUNK_E);
  south_quad(x - t, x + t, g, g + 10 * u, z - t, C_TRUNK);
  float y = g + 10 * u;
  for (int i = 0; i <= layers; i++) {
    float y1 = y + 5 * u;
    south_quad(x - l, x + l, y, y1, z - l, C_LEAF_B);
    east_quad(x + l, z - l, z + l, y, y1, C_LEAF_B);
    if (i == layers) break;
    float y2 = y1 + 10 * u;
    south_quad(x - l, x + l, y1, y2, z - l, C_LEAF_A);
    east_quad(x + l, z - l, z + l, y1, y2, C_LEAF_A);
    y = y2;
  }
  ground_quad(x - l, x + l, z - l, z + l, top, C_LEAF);
}

// ---------------------------------------------------------------------------
// Random numbers

static uint32_t rnd(void) {
  rng_s ^= rng_s << 13;
  rng_s ^= rng_s >> 17;
  rng_s ^= rng_s << 5;
  return rng_s;
}
static int irand(int n) { return (int)((rnd() >> 8) % (uint32_t)n); }
static float frand(float a, float b) { return a + (b - a) * (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static float fabs_(float v) { return __builtin_fabsf(v); }
static NOINLINE int iround(float v) { return (int)(v + 16384.5f) - 16384; }
static NOINLINE float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// ---------------------------------------------------------------------------
// World

enum { O_NONE, O_SHORT, O_MEDIUM, O_TALL, O_ROCK, O_PAD };

#define H_GRASS (15 / 40.0f)
#define H_ROAD (10 / 40.0f)
#define H_WATER (5 / 40.0f)
#define H_LOG (9 / 40.0f)
#define H_PAD (2 / 40.0f)

#define HOP_TIME 0.16f
#define HOP_HEIGHT 0.36f
#define CAR_HALF (27 / 40.0f)
#define TRUCK_HALF (56 / 40.0f)
#define WAGON 4.0f
#define TRAIN_CARS 7
#define TRAIN_SPEED 33.3f
#define TRAIN_WARN 2.0f
#define CHICK_HALF 0.14f
#define IDLE_EAGLE 6.0f

static NOINLINE Lane *lane_at(int row) { return &lanes[(unsigned)(row + NLANES * 1024) % NLANES]; }

static float lane_h(int type) { return type == L_GRASS ? H_GRASS : type == L_ROAD || type == L_RAIL ? H_ROAD : H_WATER; }

static int is_truck(int kind) { return kind >= 5; }
static float obj_half(const Lane *L, const Obj *o) {
  if (L->type == L_ROAD) return is_truck(o->kind) ? TRUCK_HALF : CAR_HALF;
  return o->kind * 0.5f - 0.05f;
}

// ---------------------------------------------------------------------------
// Generation: sets of lanes like the original (grass, roads, rivers, rails).

#define ALLPLAY 0x1FF


static int pick_tree(int edge) {
  int r = irand(100);
  if (edge) return r < 60 ? O_TALL : r < 80 ? O_MEDIUM : r < 90 ? O_SHORT : O_ROCK;
  return r < 10 ? O_TALL : r < 40 ? O_MEDIUM : r < 80 ? O_SHORT : O_ROCK;
}

static uint16_t spread(uint16_t seed, uint16_t fr) {
  uint16_t m = seed & fr, old;
  do {
    old = m;
    m |= ((m << 1) | (m >> 1)) & fr;
  } while (m != old);
  return m;
}

static int random_bit(uint16_t mask) {
  int n = 0;
  for (int i = 0; i < 9; i++) n += mask >> i & 1;
  int k = irand(n ? n : 1);
  for (int i = 0; i < 9; i++)
    if ((mask >> i & 1) && k-- == 0) return i;
  return 4;
}

static void new_set(int row) {
  int t, gp = row < 50 ? 90 : row < 100 ? 75 : row < 150 ? 50 : row < 200 ? 25 : 0;
  if (set_type == L_GRASS) t = kinds[1 + irand(3)];
  else if (irand(100) < gp) t = L_GRASS;
  else do t = kinds[irand(4)]; while (t == set_type);
  int n;
  switch (t) {
    case L_GRASS: {
      int r = irand(100);
      n = r < 60 ? 1 : r < 80 ? 2 : r < 90 ? 3 : r < 95 ? 4 : 5;
      break;
    }
    case L_ROAD: n = 1 + irand(row <= 25 ? 2 : row <= 50 ? 3 : row <= 100 ? 4 : row <= 150 ? 5 : 6); break;
    case L_RIVER: n = 1 + irand(row <= 25 ? 1 : row <= 50 ? 2 : row <= 100 ? 3 : row <= 150 ? 4 : 5); break;
    default: n = 1 + irand(row <= 100 ? 1 : row <= 150 ? 2 : 3); break;
  }
  set_type = t;
  set_left = n;
}

// Evenly spaced moving objects on a looping lane.
static void place_movers(Lane *L, float spacing) {
  int n = (int)(30.0f / spacing) + 1;
  if (n > MAXOBJ) n = MAXOBJ;
  L->period = n * spacing;
  L->nobj = n;
  float x0 = frand(0, spacing) - L->period * 0.5f;
  for (int i = 0; i < n; i++) L->obj[i].x = x0 + i * spacing;
}

static void gen_lane(void) {
  int row = gen_next++;
  Lane *L = lane_at(row);
  memset(L, 0, sizeof(*L));
  L->row = row;
  L->coin = NO_COIN;
  L->bob = -1;
  L->alt = row & 1;
  int type = L_GRASS, dir = 0;
  if (row > start_end) {
    if (set_left <= 0) new_set(row);
    set_left--;
    type = set_type;
    if (type == L_RIVER && irand(100) < 30) type = L_LILY;
  }
  L->type = type;
  switch (type) {
    case L_GRASS: {
      int wall = row <= -4, open = row >= -3 && row <= 0;
      int p = obstacle_pct[row / 25 > 11 ? 11 : row / 25];
      int keep = row <= 1 ? 4 : random_bit(reach_prev);
      for (int c = -NCOLS / 2; c <= NCOLS / 2; c++) {
        int edge = c < PLAY_MIN || c > PLAY_MAX, o = 0;
        if (edge) {
          if (c == PLAY_MIN - 1 || c == PLAY_MAX + 1 || irand(10)) o = pick_tree(1);
        } else if (wall) {
          o = pick_tree(1);
        } else if (!open && c - PLAY_MIN != keep && irand(100) < p) {
          o = pick_tree(0);
        }
        L->cells[COL0 + c] = (uint8_t)o;
      }
      if (row > 0) {
        uint16_t fr = 0;
        for (int i = 0; i < 9; i++)
          if (!L->cells[COL0 + PLAY_MIN + i]) fr |= 1 << i;
        reach_prev = spread(reach_prev, fr);
        if (row > 1 && irand(100) < 10) {
          int c = random_bit(fr);
          L->coin = (int8_t)(c + PLAY_MIN);
        }
      } else {
        reach_prev = ALLPLAY;
      }
      break;
    }
    case L_ROAD: {
      dir = prev_dir ? -prev_dir : (irand(2) ? 1 : -1);
      int kind = irand(8);
      float len = is_truck(kind) ? 2 * TRUCK_HALF : 2 * CAR_HALF, span = 30 + len;
      float boost = 1.0f + (row > 300 ? 300 : row) / 600.0f;
      L->speed = frand(2.0f, 3.33f) * boost;
      float gap = (float)(2 + irand(12));
      int n = (int)(span / (gap + len));
      if (n < 1) n = 1;
      if (n > MAXOBJ) n = MAXOBJ;
      place_movers(L, span / n);
      for (int i = 0; i < L->nobj; i++) L->obj[i].kind = (uint8_t)kind;
      if (irand(100) < 5) L->coin = (int8_t)(PLAY_MIN + irand(9));
      reach_prev = ALLPLAY;
      break;
    }
    case L_RIVER: {
      dir = prev_dir && irand(100) < 95 ? -prev_dir : (irand(2) ? 1 : -1);
      L->speed = frand(1.33f, 3.0f);
      place_movers(L, L->speed * frand(2.5f, 3.0f));
      float maxlen = L->period / L->nobj - 1.2f;
      for (int i = 0; i < L->nobj; i++) {
        int r = irand(100), len = r < 35 ? 2 : r < 80 ? 3 : 4;
        while (len > 2 && len > maxlen) len--;
        L->obj[i].kind = (uint8_t)len;
        L->obj[i].coin = irand(100) < 5 ? (int8_t)irand(len) : -1;
      }
      reach_prev = ALLPLAY;
      break;
    }
    case L_LILY: {
      uint16_t pads = 1 << random_bit(reach_prev);
      int n = 1;
      for (int i = 0; i < 9 && n < 3; i++) {
        if (pads >> i & 1) continue;
        if (irand(100) < ((reach_prev >> i & 1) ? 30 : 10)) {
          pads |= 1 << i;
          n++;
        }
      }
      for (int i = 0; i < 9; i++)
        if (pads >> i & 1) L->cells[COL0 + PLAY_MIN + i] = O_PAD;
      for (int c = 0; c < NCOLS; c++)
        if ((c - COL0 < PLAY_MIN || c - COL0 > PLAY_MAX) && irand(100) < 12) L->cells[c] = O_PAD;
      if (irand(100) < 5) L->coin = (int8_t)(random_bit(pads) + PLAY_MIN);
      reach_prev &= pads;
      break;
    }
    case L_RAIL: {
      dir = irand(2) ? 1 : -1;
      float wait = frand(3.0f, 8.0f);
      L->period = wait;
      L->timer = frand(0, wait) + TRAIN_WARN;
      if (irand(100) < 5) L->coin = (int8_t)(PLAY_MIN + irand(9));
      reach_prev = ALLPLAY;
      dir = 0;  // trains do not set the next lane's direction
      L->dir = irand(2) ? 1 : -1;
      break;
    }
  }
  if (type != L_RAIL) L->dir = (int8_t)dir;
  prev_dir = dir;
}

// ---------------------------------------------------------------------------
// Gameplay

enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_OVER };
enum { D_NONE, D_PANCAKE, D_SPLAT, D_TRAIN, D_DROWN, D_LOG, D_EAGLE };


static void spawn_parts(float x, float y, float z, int n, int c1, int c2, float spd, float up) {
  for (int i = 0; i < NPARTS && n > 0; i++) {
    Part *p = &parts[i];
    if (p->life > 0) continue;
    *p = (Part){x, y, z, frand(-spd, spd), frand(up * 0.5f, up), frand(-spd, spd), frand(0.6f, 1.0f),
                (uint8_t)(irand(3) ? c1 : c2), (uint8_t)(2 + irand(3))};
    n--;
  }
}

static int rail_warning(const Lane *L) { return L->train || L->timer < TRAIN_WARN; }

#ifdef HOST_GOD
static int god = 1;
#else
static const int god = 0;
#endif

static void kill(int how) {
  if (P.dead || (god && how != D_EAGLE)) return;
  P.dead = how;
  P.dt = 0;
  P.hop = 0;
  P.nq = 0;
  state = ST_DEAD;
  st_time = 0;
  if (how == D_DROWN || how == D_LOG) {
    spawn_parts(P.x, P.base - H_WATER, P.z, 20, C_FOAM, C_WATER, 1.2f, 4.5f);
    if (how == D_LOG) shake = 0.5f;
  } else if (how == D_EAGLE) {
    eagle_on = 1;
    eagle_x = P.x;
    eagle_z = P.z + 13.0f;
    eagle_y = 2.4f;
  }
}

// Surface height where the chicken would stand at (x, row).
static float stand_h(int row, int on_log) {
  const Lane *L = lane_at(row);
  if (L->type == L_RIVER) return H_WATER + (on_log ? H_LOG : 0);
  if (L->type == L_LILY) return H_WATER + H_PAD;
  return lane_h(L->type);
}

// Log covering x in lane L after time t; returns its index and the nearest slot center.
static int find_log(const Lane *L, float x, float t, float *slot) {
  for (int i = 0; i < L->nobj; i++) {
    const Obj *o = &L->obj[i];
    float cx = o->x + L->dir * L->speed * t, half = o->kind * 0.5f;
    float rel = x - cx;
    if (rel < -half - 0.45f || rel > half + 0.45f) continue;
    int idx = (int)(rel + half + 16384.0f) - 16384;
    if (idx < 0) idx = 0;
    if (idx >= o->kind) idx = o->kind - 1;
    *slot = cx - half + 0.5f + idx;
    return i;
  }
  return -1;
}

static void start_hop(int dir) {
  P.face = dir;
  int trow = P.row + DZ[dir];
  int tcol = iround(P.x + DX[dir]);
  Lane *T = lane_at(trow);
  if (tcol < PLAY_MIN || tcol > PLAY_MAX || T->row != trow ||
      (T->type == L_GRASS && T->cells[COL0 + tcol] && T->cells[COL0 + tcol] != O_PAD)) {
    P.nq = 0;  // blocked: just turn
    return;
  }
  float tx = (float)tcol;
  if (T->type == L_RIVER) {
    float slot;
    if (find_log(T, P.x + DX[dir], HOP_TIME, &slot) >= 0) tx = slot;
    else tx = P.x + DX[dir];
  }
  P.hop = 1;
  P.t = 0;
  P.fx = P.x;
  P.fz = P.z;
  P.fbase = P.base;
  P.tx = tx;
  P.tz = (float)trow;
  P.log = -1;
}

static void collect_coin(void) {
  Lane *L = lane_at(iround(P.z));
  int col = iround(P.x);
  if (L->coin != NO_COIN && L->coin == col && fabs_(P.x - col) < 0.4f && fabs_(P.z - L->row) < 0.4f) {
    L->coin = NO_COIN;
    coins++;
    spawn_parts(col, P.base + 0.3f, L->row, 8, C_GOLD, C_COIN, 1.0f, 3.0f);
  }
  if (L->type == L_RIVER && P.log >= 0) {
    Obj *o = &L->obj[P.log];
    if (o->coin >= 0 && fabs_(o->x - o->kind * 0.5f + 0.5f + o->coin - P.x) < 0.4f) {
      o->coin = -1;
      coins++;
      spawn_parts(P.x, P.base + 0.3f, L->row, 8, C_GOLD, C_COIN, 1.0f, 3.0f);
    }
  }
}

static void land(void) {
  P.hop = 0;
  P.y = 0;
  P.x = P.tx;
  P.z = P.tz;
  P.row = iround(P.tz);
  Lane *L = lane_at(P.row);
  P.base = stand_h(P.row, 0);
  if (L->type == L_RIVER) {
    float slot;
    int i = find_log(L, P.x, 0, &slot);
    if (i < 0) {
      kill(D_DROWN);
      return;
    }
    P.log = i;
    P.x = slot;
    P.logoff = slot - L->obj[i].x;
    P.base = stand_h(P.row, 1);
    L->bob = (int8_t)i;
    L->bobt = 0;
  } else if (L->type == L_LILY) {
    int col = iround(P.x);
    if (L->cells[COL0 + col] != O_PAD) {
      kill(D_DROWN);
      return;
    }
    L->bob = (int8_t)col;
    L->bobt = 0;
  }
  collect_coin();
  P.sy = 0.8f;
  P.sx = P.sz = 1.1f;
  if (P.nq) {
    int d = P.q[0];
    P.nq--;
    for (int i = 0; i < P.nq; i++) P.q[i] = P.q[i + 1];
    start_hop(d);
  }
}

static float train_len(void) { return WAGON * TRAIN_CARS; }

static NOINLINE void update_lanes(float dt) {
  int r0 = (int)cam_z - 8, r1 = (int)cam_z + 14;
  for (int r = r0; r <= r1; r++) {
    Lane *L = lane_at(r);
    if (L->row != r) continue;
    if (L->bob >= 0) {
      L->bobt += dt;
      if (L->bobt > 0.3f) L->bob = -1;
    }
    if (L->type == L_ROAD || L->type == L_RIVER) {
      float v = L->dir * L->speed * dt, h = L->period * 0.5f;
      for (int i = 0; i < L->nobj; i++) {
        Obj *o = &L->obj[i];
        o->x += v;
        if (o->x > h) o->x -= L->period;
        if (o->x < -h) o->x += L->period;
      }
    } else if (L->type == L_RAIL) {
      if (L->train) {
        L->trainx += L->dir * TRAIN_SPEED * dt;
        if (fabs_(L->trainx) > 18 + train_len()) {
          L->train = 0;
          L->timer = L->period + TRAIN_WARN;
        }
      } else {
        L->timer -= dt;
        if (L->timer <= 0) {
          L->train = 1;
          L->trainx = -L->dir * 18.0f;
        }
      }
    }
  }
}

static void check_hazards(void) {
  int row = iround(P.z);
  Lane *L = lane_at(row);
  if (L->type == L_ROAD) {
    for (int i = 0; i < L->nobj; i++) {
      Obj *o = &L->obj[i];
      float half = obj_half(L, o), d = P.x - o->x;
      if (fabs_(d) >= half + CHICK_HALF) continue;
      if (P.hop && P.fz != P.tz && fabs_(d) < half - 0.1f) {
        // hopped into the side of the vehicle: flattened against it and carried away
        kill(D_SPLAT);
        P.sz = 0.12f;
        P.sx = P.sy = 1.15f;
        P.z = row + (P.fz < P.tz ? -0.47f : 0.47f);
        P.base = H_ROAD;
        P.y = 0;
      } else if (d * L->dir > 0) {
        kill(D_PANCAKE);  // run over
        P.base = H_ROAD;
        P.y = 0;
        P.z = row;
        return;
      } else {
        kill(D_SPLAT);  // hopped into its back
        P.sx = 0.12f;
        P.sy = P.sz = 1.15f;
        P.x = o->x - L->dir * (half + 0.03f);
        P.base = H_ROAD;
        P.y = 0;
        P.z = row;
      }
      P.crow = row;
      P.cobj = i;
      P.cdx = P.x - o->x;
      return;
    }
  } else if (L->type == L_RAIL && L->train) {
    float a = L->trainx, b = L->trainx - L->dir * train_len();
    if (a > b) { float t = a; a = b; b = t; }
    if (P.x > a - CHICK_HALF && P.x < b + CHICK_HALF) {
      kill(D_TRAIN);
      P.sx = 0.12f;
      P.sy = P.sz = 1.15f;
      P.z = row;
      P.base = H_ROAD;
      P.y = 0;
      P.crow = row;
      P.cdx = L->dir * 0.03f;
    }
  }
}

static void update_player(float dt) {
  if (P.hop) {
    P.t += dt / HOP_TIME;
    if (P.t >= 1) {
      land();
    } else {
      float t = P.t;
      P.x = P.fx + (P.tx - P.fx) * t;
      P.z = P.fz + (P.tz - P.fz) * t;
      P.base = P.fbase + (stand_h(iround(P.tz), lane_at(iround(P.tz))->type == L_RIVER) - P.fbase) * t;
      P.y = HOP_HEIGHT * 4 * t * (1 - t);
      P.sy = 0.8f + 0.35f * 4 * t * (1 - t);
      P.sx = P.sz = 1.1f - 0.15f * 4 * t * (1 - t);
      int r = iround(P.z);
      if (r - 0 > P.best) {
        P.best = r;
        P.idle = 0;
      }
      collect_coin();
    }
  } else {
    float k = dt * 12 > 1 ? 1 : dt * 12;
    P.sy += (1 - P.sy) * k;
    P.sx += (1 - P.sx) * k;
    P.sz = P.sx;
    if (P.log >= 0) {
      Lane *L = lane_at(P.row);
      P.x = L->obj[P.log].x + P.logoff;
      if ((P.x < PLAY_MIN - 0.5f && L->dir < 0) || (P.x > PLAY_MAX + 0.5f && L->dir > 0)) kill(D_LOG);
    }
  }
  if (P.row > P.best) {
    P.best = P.row;
    P.idle = 0;
  }
  if (!P.dead) check_hazards();
}

static void update_dead(float dt) {
  P.dt += dt;
  float k = dt * 16 > 1 ? 1 : dt * 16;
  if (P.dead == D_PANCAKE) {
    P.sy += (0.1f - P.sy) * k;
    P.sx += (1.25f - P.sx) * k;
    P.sz = P.sx;
  } else if (P.dead == D_SPLAT || P.dead == D_TRAIN) {
    Lane *L = lane_at(P.crow);
    P.x = (P.dead == D_TRAIN ? L->trainx : L->obj[P.cobj].x) + P.cdx;
  } else if (P.dead == D_DROWN || P.dead == D_LOG) {
    if (P.dead == D_LOG && P.log >= 0) P.x = lane_at(P.row)->obj[P.log].x + P.logoff;
    P.y -= dt * 3.0f;
  } else if (P.dead == D_EAGLE) {
    eagle_z -= 24.0f * dt;
    float d = eagle_z - P.z;
    if (d > 0) {
      eagle_y = 0.75f + d * 0.13f;
    } else {  // grabbed and carried away
      eagle_y += 1.2f * dt;
      P.z = eagle_z;
      P.y = eagle_y - 0.62f - P.base;
      P.x = eagle_x;
    }
    if (d < -14) eagle_on = 0;
  }
}

static void update_parts(float dt) {
  for (int i = 0; i < NPARTS; i++) {
    Part *p = &parts[i];
    if (p->life <= 0) continue;
    p->life -= dt;
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->z += p->vz * dt;
    p->vy -= 16.0f * dt;
    if (p->y < 0) p->life = 0;
  }
}

static void update_camera(float dt) {
  float ex = clampf(P.x, -2.5f, 2.5f) - cam_x;
  float vx = fabs_(ex) * 3.33f;
  if (vx > 3.33f) vx = 3.33f;
  cam_x += (ex > 0 ? 1 : -1) * (vx * dt > fabs_(ex) ? fabs_(ex) : vx * dt);
  if (state == ST_PLAY) {
    float d = P.z - cam_z;
    cam_z += (0.5f + 4.17f * clampf(d, 0, 1)) * dt;
    if (d <= 0.05f) P.idle += dt;
  }
  if (shake > 0) shake -= dt;
  while (gen_next < (int)cam_z + 16) gen_lane();
}

static void new_game(void) {
  rng_s = eadk_random() | 1;
  memset(lanes, 0, sizeof(lanes));
  memset(parts, 0, sizeof(parts));
  gen_next = -10;
  set_left = 0;
  set_type = L_GRASS;
  prev_dir = 0;
  start_end = 1 + irand(2);
  reach_prev = ALLPLAY;
  memset(&P, 0, sizeof(P));
  P.sx = P.sy = P.sz = 1;
  P.face = 0;
  P.visible = 1;
  P.log = -1;
  P.base = H_GRASS;
  cam_x = cam_z = 0;
  eagle_on = 0;
  shake = 0;
  score = 0;
  new_top = 0;
  state = ST_TITLE;
  st_time = 0;
  while (gen_next < 16) gen_lane();
}

// ---------------------------------------------------------------------------
// Drawing the world

// Lanes are drawn only over the part that is on screen ([lxa, lxb]).

static void lane_split(float z0, float z1, float y, int cin, int cout) {
  float m0 = PLAY_MIN - 0.5f, m1 = PLAY_MAX + 0.5f;
  if (lxa < m0) ground_quad(lxa, m0, z0, z1, y, cout);
  ground_quad(lxa > m0 ? lxa : m0, lxb < m1 ? lxb : m1, z0, z1, y, cin);
  if (lxb > m1) ground_quad(m1, lxb, z0, z1, y, cout);
}

static void lane_side(float z, float ylo, float yhi, int cin, int cout) {
  float m0 = PLAY_MIN - 0.5f, m1 = PLAY_MAX + 0.5f;
  if (lxa < m0) south_quad(lxa, m0, ylo, yhi, z, cout);
  south_quad(lxa > m0 ? lxa : m0, lxb < m1 ? lxb : m1, ylo, yhi, z, cin);
  if (lxb > m1) south_quad(m1, lxb, ylo, yhi, z, cout);
}

static void draw_ground(int row) {
  Lane *L = lane_at(row), *S = lane_at(row - 1);
  float z0 = row - 0.5f, z1 = row + 0.5f, h = lane_h(L->type), hs = lane_h(S->type);
  lxa = (-ox - PZX * z1) / PXX - 0.6f;
  lxb = (SW - ox - PZX * z0) / PXX + 0.6f;
  int t = L->type, c0 = (int)(lxa + 1000) - 1000, c1 = (int)(lxb + 1000) - 999;
  if (t == L_GRASS) lane_split(z0, z1, h, L->alt ? C_GRASS2 : C_GRASS, L->alt ? C_GRASS2_O : C_GRASS_O);
  else if (t == L_ROAD || t == L_RAIL) lane_split(z0, z1, h, C_ROAD, C_ROAD_O);
  else lane_split(z0, z1, h, C_WATER, C_WATER_O);
  if (h > hs) {
    if (t == L_GRASS) lane_side(z0, hs, h, C_GRASS_SH, C_GRASS_OSH);
    else lane_side(z0, hs, h, C_ROAD_SIDE, C_ROAD_SIDE);
  }
  if (t == L_ROAD && lane_at(row + 1)->type == L_ROAD) {
    // dashed divider: one tile long dashes centered on even columns
    for (int c = c0 & ~1; c <= c1; c += 2) {
      int in = c >= PLAY_MIN && c <= PLAY_MAX;
      ground_quad(c - 0.5f, c + 0.5f, z1 - 0.05f, z1 + 0.05f, h, in ? C_DASH : C_DASH_O);
    }
  } else if (t == L_RAIL) {
    for (int c = c0; c <= c1; c++)
      draw_cuboid(c + 0.34f, h, row - 0.44f, c + 0.48f, h + 0.07f, row + 0.44f, C_TIE, C_TIE_S, C_TIE_S);
    draw_cuboid(lxa, h, row - 0.36f, lxb, h + 0.13f, row - 0.23f, C_RAIL, C_RAIL_S, C_RAIL_S);
    draw_cuboid(lxa, h, row + 0.3f, lxb, h + 0.13f, row + 0.43f, C_RAIL, C_RAIL_S, C_RAIL_S);
  } else if (t == L_RIVER || t == L_LILY) {
    // froth where the river meets the edges of the playable area
    for (int s = -1; s <= 1; s += 2) {
      float ex = s * (PLAY_MAX + 0.5f);
      if (ex < lxa - 1 || ex > lxb + 1) continue;
      for (int i = 0; i < 5; i++) {
        float zz = row - 0.37f + i * 0.185f, a = game_time * (2.1f + 0.7f * i) + i * 1.7f + row;
        float w = 0.13f + 0.03f * (i & 1);
        ground_quad(ex - w, ex + w, zz - w, zz + w, h + 0.001f, C_FOAM);
        float f = a - (int)a;
        float jx = ex + 0.26f * s * (f > 0.5f ? 1.5f - f : f + 0.5f) - 0.13f * s;
        float w2 = 0.17f + 0.02f * ((i + row) % 3);
        ground_quad(jx - w2, jx + w2, zz - w2, zz + w2, h + 0.001f, C_FOAM);
      }
    }
  }
}

static float bob_of(const Lane *L, int idx) {
  if (L->bob != idx) return 0;
  float t = L->bobt;
  return t < 0.15f ? -t * (0.2f / 0.15f) : t < 0.3f ? -(0.3f - t) * (0.2f / 0.15f) : 0;
}

static void draw_coin(float x, float g, float z) {
  float sx = sx_of(x, z);
  if (sx < -10 || sx > SW + 10) return;
  float a = game_time * 3.2f;
  float ph = a - (int)a;  // 0..1 turn
  float c = ph < 0.5f ? 1 - ph * 4 : -3 + ph * 4;
  float ac = fabs_(c), w = 0.2f * ac + 0.02f, d = 0.2f * (1 - ac) + 0.02f;
  float y = g + 0.1f + 0.04f * (ph < 0.5f ? ph : 1 - ph);
  draw_cuboid(x - w, y + 0.06f, z - d, x + w, y + 0.38f, z + d, C_COIN, C_COIN_S, C_COIN_S);
  draw_cuboid(x - w * 0.7f, y, z - d, x + w * 0.7f, y + 0.44f, z + d, C_COIN, C_COIN_S, C_COIN_S);
  if (ac > 0.5f) south_quad(x - w * 0.35f, x + w * 0.35f, y + 0.14f, y + 0.3f, z - d - 0.001f, C_COIN_C);
}

// Things standing on a lane, drawn west to east. md: the model, if any.
typedef struct { float key; uint8_t kind, a, md; } Item;
enum { I_MODEL, I_TREE, I_PAD, I_LOG, I_SIGNAL, I_COIN, I_PLAYER };

static void draw_player(void) { draw_model_s(MD_CHICKEN + P.face, P.x, P.base + P.y, P.z, P.sx, P.sy, P.sz); }

static void draw_lane_objects(int row) {
  Lane *L = lane_at(row);
  float g = lane_h(L->type);
  Item it[NCOLS + MAXOBJ + 12];
  int n = 0;
  if (L->type == L_GRASS || L->type == L_LILY)
    for (int c = 0; c < NCOLS; c++) {
      int o = L->cells[c];
      if (o) it[n++] = (Item){(float)(c - COL0), (uint8_t)(o == O_ROCK ? I_MODEL : o == O_PAD ? I_PAD : I_TREE), (uint8_t)c,
                              (uint8_t)(o == O_ROCK ? MD_ROCK : MD_LILY)};
    }
  for (int i = 0; i < L->nobj; i++) {
    int k = L->obj[i].kind;
    if (L->type == L_ROAD)
      it[n++] = (Item){L->obj[i].x, I_MODEL, 0, (uint8_t)((is_truck(k) ? MD_TRUCK + (k - 5) * 2 : MD_CAR + k * 2) + (L->dir < 0))};
    else
      it[n++] = (Item){L->obj[i].x, I_LOG, (uint8_t)i, (uint8_t)(MD_LOG + k - 2)};
  }
  if (L->type == L_RAIL) {
    it[n++] = (Item){-0.4f, I_SIGNAL, 0, MD_SIGNAL};
    if (L->train)
      for (int i = 0; i < TRAIN_CARS; i++)
        it[n++] = (Item){L->trainx - L->dir * (WAGON * 0.5f + WAGON * i), I_MODEL, 0,
                         (uint8_t)(i == 0 ? MD_ENGINE + (L->dir < 0) : i == TRAIN_CARS - 1 ? MD_ENGINE + (L->dir > 0) : MD_WAGON)};
  }
  if (L->coin != NO_COIN) it[n++] = (Item){(float)L->coin, I_COIN};
  int prow = iround(P.z), flat = P.dead == D_PANCAKE;
  if (P.visible && prow == row && !flat) {
    float k = P.x + 0.001f;
    if (P.log >= 0 && P.row == row && L->obj[P.log].x + 0.002f > k) k = L->obj[P.log].x + 0.002f;
    if (P.dead == D_SPLAT) k = L->obj[P.cobj].x + (P.z < row ? 0.002f : -0.002f) + (P.sx < 0.5f ? P.cdx : 0);
    if (P.dead == D_TRAIN) k = P.x;
    it[n++] = (Item){k, I_PLAYER};
  }
  for (int i = 1; i < n; i++) {
    Item t = it[i];
    int j = i - 1;
    while (j >= 0 && it[j].key > t.key) { it[j + 1] = it[j]; j--; }
    it[j + 1] = t;
  }
  if (P.visible && prow == row && flat) draw_player();  // the pancake lies under the traffic
  // shadows on this lane's ground
  for (int i = 0; i < n; i++) {
    Item *d = &it[i];
    float x = d->key;
    if (d->kind == I_TREE) {
      float top = g + (15 + 15 * L->cells[d->a]) / 40.0f;
      shadow_box(x - 0.32f, x + 0.32f, row - 0.31f, row + 0.31f, g + 0.25f, top, g);
      shadow_box(x - 0.15f, x + 0.15f, row - 0.15f, row + 0.15f, g, g + 0.25f, g);
    } else if (d->kind == I_COIN) {
      shadow_box(x - 0.14f, x + 0.14f, row - 0.05f, row + 0.05f, g + 0.1f, g + 0.54f, g);
    } else if (d->kind == I_PLAYER) {
      shadow_model_s(MD_CHICKEN + P.face, P.x, P.base + P.y, P.z, g, P.sx, P.sy, P.sz);
    } else {
      shadow_model(d->md, x, g, row - (d->kind == I_SIGNAL ? 0.43f : 0), g);
    }
  }
  for (int i = 0; i < n; i++) {
    Item *d = &it[i];
    float x = d->key, b = 0, z = row;
    int md = d->md;
    switch (d->kind) {
      case I_TREE: draw_tree(x, g, row, L->cells[d->a]); continue;
      case I_COIN: draw_coin(x, g + (L->type == L_LILY ? H_PAD : 0), row); continue;
      case I_PLAYER:
        b = P.log >= 0 && P.row == row ? bob_of(L, P.log) : L->type == L_LILY && !P.hop ? bob_of(L, iround(P.x)) * 0.5f : 0;
        P.base += b;
        draw_player();
        P.base -= b;
        continue;
      case I_PAD: b = bob_of(L, d->a - COL0) * 0.5f; break;
      case I_LOG: b = bob_of(L, d->a); break;
      case I_SIGNAL:
        z -= 0.43f;
        if (rail_warning(L)) md += 1 + ((int)(game_time / 0.3f) & 1);
        break;
    }
    draw_model(md, x, g + b, z);
    if (d->kind == I_LOG) {
      Obj *o = &L->obj[d->a];
      if (o->coin >= 0) draw_coin(x - o->kind * 0.5f + 0.5f + o->coin, g + H_LOG + b, row);
    }
  }
}

static void draw_parts(void) {
  for (int i = 0; i < NPARTS; i++) {
    Part *p = &parts[i];
    if (p->life <= 0) continue;
    float s = p->sz / 80.0f;
    int side = p->c == C_WHITE || p->c == C_FOAM ? C_GRAY : shade[p->c];
    if (p->c == C_GOLD) side = C_COIN_S;
    draw_cuboid(p->x - s, p->y, p->z - s, p->x + s, p->y + 2 * s, p->z + s, p->c, side, side);
  }
}

static void set_camera(void) {
  float cx = cam_x, cz = cam_z;
  if (shake > 0) {
    cx += frand(-0.08f, 0.08f);
    cz += frand(-0.08f, 0.08f);
  }
  float fx = 160.0f - (PXX * cx + PZX * cz), fy = 172.0f - (PXY * cx + PZY * cz + PYY * H_GRASS);
  ox = (float)iround(fx);
  oy = (float)iround(fy);
}

static void draw_world(void) {
  set_camera();
  int top = (int)cam_z + 12, bot = (int)cam_z - 7;
  for (int row = top; row >= bot; row--) {
    draw_ground(row);
    draw_lane_objects(row);
  }
  draw_parts();
  if (eagle_on) draw_model_s(MD_EAGLE, eagle_x, eagle_y, eagle_z, 2.2f, 2.2f, 2.2f);
}

// ---------------------------------------------------------------------------
// Text, in the original's blocky font (glyphs are in the constant data).

static NOINLINE int glyph_index(char ch) {
  for (int i = 0; font_chars[i]; i++)
    if (font_chars[i] == ch) return i;
  return -1;
}

static int text_width(const char *s, int sc) {
  int w = 0;
  for (; *s; s++) {
    int g = glyph_index(*s);
    w += (g < 0 ? 4 : font_w[g] + 2) * sc;
  }
  return w - 2 * sc;
}

// Draws text as vertical runs of font pixels. Each run's rectangle is grown by
// (gl, gt, gr, gb) pixels; ext > 0 instead sweeps it up-right into a 3D slab
// (the logo's extrusion). shear: vertical offset per font column, 1/256 px.
static void draw_text_x(int x, int y, const char *s, int sc, int c, int shear, int gl, int gt, int gr, int gb, int ext) {
  int cx = 0;
  for (; *s; s++) {
    int g = glyph_index(*s);
    if (g < 0) {
      cx += 4;
      continue;
    }
    for (int col = 0; col < font_w[g]; col++) {
      int dy = ((cx + col) * sc * shear) >> 8, bit = 0x800 >> col;
      for (int row = 0; row < 10; row++) {
        if (!(font[g][row] & bit)) continue;
        int r1 = row;
        while (r1 + 1 < 10 && (font[g][r1 + 1] & bit)) r1++;
        int x0 = x + (cx + col) * sc - gl, x1 = x + (cx + col + 1) * sc + gr;
        int y0 = y + row * sc + dy - gt, y1 = y + (r1 + 1) * sc + dy + gb;
        if (ext) {
          float px[6] = {x0, x1, x1 + ext, x1 + ext, x0 + ext, x0};
          float py[6] = {y1, y1, y1 - 2 * ext, y0 - 2 * ext, y0 - 2 * ext, y0};
          raster(px, py, 6, c);
        } else {
          fill_rect(x0, y0, x1 - x0, y1 - y0, c);
        }
        row = r1;
      }
    }
    cx += font_w[g] + 2;
  }
}

static void draw_text(int x, int y, const char *s, int sc, int c, int shear) {
  draw_text_x(x, y, s, sc, c, shear, 0, 0, 0, 0, 0);
}

// Text with a black outline and a drop shadow, like the game's HUD.
static void text_ol(int x, int y, const char *s, int sc, int c) {
  draw_text_x(x, y, s, sc, C_OUTLINE, 0, 1, 1, 1, 2, 0);
  draw_text(x, y, s, sc, c, 0);
}

static void itoa_(int v, char *buf) {
  char t[12];
  int n = 0;
  do {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n) *buf++ = t[--n];
  *buf = 0;
}

// ---------------------------------------------------------------------------
// User interface

static void draw_coin_icon(int x, int y) {
  fill_rect(x + 3, y - 1, 10, 18, C_OUTLINE);
  fill_rect(x - 1, y + 3, 18, 10, C_OUTLINE);
  fill_rect(x + 1, y + 1, 14, 14, C_OUTLINE);
  fill_rect(x + 3, y + 1, 10, 14, C_COIN);
  fill_rect(x + 1, y + 3, 14, 10, C_COIN);
  fill_rect(x + 5, y + 4, 6, 8, C_COIN_C);
  fill_rect(x + 7, y + 6, 4, 4, C_COIN);
}

static void draw_coins_hud(void) {
  char buf[12];
  itoa_(coins, buf);
  int w = text_width(buf, 2);
  text_ol(SW - 28 - w, 6, buf, 2, C_GOLD);
  draw_coin_icon(SW - 22, 8);
}

// Title logo: sheared blocky letters on a black 3D slab. t: 0 at rest, <0 entering, >0 leaving.
static void draw_logo(float t) {
  int dx = (int)(t * 220), dy = (int)(t * 60);
  const int sc = 3, sh = 59;
  const char *l1 = str_crossy, *l2 = str_road;
  int w1 = text_width(l1, sc), w2 = text_width(l2, sc);
  int x1 = 160 - w1 / 2 - 8 + dx, y1 = 8 + dy;
  int x2 = x1 + (w1 - w2) / 2 + 3, y2 = y1 + 33 + (((x2 - x1) * sh) >> 8);
  draw_text_x(x1, y1, l1, sc, C_OUTLINE, sh, 2, 2, 2, 2, 9);
  draw_text_x(x2, y2, l2, sc, C_OUTLINE, sh, 2, 2, 2, 2, 9);
  draw_text(x1, y1, l1, sc, C_WHITE, sh);
  draw_text(x2, y2, l2, sc, C_WHITE, sh);
}

// Pointing hand (tap hint), from its 12x14 outline and fill masks.
static void draw_hand(int x, int y) {
  for (int r = 0; r < 14; r++)
    for (int c = 0; c < 12; c++) {
      int m = 0x800 >> c;
      if (hand_ol[r] & m) fill_rect(x + c * 2, y + r * 2, 2, 2, C_OUTLINE);
      else if (hand_in[r] & m) fill_rect(x + c * 2, y + r * 2, 2, 2, C_WHITE);
    }
}

static void draw_tap_hint(void) {
  float ph = st_time / 0.6f;
  ph -= (int)ph;
  int up = ph > 0.5f ? 4 : 0;
  int x = (int)(sx_of(P.x, P.z)) + 2, y = (int)(sy_of(P.x, P.base, P.z)) + 14 - up;
  draw_hand(x, y);
  if (up) {  // tap marks around the fingertip
    fill_rect(x + 10, y - 9, 2, 5, C_WHITE);
    fill_rect(x + 3, y - 7, 2, 2, C_WHITE);
    fill_rect(x + 5, y - 5, 2, 2, C_WHITE);
    fill_rect(x + 17, y - 7, 2, 2, C_WHITE);
    fill_rect(x + 15, y - 5, 2, 2, C_WHITE);
  }
}

static void draw_play_button(int cx, int y) {
  fill_rect(cx - 33, y + 2, 66, 42, C_OUTLINE);
  fill_rect(cx - 31, y, 62, 46, C_OUTLINE);
  fill_rect(cx - 31, y + 2, 62, 42, C_WHITE);
  fill_rect(cx - 29, y + 2, 58, 40, C_BTN);
  fill_rect(cx - 29, y + 36, 58, 6, C_BTN_S);
  for (int i = 0; i < 10; i++) fill_rect(cx - 9 + i * 2, y + 9 + i, 2, 22 - 2 * i, C_WHITE);
}

static void draw_pause(void) {
  fill_rect(140, 94, 16, 48, C_OUTLINE);
  fill_rect(164, 94, 16, 48, C_OUTLINE);
  fill_rect(142, 96, 12, 44, C_WHITE);
  fill_rect(166, 96, 12, 44, C_WHITE);
}

static void render(void) {
  draw_world();
  char buf[16];
  if (state == ST_TITLE) {
    if (show_logo) draw_logo(st_time < 0.25f ? (st_time - 0.25f) * 4 : 0);
    draw_tap_hint();
  } else {
    if (state == ST_PLAY && show_logo && st_time < 0.25f) draw_logo(st_time * 4);
    itoa_(score, buf);
    int big = state == ST_OVER ? 3 : 2;
    text_ol(6, 6, buf, big, C_WHITE);
    if (state == ST_OVER) {
      float t = clampf(st_time * 4, 0, 1);
      int x = -90 + (int)(t * 96);
      if (new_top) text_ol(x, 42, str_new_top, 1, C_WHITE);
      else {
        char tb[20] = "TOP ";
        itoa_(top_score, tb + 4);
        text_ol(x, 42, tb, 1, C_WHITE);
      }
      float u = clampf((st_time - 0.3f) * 5, 0, 1);
      draw_play_button(160, SH + 4 - (int)(u * 66));
    }
  }
  draw_coins_hud();
  if (paused) draw_pause();
}

// ---------------------------------------------------------------------------
// Persistence

// The calculator keeps its files in RAM; the userland header of the running
// firmware slot tells where. Top score and coins go in a tiny record named
// "crossyroad.sav", written only after the whole file system checks out.
#if (PLATFORM_DEVICE && !defined(HOST)) || defined(SAVE_TEST)
#define FS_MAGIC 0xEE0BDDBAu
#define SAVE_LEN 12

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// Walks all records; returns the end marker position or 0 if anything looks wrong.
static uint8_t *fs_walk(uint8_t **found) {
  uint8_t *p = fs_buf, *end = fs_buf + fs_size;
  *found = 0;
  for (;;) {
    if (p + 2 > end) return 0;
    uint32_t n = rd16(p);
    if (!n) return p;
    if (n < 4 || p + n > end) return 0;
    const char *a = (const char *)p + 2, *b = save_name;
    while (*a && *a == *b) a++, b++;
    if (!*a && !*b && n == 2 + sizeof(save_name) + SAVE_LEN) *found = p;
    p += n;
  }
}

#ifdef SAVE_TEST
static void fs_locate(void) {
  extern void *dlsym(void *, const char *);
  uint8_t *fs = dlsym((void *)0, "_ZN3Ion7Storage10FileSystem16sharedFileSystemE");
  if (!fs || *(uint32_t *)fs != FS_MAGIC) return;
  fs_buf = fs + 4;
  fs_size = 42 * 1024;
}
#else
static void fs_locate(void) {
  uint32_t me;  // any address inside this app: this code's own
  __asm__("mov %0, pc" : "=r"(me));
  for (int i = 0; i < 2; i++) {
    const volatile uint32_t *h = (const volatile uint32_t *)slots[i];
    if (h[0] != 0xDEC0EDFEu || me < h[5] || me >= h[6]) continue;
    uint32_t a = h[3], n = h[4];
    if ((a & 3) || a < 0x20000000u || a > 0x24080000u || n < 4096 || n > 0x20000 || (n & 3)) continue;
    const volatile uint32_t *st = (const volatile uint32_t *)a;
    if (st[0] != FS_MAGIC || st[1 + n / 4] != FS_MAGIC) continue;
    fs_buf = (uint8_t *)(a + 4);
    fs_size = n;
    uint8_t *f;
    if (!fs_walk(&f)) fs_buf = 0;
    return;
  }
}
#endif

static uint32_t save_sum(uint32_t a, uint32_t b) { return (a * 2654435761u) ^ (b + 0x5A17u); }

static void load_save(void) {
  fs_locate();
  uint8_t *f;
  if (!fs_buf || !fs_walk(&f) || !f) return;
  const uint8_t *d = f + 2 + sizeof(save_name);
  uint32_t v[3];
  for (int i = 0; i < 3; i++) v[i] = d[4 * i] | d[4 * i + 1] << 8 | d[4 * i + 2] << 16 | (uint32_t)d[4 * i + 3] << 24;
  if (save_sum(v[0], v[1]) != v[2] || v[0] > 1000000 || v[1] > 1000000) return;
  top_score = (int)v[0];
  coins = (int)v[1];
}

static NOINLINE void write_save(void) {
  uint8_t *f, *end;
  if (!fs_buf || !(end = fs_walk(&f))) return;
  uint32_t n = 2 + sizeof(save_name) + SAVE_LEN;
  if (!f) {  // append a new record if it fits
    if (end + n + 2 > fs_buf + fs_size) return;
    f = end;
    wr16(f + n, 0);
    memcpy(f + 2, save_name, sizeof(save_name));
    wr16(f, n);
  }
  uint32_t v[3] = {(uint32_t)top_score, (uint32_t)coins, save_sum((uint32_t)top_score, (uint32_t)coins)};
  uint8_t *d = f + 2 + sizeof(save_name);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++) d[4 * i + j] = (uint8_t)(v[i] >> (8 * j));
}
#else
static void load_save(void) {}
static void write_save(void) {}
#endif

// ---------------------------------------------------------------------------
// Display

static void present(void) {
  const uint16_t *p = pal;
  if (fade > 0) {  // fade to white
    int f = (int)(fade * 32);
    if (f > 32) f = 32;
    for (int i = 0; i < NCOLORS; i++) {
      int c = pal[i], r = c >> 11, g = (c >> 5) & 63, b = c & 31;
      r += ((31 - r) * f) >> 5;
      g += ((63 - g) * f) >> 5;
      b += ((31 - b) * f) >> 5;
      fpal[i] = (uint16_t)(r << 11 | g << 5 | b);
    }
    p = fpal;
  }
  eadk_display_wait_for_vblank();
  for (int y = 0; y < SH; y += STRIP) {
    const uint8_t *s = fb + y * SW;
    const uint32_t *s4 = (const uint32_t *)s;
    uint32_t *d4 = (uint32_t *)strip;
    for (int i = 0; i < SW * STRIP / 4; i++) {
      uint32_t v = s4[i];
      d4[2 * i] = p[v & 255] | (uint32_t)p[(v >> 8) & 255] << 16;
      d4[2 * i + 1] = p[(v >> 16) & 255] | (uint32_t)p[v >> 24] << 16;
    }
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)y, SW, STRIP}, strip);
  }
}

// ---------------------------------------------------------------------------
// Main loop

static void update(float dt) {
  game_time += dt;
  st_time += dt;
  update_lanes(dt);
  update_parts(dt);
  if (state == ST_TITLE || state == ST_PLAY) {
    update_player(dt);
    if (state == ST_PLAY && !P.dead &&
        (P.idle >= IDLE_EAGLE || P.row <= P.best - 3 || P.z < cam_z - 3.6f))
      kill(D_EAGLE);
  } else if (state == ST_DEAD || state == ST_OVER) {
    update_dead(dt);
    float wait = P.dead == D_EAGLE ? 1.1f : P.dead == D_PANCAKE ? 0.7f : 0.5f;
    if (state == ST_DEAD && st_time > wait) {
      state = ST_OVER;
      st_time = 0;
      if (score > top_score) {
        top_score = score;
        new_top = 1;
      }
      write_save();
    }
  }
  if (state == ST_PLAY) score = P.best;
  update_camera(dt);
#ifdef HOST_DEBUG
  {
    extern int printf(const char *, ...);
    static int fr;
    fr++;
    printf("f%d st%d dead%d x%.2f z%.2f y%.2f base%.2f row%d best%d hop%d t%.2f log%d vis%d cam%.2f,%.2f idle%.1f\n", fr, state, P.dead, P.x, P.z, P.y,
           P.base, P.row, P.best, P.hop, P.t, P.log, P.visible, cam_x, cam_z, P.idle);
  }
#endif
}

#ifdef ROM_PACKED
// Decoder for the packed tables (tools/pack_rom.py): per item a flag bit, then
// a literal byte or an 8-bit distance and a 4-bit length.
static void unpack(uint8_t *d, const uint8_t *s) {
  uint32_t buf = 0;
  int nb = 0;
  for (uint8_t *end = d + sizeof(struct Rom); d < end;) {
    while (nb < 16) {
      buf = buf << 8 | *s++;
      nb += 8;
    }
    if (buf >> --nb & 1) {
      unsigned v = buf >> (nb -= 12);
      const uint8_t *m = d - (v >> 4 & 255) - 1;
      for (int k = (v & 15) + 2; k--;) *d++ = *m++;
    } else {
      *d++ = (uint8_t)(buf >> (nb -= 8));
    }
  }
}
#endif

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
#if PLATFORM_DEVICE && !defined(HOST)
  struct State *caller_r9 = g9;  // r9 belongs to the caller: given back on exit
  g9 = &state_mem;
  __asm__("" : "+r"(g9));  // keep using r9, not the address it was given
#endif
#ifdef ROM_PACKED
  unpack((uint8_t *)&G.rom, rom_packed);
#else
  memcpy(&G.rom, &rom_init, sizeof(rom_init));
#endif
  init_palette();
  build_models();
  load_save();
  new_game();
  P.face = 2;
  show_logo = 1;
  uint64_t last = eadk_timing_millis(), prevk = ~(uint64_t)0;
  for (;;) {
    uint64_t k = eadk_keyboard_scan(), pr = k & ~prevk;
    prevk = k;
#define PRESSED(key) ((pr >> (key)) & 1)
    int dir = -1;
    if (PRESSED(eadk_key_up) || PRESSED(eadk_key_ok) || PRESSED(eadk_key_exe)) dir = 0;
    else if (PRESSED(eadk_key_right)) dir = 1;
    else if (PRESSED(eadk_key_down)) dir = 2;
    else if (PRESSED(eadk_key_left)) dir = 3;
    if (PRESSED(eadk_key_back)) {
      if (state == ST_PLAY && !paused) paused = 1;
      else if (paused) paused = 0;
      else break;
    }
    if (paused) {
      if (dir >= 0) paused = 0;
      dir = -1;
    }
    if (dir >= 0 && !restarting) {
      if (state == ST_TITLE) {
        state = ST_PLAY;
        st_time = 0;
      }
      if (state == ST_PLAY && !P.dead) {
        if (P.hop) {
          if (P.nq < 3) P.q[P.nq++] = dir;
        } else {
          start_hop(dir);
        }
      } else if (state == ST_OVER && st_time > 0.4f && dir == 0) {
        restarting = 1;
        restart_t = 0;
      }
    }
    uint64_t now = eadk_timing_millis();
    float dt = (float)(uint32_t)(now - last) * 0.001f;
    last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (restarting) {
      restart_t += dt;
      fade = restart_t < 0.2f ? restart_t * 5 : 2 - restart_t * 5;
      if (restart_t >= 0.2f && restarting == 1) {
        new_game();
        P.face = 0;
        show_logo = 0;
        restarting = 2;
      }
      if (restart_t >= 0.4f) {
        restarting = 0;
        fade = 0;
      }
    }
    if (!paused) {
      int steps = dt > 0.017f ? 2 : 1;
      for (int i = 0; i < steps; i++) update(dt / steps);
    }
    render();
#ifdef HOST_STATS
    { extern void stats_frame(void); stats_frame(); }
#endif
    present();
  }
#if PLATFORM_DEVICE && !defined(HOST)
  g9 = caller_r9;
#endif
  return 0;
}
