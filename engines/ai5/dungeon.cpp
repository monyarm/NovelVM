/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#include "common/endian.h"
#include "common/textconsole.h"

#include "ai5/dungeon.h"
#include "ai5/gfx.h"
#include "ai5/vm.h"

namespace AI5 {

// Wall CG should be mirrored
#define VIEW_MIRRORED 0x80

// Stop drawing after this wall
#define DRAW_ORDER_END 0xff

// Maximum map size (in tiles)
#define MAX_TW 20
#define MAX_TH 20

/*
 * XXX: KABE[n].DAT is a bunch of variously-sized 4-bit indexed bitmaps
 *      concatenated together. The sizes and offsets are baked into the
 *      engine.
 */
static struct KabeEntry {
	int w;           // width of CG
	int h;           // height of CG
	unsigned offset; // offset into KABE[n].DAT
} kabeEntry[] = {
	// KABE1.DAT, KABE2.DAT
	{ 88,  480, 0x0 },
	{ 84,  440, 0x5280 },
	{ 60,  312, 0x9ab0 },
	{ 88,  432, 0xbf40 },
	{ 144, 432, 0x10980 },
	{ 84,  312, 0x18300 },
	{ 48,  312, 0x1b630 },
	{ 20,  312, 0x1d370 },
	{ 28,  480, 0x1dfa0 },
	{ 112, 480, 0x1f9e0 },
	{ 92,  368, 0x262e0 },
	{ 28,  480, 0x2a500 },
	{ 264, 480, 0x2bf40 },
	{ 112, 368, 0x3b6c0 },
	{ 28,  368, 0x40740 },
	{ 40,  368, 0x41b60 },
	{ 92,  208, 0x43820 },
	{ 116, 480, 0x45d80 },
	{ 84,  412, 0x4ca40 },
	{ 32,  268, 0x50dd8 },
	{ 80,  412, 0x51e98 },
	{ 40,  412, 0x55ef8 },
	{ 84,  412, 0x57f28 },
	{ 84,  260, 0x5c2c0 },
	{ 36,  260, 0x5ed68 },
	{ 412, 480, 0x5ffb0 },
	{ 228, 408, 0x781f0 },
	{ 92,  296, 0x837a0 },
	{ 144, 420, 0x86cd0 },
	{ 228, 480, 0x8e2f0 },
	{ 268, 288, 0x9b8b0 },
	{ 156, 256, 0xa4f70 },
	{ 44,  232, 0xa9d70 },
	{ 112, 224, 0xab160 },
	{ 36,  224, 0xae260 },
	{ 48,  240, 0xaf220 },
	{ 180, 288, 0xb08a0 },
	{ 320, 104, 0xb6de0 },
	{ 320, 168, 0xbaee0 },
	{ 320, 104, 0xc17e0 },
	{ 320, 168, 0xc58e0 },
	{ 320, 104, 0xcc1e0 },
	{ 320, 168, 0xd02e0 },
	{ 640, 104, 0xd6be0 },
	{ 640, 168, 0xdede0 },
	// KABE3.DAT
	{ 88,  480, 0x0 },
	{ 84,  440, 0x5280 },
	{ 60,  312, 0x9ab0 },
	{ 88,  432, 0xbf40 },
	{ 144, 432, 0x10980 },
	{ 84,  312, 0x18300 },
	{ 48,  312, 0x1b630 },
	{ 20,  312, 0x1d370 },
	{ 28,  480, 0x1dfa0 },
	{ 112, 480, 0x1f9e0 },
	{ 92,  368, 0x262e0 },
	{ 28,  480, 0x2a500 },
	{ 264, 480, 0x2bf40 },
	{ 112, 368, 0x3b6c0 },
	{ 28,  368, 0x40740 },
	{ 40,  368, 0x41b60 },
	{ 92,  208, 0x43820 },
	{ 116, 480, 0x45d80 },
	{ 84,  412, 0x4ca40 },
	{ 32,  268, 0x50dd8 },
	{ 80,  412, 0x51e98 },
	{ 40,  412, 0x55ef8 },
	{ 84,  412, 0x57f28 },
	{ 84,  260, 0x5c2c0 },
	{ 36,  260, 0x5ed68 },
	{ 412, 480, 0x5ffb0 },
	{ 228, 408, 0x781f0 },
	{ 92,  296, 0x837a0 },
	{ 144, 420, 0x86cd0 },
	{ 228, 480, 0x8e2f0 },
	{ 268, 288, 0x9b8b0 },
	{ 156, 256, 0xa4f70 },
	{ 44,  232, 0xa9d70 },
	{ 112, 224, 0xab160 },
	{ 36,  224, 0xae260 },
	{ 48,  240, 0xaf220 },
	{ 180, 288, 0xb08a0 },
};

/*
 * View Modes
 * ----------
 *
 * The view mode controls the order in which walls are drawn, which CGs are
 * drawn for each wall, and at which position they are drawn. All of this data
 * is hardcoded into the engine for each view mode.
 *
 * This is used to implement short (3 frame) animations when moving through the
 * dungeon.
 *
 * E.g. moving forward:
 *     [start] = VIEW_MODE_STANDARD @ tile a
 *     frame 1 = VIEW_MODE_FORWARD  @ tile a
 *     frame 2 = VIEW_MODE_BACKWARD @ tile b
 *     frame 3 = VIEW_MODE_STANDARD @ tile b
 *
 * rotating to the left:
 *     [start] = VIEW_MODE_STANDARD  @ rot a
 *     frame 1 = VIEW_MODE_ROT_LEFT  @ rot a
 *     frame 2 = VIEW_MODE_ROT_RIGHT @ rot b
 *     frame 3 = VIEW_MODE STANDARD  @ rot b
 */
enum DungeonViewMode {
	VIEW_MODE_STANDARD,  // standard view mode
	VIEW_MODE_FORWARD,   // 1/3-tile forward view
	VIEW_MODE_BACKWARD,  // 1/3-tile backward view
	VIEW_MODE_ROT_RIGHT, // 15deg clockwise rotated view
	VIEW_MODE_ROT_LEFT,  // 15deg counter-clockwise rotated view
};

/*
 * Indices into `dungeon.view`.
 */
enum DungeonViewWall {
	VIEW_1_LEFT,
	VIEW_1_FRONT,
	VIEW_2_LEFT,
	VIEW_2_FRONT,
	VIEW_2_RIGHT,
	VIEW_3_FRONT,
	VIEW_3_RIGHT,
	VIEW_5_FRONT,
	VIEW_6_LEFT,
	VIEW_6_FRONT,
	VIEW_7_LEFT,
	VIEW_7_FRONT,
	VIEW_7_RIGHT,
	VIEW_8_FRONT,
	VIEW_8_RIGHT,
	VIEW_9_FRONT,
	VIEW_11_FRONT,
	VIEW_12_LEFT,
	VIEW_12_FRONT,
	VIEW_12_RIGHT,
	VIEW_12_BACK,
	VIEW_13_FRONT,
};
#define VIEW_NR_WALLS (VIEW_13_FRONT+1)

/*
 * A map tile. A tile has 4 walls (4 bits each, packed into the `walls` member)
 * and an 8-bit ID.
 */
struct DungeonTile {
	uint16 walls;
	byte id;
	byte uk;
};

/*
 * Accessors for the `walls` member of `DungeonTile`.
 */
#define WALL_FRONT(walls) (walls >> 12)
#define WALL_RIGHT(walls) ((walls & 0x0f00) >> 8)
#define WALL_BACK(walls)  ((walls & 0x00f0) >> 4)
#define WALL_LEFT(walls)  (walls & 0xf)

/*
 * Global dungeon data.
 */
static struct Dungeon {
	int tw, th;
	byte *kabe[3];
	GfxColor pal[16];
	struct {
		DungeonDirection dir;
		unsigned x, y;
	} player;
	DungeonTile tile[MAX_TH][MAX_TW];
	byte view[VIEW_NR_WALLS];
	byte movedThroughWall;
} dungeon;

/*
 * A 'logical' CG, which may consist of multiple actual CGs drawn side-by-side.
 * See 'Wall CGs' explanation below.
 */
struct DungeonViewCg {
	unsigned x, y;
	byte kabe[8];
};

/*
 * An entry in a draw order. If a wall exists at `dungeon.view[wall]`, then it
 * is drawn and the algorithm jumps to `nextSightLine` in the draw order.
 * See 'Draw Order' explanation below.
 */
struct DungeonDrawOrderEntry {
	byte wall;
	unsigned nextSightLine;
};

/*
 * Wall CGs
 * --------
 *
 * E.g.
 *     [2] = { 0, 52, { 7, 7|M, 6|M, E } },
 *
 * Explanation:
 *     The CG for the second wall in the current draw order (see 'Draw Order'
 *     explanation below) should be drawn at (0, 52) and consists of the
 *     following CGs:
 *         * CG 7
 *         * CG 7 mirrored on the Y-axis
 *         * CG 6 mirrored on the Y-axis
 *     Each CG is drawn in order, left to right.
 */

#define M VIEW_MIRRORED
#define E DRAW_ORDER_END

// CG numbers and position for VIEW_MODE_STANDARD. Index corresponds to draw order.
static DungeonViewCg dungeonWallsStandard[] = {
	{   0,  0, { 0, E } },
	{   0,  0, { 3|M, E } },
	{   0, 52, { 7, 7|M, 6|M, E } },
	{ 552,  0, { 0|M, E } },
	{ 552,  0, { 3, E } },
	{ 552, 52, { 6, 7, 7|M, E } },
	{  88,  0, { 3, 4, 4|M, 3|M, E } },
	{  88,  0, { 1, E } },
	{  88, 52, { 5|M, E } },
	{ 468,  0, { 1|M, E } },
	{ 468, 52, { 5, E } },
	{ 172, 52, { 5, 6, 7, 7|M, 6|M, 5|M, E } },
	{ 172, 52, { 2, E } },
	{ 408, 52, { 2|M, E } }
};

// CG numbers and position for VIEW_MODE_FORWARD. Index corresponds to draw order.
static DungeonViewCg dungeonWallsForward[] = {
	{   0,   0, { 8, E } },
	{   0,   0, { 11|M, E } },
	{   0,  24, { 14|M, E } },
	{ 612,   0, { 8|M, E } },
	{ 612,   0, { 11, E } },
	{ 612,  24, { 14, E } },
	{  28,   0, { 11, 12, 12|M, 11|M, E } },
	{  28,   0, { 9, 0xff } },
	{  28,  24, { 13|M, E } },
	{  48, 104, { 16, E } },
	{ 500,   0, { 9|M, E } },
	{ 500,  24, { 13, E } },
	{ 500, 104, { 16|M, E } },
	{ 140,  24, { 13, 14, 15, 15|M, 14|M, 13|M, E } },
	{ 140,  24, { 10, E } },
	{ 140, 104, { 16|M, E } },
	{ 408,  24, { 10|M, E } },
	{ 408, 104, { 16, E } },
	{ 232, 104, { 16, 16|M, E } }
};

// CG numbers and position for VIEW_MODE_BACKWARD. Index corresponds to draw order.
static DungeonViewCg dungeonWallsBackward[] = {
	{   0,  0, { 17, E } },
	{   0,  0, { 21|M, 20|M, E } },
	{   0, 76, { 23|M, 24|M, E } },
	{ 524,  0, { 17|M, E } },
	{ 516,  0, { 20, 21, E } },
	{ 520, 76, { 24, 23, E } },
	{ 116,  0, { 20, 21, 22, 22|M, 21|M, 20|M, E } },
	{ 116,  0, { 18, E } },
	{ 116, 76, { 23|M, E } },
	{ 440,  0, { 18|M, E } },
	{ 440, 76, { 23, E } },
	{ 200, 76, { 23, 24, 24|M, 23|M, E } },
	{ 200, 72, { 19, E } },
	{ 408, 72, { 19|M, E } }
};

// CG numbers and position for VIEW_MODE_ROT_RIGHT. Index corresponds to draw order.
static DungeonViewCg dungeonWallsRotRight[] = {
	{   0,  0, { 25, E } },
	{   0, 64, { 30, E } },
	{ 176, 60, { 27, E } },
	{ 268,  0, { 28, E } },
	{ 268, 84, { 31, E } },
	{ 376, 96, { 34, E } },
	{ 412,  0, { 29, E } },
	{ 412,  4, { 26, E } },
	{ 424, 92, { 32, E } },
	{ 412, 88, { 35, E } },
	{ 460, 64, { 36, E } },
	{ 468, 96, { 33, E } }
};

// CG numbers and position for VIEW_MODE_ROT_LEFT. Index corresponds to draw order.
static DungeonViewCg dungeonWallsRotLeft[] = {
	{ 228,  0, { 25|M, E } },
	{ 368, 64, { 30|M, E } },
	{ 372, 60, { 27|M, E } },
	{ 228,  0, { 28|M, E } },
	{ 212, 84, { 31|M, E } },
	{ 228, 96, { 34|M, E } },
	{   0,  0, { 29|M, E } },
	{   0,  4, { 26|M, E } },
	{ 168, 92, { 32|M, E } },
	{ 180, 88, { 35|M, E } },
	{   0, 64, { 36|M, E } },
	{  56, 96, { 33|M, E } }
};

// CG numbers and positions of walls for each view mode.
static DungeonViewCg *dungeonWalls[] = {
	dungeonWallsStandard,
	dungeonWallsForward,
	dungeonWallsBackward,
	dungeonWallsRotRight,
	dungeonWallsRotLeft
};

// CG numbers and position of ceiling for each view mode.
static DungeonViewCg dungeonCeilings[] = {
	{ 0, 0, { 37, 37|M, E } },
	{ 0, 0, { 39, 39|M, E } },
	{ 0, 0, { 41, 41|M, E } },
	{ 0, 0, { 43|M, E } },
	{ 0, 0, { 43, E } },
};

// CG numbers and position of floor for each view mode.
static DungeonViewCg dungeonFloors[] = {
	{ 0, 312, { 38, 38|M, E } },
	{ 0, 312, { 40, 40|M, E } },
	{ 0, 312, { 42, 42|M, E } },
	{ 0, 312, { 44|M, E } },
	{ 0, 312, { 44, E } },
};

#undef E
#undef M

/*
 * Draw Order
 * ----------
 *
 * E.g.
 *     { VIEW_12_LEFT,  3 },
 *     { VIEW_11_FRONT, 3 },
 *
 * Explanation:
 *     If there's a wall at VIEW_12_LEFT, draw it, and then skip to index 3 in
 *     the draw order array (the next sight line).
 *     If there is not a wall at VIEW_12_LEFT, continue to the next entry in
 *     the draw order array (in this case, the entry for VIEW_11_FRONT).
 */

// Wall draw order for VIEW_MODE_STANDARD.
static DungeonDrawOrderEntry dungeonDrawOrderStandard[] = {
	{ VIEW_12_LEFT,   3 },
	{ VIEW_11_FRONT,  3 },
	{ VIEW_6_FRONT,   3 },
	{ VIEW_12_RIGHT,  6 },
	{ VIEW_13_FRONT,  6 },
	{ VIEW_8_FRONT,   6 },
	{ VIEW_12_FRONT,  DRAW_ORDER_END },
	{ VIEW_7_LEFT,    9 },
	{ VIEW_6_FRONT,   9 },
	{ VIEW_7_RIGHT,   11 },
	{ VIEW_8_FRONT,   11 },
	{ VIEW_7_FRONT,   DRAW_ORDER_END },
	{ VIEW_2_LEFT,    13 },
	{ VIEW_2_RIGHT,   DRAW_ORDER_END },
	{ DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_FORWARD.
static DungeonDrawOrderEntry dungeonDrawOrderForward[] = {
	{ VIEW_12_LEFT,   3 },
	{ VIEW_11_FRONT,  3 },
	{ VIEW_6_FRONT,   3 },
	{ VIEW_12_RIGHT,  6 },
	{ VIEW_13_FRONT,  6 },
	{ VIEW_8_FRONT,   6 },
	{ VIEW_12_FRONT,  DRAW_ORDER_END },
	{ VIEW_7_LEFT,    10 },
	{ VIEW_6_FRONT,   10 },
	{ VIEW_1_FRONT,   10 },
	{ VIEW_7_RIGHT,   13 },
	{ VIEW_8_FRONT,   13 },
	{ VIEW_3_FRONT,   13 },
	{ VIEW_7_FRONT,   DRAW_ORDER_END },
	{ VIEW_2_LEFT,    16 },
	{ VIEW_1_FRONT,   16 },
	{ VIEW_2_RIGHT,   18 },
	{ VIEW_3_FRONT,   18 },
	{ VIEW_2_FRONT,   DRAW_ORDER_END },
	{ DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_ROT_RIGHT.
static DungeonDrawOrderEntry dungeonDrawOrderRotRight[] = {
	{ VIEW_12_FRONT,  6 },
	{ VIEW_7_FRONT,   3 },
	{ VIEW_2_RIGHT,   3 },
	{ VIEW_7_RIGHT,   6 },
	{ VIEW_8_FRONT,   6 },
	{ VIEW_3_RIGHT,   6 },
	{ VIEW_12_RIGHT,  DRAW_ORDER_END },
	{ VIEW_13_FRONT,  DRAW_ORDER_END },
	{ VIEW_8_FRONT,   10 },
	{ VIEW_3_RIGHT,   10 },
	{ VIEW_8_RIGHT,   DRAW_ORDER_END },
	{ VIEW_9_FRONT,   DRAW_ORDER_END },
	{ DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_ROT_LEFT.
static DungeonDrawOrderEntry dungeonDrawOrderRotLeft[] = {
	{ VIEW_12_FRONT,  6 },
	{ VIEW_7_FRONT,   3 },
	{ VIEW_2_LEFT,    3 },
	{ VIEW_7_LEFT,    6 },
	{ VIEW_6_FRONT,   6 },
	{ VIEW_1_LEFT,    6 },
	{ VIEW_12_LEFT,   DRAW_ORDER_END },
	{ VIEW_11_FRONT,  DRAW_ORDER_END },
	{ VIEW_6_FRONT,   10 },
	{ VIEW_1_LEFT,    10 },
	{ VIEW_6_LEFT,    DRAW_ORDER_END },
	{ VIEW_5_FRONT,   DRAW_ORDER_END },
	{ DRAW_ORDER_END, DRAW_ORDER_END },
};

// Draw orders for each view mode.
static DungeonDrawOrderEntry *dungeonDrawOrder[] = {
	dungeonDrawOrderStandard,
	dungeonDrawOrderForward,
	dungeonDrawOrderStandard,
	dungeonDrawOrderRotRight,
	dungeonDrawOrderRotLeft,
};

/*
 * FLOOR[n].MP3 contains the map data for floor 'n'.
 */
static void dungeonLoadMp3(byte *mp3)
{
	dungeon.tw = READ_LE_UINT16(mp3 + 0);
	dungeon.th = READ_LE_UINT16(mp3 + 2);
	if (dungeon.tw > MAX_TW || dungeon.th > MAX_TH)
		error("Dungeon size too large: %ux%u", dungeon.tw, dungeon.th);

	byte *tile = mp3 + 4;
	for (int row = 0; row < dungeon.th; row++) {
		for (int col = 0; col < dungeon.tw; col++, tile += 4) {
			dungeon.tile[row][col].walls = READ_LE_UINT16(tile);
			dungeon.tile[row][col].id = tile[2];
			dungeon.tile[row][col].uk = tile[3];
		}
	}
}

/*
 * KABE.PAL contains a 4-bit indexed, BGR555 palette for dungeon CGs.
 */
static void dungeonLoadPal(byte *pal)
{
	for (int i = 0; i < 16; i++) {
		uint16 c = READ_LE_UINT16(pal + i * 2);
		dungeon.pal[i] = gfxDecodeBgr555(c);
	}
}

/*
 * Wall Rotations
 * --------------
 *
 * In canonical (North-facing) representation, a walls value of 0xABCD means:
 *     North wall = 0xA    ^
 *     East wall  = 0xB    A
 *     South wall = 0xC  D + B
 *     West wall  = 0xD    C
 *
 * These functions rotate a walls value so that 0xABCD would instead mean:
 *     Front wall = 0xA
 *     Right wall = 0xB    D
 *     Back wall  = 0xC  C + A >
 *     Left wall  = 0xD    B
 *
 * When we load the view, all walls are rotated according to the direction the
 * player is facing.
 */

static uint16 rotateWallsFacingEast(uint16 walls)
{
	return (walls >> 12) | (walls << 4);
}

static uint16 rotateWallsFacingWest(uint16 walls)
{
	return (walls << 12) | (walls >> 4);
}

static uint16 rotateWallsFacingSouth(uint16 walls)
{
	return (walls << 8) | (walls >> 8);
}

/*             FORWARD
 *
 *   +----+====+====+====+----+
 *   |    =    =    =    =    |
 *   |  0 =  1 =  2 =  3 =  4 |
 *   |    =    =    =    =    |
 * L +====+====+====+====+====+ R
 * E |    =    =    =    =    | I
 * F |  5 =  6 =  7 =  8 =  9 | G
 * T |    =    =    =    =    | H
 *   +----+====+====+====+----+ T
 *   |    |    =::::=    |    |
 *   | 10 | 11 =:12:= 13 | 14 |
 *   |    |    =::::=    |    |
 *   +----+----+====+----+----+
 *               ^^
 *             PLAYER
 *
 * This function loads a 5x3 grid of tiles around the player, in the direction
 * the player is facing. The walls within this grid that are potentially
 * visible are stored into the `dungeon.view` array (in the diagram above,
 * these walls are marked with '=' characters).
 *
 */
static void dungeonLoadView(int x, int y, DungeonDirection dir)
{
	uint16 view[15] = {0};
	switch (dir) {
	case DUNGEON_EAST:
		for (int vX = x + 2, i = 0; vX >= x; vX--) {
			for (int vY = y - 2; vY <= y + 2; vY++, i++) {
				if (vX >= dungeon.tw || vY < 0 || vY >= dungeon.th)
					continue;
				uint16 walls = dungeon.tile[vY][vX].walls;
				view[i] = rotateWallsFacingEast(walls);
			}
		}
		break;
	case DUNGEON_NORTH:
		for (int vY = y - 2, i = 0; vY <= y; vY++) {
			for (int vX = x - 2; vX <= x + 2; vX++, i++) {
				if (vY < 0 || vX < 0 || vX >= dungeon.tw)
					continue;
				view[i] = dungeon.tile[vY][vX].walls;
			}
		}
		break;
	case DUNGEON_WEST:
		for (int vX = x - 2, i = 0; vX <= x; vX++) {
			for (int vY = y + 2; vY >= y - 2; vY--, i++) {
				if (vX < 0 || vY < 0 || vY >= dungeon.th)
					continue;
				uint16 walls = dungeon.tile[vY][vX].walls;
				view[i] = rotateWallsFacingWest(walls);
			}
		}
		break;
	case DUNGEON_SOUTH:
		for (int vY = y + 2, i = 0; vY >= y; vY--) {
			for (int vX = x + 2; vX >= x - 2; vX--, i++) {
				if (vY >= dungeon.th || vX < 0 || vX >= dungeon.tw)
					continue;
				uint16 walls = dungeon.tile[vY][vX].walls;
				view[i] = rotateWallsFacingSouth(walls);
			}
		}
		break;
	}

	// load relevant walls into dungeon.view
	dungeon.view[VIEW_1_LEFT]   = WALL_LEFT(view[1]);
	dungeon.view[VIEW_1_FRONT]  = WALL_FRONT(view[1]);
	dungeon.view[VIEW_2_LEFT]   = WALL_LEFT(view[2]);
	dungeon.view[VIEW_2_FRONT]  = WALL_FRONT(view[2]);
	dungeon.view[VIEW_2_RIGHT]  = WALL_RIGHT(view[2]);
	dungeon.view[VIEW_3_FRONT]  = WALL_FRONT(view[3]);
	dungeon.view[VIEW_3_RIGHT]  = WALL_RIGHT(view[3]);
	dungeon.view[VIEW_5_FRONT]  = WALL_FRONT(view[5]);
	dungeon.view[VIEW_6_LEFT]   = WALL_LEFT(view[6]);
	dungeon.view[VIEW_6_FRONT]  = WALL_FRONT(view[6]);
	dungeon.view[VIEW_7_LEFT]   = WALL_LEFT(view[7]);
	dungeon.view[VIEW_7_FRONT]  = WALL_FRONT(view[7]);
	dungeon.view[VIEW_7_RIGHT]  = WALL_RIGHT(view[7]);
	dungeon.view[VIEW_8_FRONT]  = WALL_FRONT(view[8]);
	dungeon.view[VIEW_8_RIGHT]  = WALL_RIGHT(view[8]);
	dungeon.view[VIEW_9_FRONT]  = WALL_FRONT(view[9]);
	dungeon.view[VIEW_11_FRONT] = WALL_FRONT(view[11]);
	dungeon.view[VIEW_12_LEFT]  = WALL_LEFT(view[12]);
	dungeon.view[VIEW_12_FRONT] = WALL_FRONT(view[12]);
	dungeon.view[VIEW_12_RIGHT] = WALL_RIGHT(view[12]);
	dungeon.view[VIEW_12_BACK]  = WALL_BACK(view[12]);
	dungeon.view[VIEW_13_FRONT] = WALL_FRONT(view[13]);
	}

static void dungeonSetPosNoCheck(unsigned x, unsigned y, DungeonDirection dir)
{
	dungeon.player.x = x;
	dungeon.player.y = y;
	dungeon.player.dir = dir;
	dungeonLoadView(x, y, dir);
}

void dungeonLoad(byte *mp3, byte *kabe1, byte *kabe2, byte *kabe3,
		byte *kabePal, byte *dunA6)
{
	dungeonLoadMp3(mp3);
	dungeonLoadPal(kabePal);
	dungeon.kabe[0] = kabe1;
	dungeon.kabe[1] = kabe2;
	dungeon.kabe[2] = kabe3;
	dungeonSetPosNoCheck(0, 0, DUNGEON_EAST);
}

void dungeonSetPos(unsigned x, unsigned y, DungeonDirection dir)
{
	if (x >= (unsigned)dungeon.tw || y >= (unsigned)dungeon.th)
		error("Invalid position: %u,%u", x, y);
	if (dir >= 4)
		error("Invalid direction: %d", dir);
	dungeonSetPosNoCheck(x, y, dir);
}

void dungeonGetPos(uint16 *x, uint16 *y, uint16 *dir)
{
	*x = dungeon.player.x;
	*y = dungeon.player.y;
	*dir = dungeon.player.dir;
}

static void dungeonDrawCg(GfxSurface *dst, unsigned x, unsigned y, byte wallId,
		unsigned wallType)
{

	bool mirrored = wallId & VIEW_MIRRORED;
	wallId &= ~VIEW_MIRRORED;
	if (wallId >= ARRAYSIZE(kabeEntry))
		error("Invalid wall ID: %u", (unsigned)wallId);

	// get CG archive
	byte *kabeDat = nullptr;
	if (wallType == 0) {
		kabeDat = dungeon.kabe[0];
	} else if (wallType == 1) {
		kabeDat = dungeon.kabe[1];
	} else if (wallType == 2) {
		kabeDat = dungeon.kabe[2];
		wallId += 45;
	} else {
		error("Invalid wall type: %u", wallType);
	}

	KabeEntry *kabe = &kabeEntry[wallId];
	byte *src = kabeDat + kabe->offset;

	// XXX: CG data is 4-bit indexed bitmap
	if (!mirrored) {
		for (int row = 0; row < kabe->h; row++) {
			byte *p = dst->pixels + (y + row) * dst->pitch + x * 3;
			for (int col = 0; col < kabe->w; col += 2, p += 6, src++) {
				GfxColor *c1 = &dungeon.pal[*src >> 4];
				GfxColor *c2 = &dungeon.pal[*src & 0xf];
				p[0] = c1->r;
				p[1] = c1->g;
				p[2] = c1->b;
				p[3] = c2->r;
				p[4] = c2->g;
				p[5] = c2->b;
			}
		}
	} else {
		for (int row = 0; row < kabe->h; row++) {
			byte *p = dst->pixels + (y + row) * dst->pitch + x * 3;
			p += (kabe->w - 2) * 3;
			for (int col = kabe->w - 2; col >= 0; col -= 2, p -= 6, src++) {
				GfxColor *c1 = &dungeon.pal[*src >> 4];
				GfxColor *c2 = &dungeon.pal[*src & 0xf];
				p[0] = c2->r;
				p[1] = c2->g;
				p[2] = c2->b;
				p[3] = c1->r;
				p[4] = c1->g;
				p[5] = c1->b;
			}
		}
	}
}

/*
 * Draw a (logical) CG. Many walls consist of multiple CGs, drawn from left to
 * right.
 */
static void dungeonDrawViewCg(GfxSurface *dst, DungeonViewCg *cg,
		unsigned wallType)
{
	unsigned x = cg->x;
	for (int i = 0; cg->kabe[i] != DRAW_ORDER_END; i++) {
		dungeonDrawCg(dst, x, cg->y, cg->kabe[i], wallType);
		x += kabeEntry[cg->kabe[i] & ~VIEW_MIRRORED].w;
	}
}

static DungeonSpeed dungeonSpeed = DUNGEON_NORMAL;
static int moveFrameTime[] = {
	5,
	25,
	50
};

void dungeonSetMoveSpeed(DungeonSpeed speed)
{
	dungeonSpeed = speed;
}

DungeonSpeed dungeonGetMoveSpeed()
{
	return dungeonSpeed;
}

static void dungeonDrawView(DungeonViewMode mode)
{
	//
	VMTimer timer = vmTimerCreate();

	uint16 dstI = memGetSysvar16(kSysVarDstSurface);
	GfxSurface *dst = gfxGetSurface(dstI);
	memset(dst->pixels, 0, dst->pitch * dst->h);


	dungeonDrawViewCg(dst, &dungeonCeilings[mode], 0);
	dungeonDrawViewCg(dst, &dungeonFloors[mode], 0);

	DungeonDrawOrderEntry *drawOrder = dungeonDrawOrder[mode];
	DungeonViewCg *wallCg = dungeonWalls[mode];
	for (int i = 0; drawOrder[i].wall != DRAW_ORDER_END;) {
		byte wall = dungeon.view[drawOrder[i].wall] & 7;
		if (wall) {
			// draw
			dungeonDrawViewCg(dst, &wallCg[i], wall - 1);
			if (drawOrder[i].nextSightLine == DRAW_ORDER_END)
				break;
			// skip to next sight line
			i = drawOrder[i].nextSightLine;
		} else {
			// check next wall in sight line
			i++;
		}
	}


	gfxWholeSurfaceDirty(dstI);
	vmTimerTick(timer, moveFrameTime[dungeonSpeed]);
	gfxUpdate();
}

void dungeonDraw()
{
	dungeonDrawView(VIEW_MODE_STANDARD);
}

static void dungeonMoveForward()
{
	dungeon.movedThroughWall = dungeon.view[VIEW_12_FRONT];
	dungeonDrawView(VIEW_MODE_FORWARD);
	switch (dungeon.player.dir) {
	case DUNGEON_NORTH: dungeon.player.y -= 1; break;
	case DUNGEON_SOUTH: dungeon.player.y += 1; break;
	case DUNGEON_WEST:  dungeon.player.x -= 1; break;
	case DUNGEON_EAST:  dungeon.player.x += 1; break;
	}
	dungeonLoadView(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeonDrawView(VIEW_MODE_BACKWARD);
	dungeonDrawView(VIEW_MODE_STANDARD);
}

static void dungeonMoveBackward()
{
	dungeon.movedThroughWall = dungeon.view[VIEW_12_BACK];
	dungeonDrawView(VIEW_MODE_BACKWARD);
	switch (dungeon.player.dir) {
	case DUNGEON_NORTH: dungeon.player.y += 1; break;
	case DUNGEON_SOUTH: dungeon.player.y -= 1; break;
	case DUNGEON_WEST:  dungeon.player.x += 1; break;
	case DUNGEON_EAST:  dungeon.player.x -= 1; break;
	}
	dungeonLoadView(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeonDrawView(VIEW_MODE_FORWARD);
	dungeonDrawView(VIEW_MODE_STANDARD);
}

static void dungeonRotateLeft()
{
	dungeonDrawView(VIEW_MODE_ROT_LEFT);
	dungeon.player.dir = (DungeonDirection)((dungeon.player.dir + 1) % 4);
	dungeonLoadView(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeonDrawView(VIEW_MODE_ROT_RIGHT);
	dungeonDrawView(VIEW_MODE_STANDARD);
}

static void dungeonRotateRight()
{
	dungeonDrawView(VIEW_MODE_ROT_RIGHT);
	dungeon.player.dir = (DungeonDirection)((dungeon.player.dir + 3) % 4);
	dungeonLoadView(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeonDrawView(VIEW_MODE_ROT_LEFT);
	dungeonDrawView(VIEW_MODE_STANDARD);
}

uint16 dungeonMove(DungeonMoveCommand cmd)
{
	switch (cmd) {
	case DUNGEON_MOVE_FORWARD:
		if (dungeon.view[VIEW_12_FRONT] & 0x8) {
			dungeonDrawView(VIEW_MODE_FORWARD);
			dungeonDrawView(VIEW_MODE_STANDARD);
			return 0xff01;
		}
		dungeonMoveForward();
		break;
	case DUNGEON_MOVE_BACKWARD:
		// XXX: backing into doorway is not allowed; must be null wall behind
		if (dungeon.view[VIEW_12_BACK]) {
			dungeonDrawView(VIEW_MODE_BACKWARD);
			dungeonDrawView(VIEW_MODE_STANDARD);
			return 0xff01;
		}
		dungeonMoveBackward();
		break;
	case DUNGEON_ROTATE_LEFT:
		dungeonRotateLeft();
		break;
	case DUNGEON_ROTATE_RIGHT:
		dungeonRotateRight();
		break;
	default:
		error("Invalid movement command: %d", cmd);
	}

	byte id = dungeon.tile[dungeon.player.y][dungeon.player.x].id;
	if (id)
		return ((uint16)id << 8) | 0xff;
	if ((dungeon.movedThroughWall & 0x7) == 3) {
		// XXX: moved through a door, but no ID set on destination tile
		return 0xff02;
	}
	if ((dungeon.movedThroughWall & 0x7) == 1) {
		// XXX: moved through a window but no ID set on destination tile
		return 0xff03;
	}
	return 0;
}

} // End of namespace AI5
