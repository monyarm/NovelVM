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

#include "common/array.h"
#include "common/debug.h"
#include "common/ptr.h"
#include "common/textconsole.h"

#include "ai5/asset.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/map.h"
#include "ai5/vm.h"

namespace AI5 {

/*
 * XXX: This implementation is specific to Doukyuusei in many ways.
 *      Significant refactoring would be required to make it usable
 *      for other games.
 */

#define MAP_LOG(...) debug(5, __VA_ARGS__)

#define SPRITE_LOG(...) debug(6, __VA_ARGS__)

enum {
	kMapMaxTiles = 11655,
	kMapFrameTime = 54,
	kNoTile = 0xffff,
	kNoLocation = 0xffff
};

MapVersion g_mapVersion = kMapVersionOld;

// runtime CCD access (from libai5's ccd.c)
struct CcdSprite {
	byte state;
	byte no;
	byte w;
	byte h;
	uint16 x;
	uint16 y;
	byte frame; // internal use
	byte scriptIndex;
	// XXX: These are for internal use by the VM. It modifies these fields
	//      in-place, even though the game doesn't appear to access them
	//      ever (Doukyuusei and All Stars at least).
	byte scriptCmd;
	byte scriptRepetitions;
	uint16 scriptPtr;
};

struct CcdSpawn {
	byte screenX;
	byte screenY;
	byte spriteX;
	byte spriteY;
};

static CcdSprite ccdLoadSprite(uint no, const byte *data) {
	uint16 spOff = READ_LE_UINT16(data) + 14 * no;
	CcdSprite sp;
	memset(&sp, 0, sizeof(sp));
	sp.state = data[spOff];
	if (sp.state == 0xff)
		return sp;
	sp.no = data[spOff + 1];
	sp.w = data[spOff + 2];
	sp.h = data[spOff + 3];
	sp.x = READ_LE_UINT16(data + spOff + 4);
	sp.y = READ_LE_UINT16(data + spOff + 6);
	sp.frame = data[spOff + 8];
	sp.scriptIndex = data[spOff + 9];
	sp.scriptCmd = data[spOff + 10];
	sp.scriptRepetitions = data[spOff + 11];
	sp.scriptPtr = READ_LE_UINT16(data + spOff + 12);
	return sp;
}

static CcdSpawn ccdLoadSpawn(uint no, const byte *data) {
	uint16 off = READ_LE_UINT16(data + 4) + 4 * no;
	CcdSpawn spawn;
	spawn.screenX = data[off];
	spawn.screenY = data[off + 1];
	spawn.spriteX = data[off + 2];
	spawn.spriteY = data[off + 3];
	return spawn;
}

// static map tile data
struct MapTile {
	uint16 bg;
	uint16 fg;
	bool collides;
};

// on-screen tile data
struct MapScreenTile {
	uint16 bg;  // background tile index
	uint16 fg;  // foreground tile index
	uint16 sp;  // sprite tile index
	uint16 sp2; // secondary sprite tile index
	bool fgCha;  // foreground tile is in bmpCha
};

struct SpritePos {
	uint tx;
	uint ty;
	byte frame;
};

struct MapPos {
	uint16 x, y;
};

static MapPos makePos(uint16 x, uint16 y) {
	MapPos p;
	p.x = x;
	p.y = y;
	return p;
}

static MapPos popBack(Common::Array<MapPos> &a) {
	MapPos p = a.back();
	a.pop_back();
	return p;
}

struct PathData {
	MapPos pred;
	uint gScore : 16;
	uint fScore : 15;
	uint notInFrontier : 1;
};

struct MapState {
	struct {
		uint tx;
		uint ty;
		uint tw;
		uint th;
	} screen;
	// size of the map
	uint cols;
	uint rows;
	// camera offset from the player sprite
	uint camOffTx;
	uint camOffTy;
	// Map.getLocation state
	MapLocationMode locationMode;
	bool getLocationEnabled;
	uint16 prevLocation;
	// sprite list
	Common::Array<CcdSprite> sprites;
	// player position history
	SpritePos posHistory[32];
	uint posHistoryPtr;
	// static map tile data
	MapTile tileData[kMapMaxTiles];
	// on-screen tiles (dynamic)
	MapScreenTile tiles[480][640];
	// frame rate timer
	VMTimer timer;
	// pathing data
	struct {
		bool active;
		MapPos goal;
		PathData tiles[480][640];
		Common::Array<MapPos> frontier;
		Common::Array<MapPos> path;
		uint pathPtr;
		struct {
			CcdSprite *sp;
			byte state;
			byte cmd;
			byte repetitions;
		} saved;
	} path;
	// Tile graphics (bitmaps). There are separate bitmaps for
	// map and sprite tiles.
	byte bmpMap[1280 * 960];
	byte bmpCha[640 * 96 * 2];
	GfxColor palMap[256];
	GfxColor palCha[256];
};

static MapState *g_mapState = nullptr;
#define map (*g_mapState)

void mapInit() {
	if (!g_mapState)
		g_mapState = new MapState();
}

void mapFini() {
	delete g_mapState;
	g_mapState = nullptr;
}

static void copyToBmp(byte *bmp, uint bmpSize, uint off, Common::SeekableReadStream *file) {
	uint size = file->size();
	if (off > bmpSize)
		off = bmpSize;
	if (off + size > bmpSize) {
		warning("Tried to write past the end of bitmap");
		size = bmpSize - off;
	}
	file->read(bmp + off, size);
}
#define copyToBmpMap(off, file) copyToBmp(map.bmpMap, 0x12c000, off, file)
#define copyToBmpCha(off, file) copyToBmp(map.bmpCha, 0x1e000, off, file)

void mapLoadBitmap(const char *name, uint col, uint row, uint which) {
	MAP_LOG("mapLoadBitmap(\"%s\",%u,%u,%u)", name, col, row, which);
	Common::ScopedPtr<Common::SeekableReadStream> file(assetDataLoad(name));
	if (!file) {
		warning("Failed to load map bitmap: \"%s\"", name);
		return;
	}

	switch (which) {
	case 0:  copyToBmpMap(row * 1280 + col, file.get()); break;
	case 1:  copyToBmpCha(0xf000 + row * 640 + col, file.get()); break;
	case 3:  copyToBmpCha(0, file.get()); break;
	default: copyToBmpCha(0x7800 + col, file.get()); break;
	}
}

void mapLoadPalette(const char *name, uint which) {
	MAP_LOG("mapLoadPalette(\"%s\",%u)", name, which);
	Common::ScopedPtr<Common::SeekableReadStream> file(assetDataLoad(name));
	if (!file) {
		warning("Failed to load map palette: \"%s\"", name);
		return;
	}
	uint size = file->size();
	if (size < 512)
		warning("Incomplete palette (%uB)", size);
	else if (size > 512)
		warning("Palette file is larger than expected (%uB)", size);

	for (uint i = 0; i * 2 + 1 < size && i < 256; i++) {
		uint16 c = file->readUint16LE();
		if (which == 1)
			map.palCha[i] = gfxDecodeBgr555(c);
		else
			map.palMap[i] = gfxDecodeBgr555(c);
	}
}

/* g_mem->mapData has the following structure:
 *
 * struct mapData {
 *     uint32 mpxPtr;
 *     uint32 screenTx;
 *     uint32 screenTy;
 *     uint32 cols;
 *     uint32 rows;
 *     uint32 uk[2];
 *     uint32 screenTw;
 *     uint32 screenTh;
 *     uint32 camOffTx;
 *     uint32 camOffTy;
 *     uint32 uk[2];
 * };
 */
static void updateMapData() {
	map.screen.tx  = READ_LE_UINT32(g_mem->mapData + 4);
	map.screen.ty  = READ_LE_UINT32(g_mem->mapData + 8);
	map.screen.tw  = READ_LE_UINT32(g_mem->mapData + 28);
	map.screen.th  = READ_LE_UINT32(g_mem->mapData + 32);
	map.camOffTx = READ_LE_UINT32(g_mem->mapData + 36);
	map.camOffTy = READ_LE_UINT32(g_mem->mapData + 40);
}

void mapLoadTilemap() {
	MAP_LOG("mapLoadTilemap()");
	// XXX: AI5WIN.EXE exposes its own internal memory to the game through
	//      System.mapData, but the game doesn't read from it anyway, and
	//      only writes to it immediately before calling Map.loadTilemap.
	//      So we just load it into a private struct.
	updateMapData();

	uint32 mpxOff = memGetSysvar32(kSysVarMpxOffset);
	map.cols = READ_LE_UINT16(g_mem->fileData + mpxOff);
	map.rows = READ_LE_UINT16(g_mem->fileData + mpxOff + 2);
	map.locationMode = kMapLocationDisabled;
	map.getLocationEnabled = false;
	map.prevLocation = kNoLocation;

	if (map.rows * map.cols > kMapMaxTiles)
		error("too many tiles in mpx: %ux%u", map.cols, map.rows);

	if (g_mapVersion == kMapVersionOld) {
		byte *mpc = g_mem->fileData + mpxOff;
		for (uint row = 0; row < map.rows; row++) {
			for (uint col = 0; col < map.cols; col++) {
				byte *tdata = mpc + 4 + (map.cols * row + col) * 3;
				// XXX: bg/fg are 10-bit values packed into 24 bits.
				uint16 v = READ_LE_UINT16(tdata + 0);
				uint16 bg = v & 0x3ff;
				uint16 fg = (v >> 10) | ((tdata[2] & 0xf) << 6);
				MapTile &t = map.tileData[row * map.cols + col];
				t.bg = bg == 0x3ff ? 0xffff : bg;
				t.fg = fg == 0x3ff ? 0xffff : fg;
				t.collides = tdata[2] & 0x80;
			}
		}
		return;
	}

	// XXX: AI5WIN.EXE reads tiles directly from System.mpxOffset in Map.loadTiles,
	//      but once again the game doesn't seem to access this data ever so we just
	//      load it into a private array.
	byte *mpx = g_mem->fileData + mpxOff;
	for (uint row = 0; row < map.rows; row++) {
		for (uint col = 0; col < map.cols; col++) {
			byte *tdata = mpx + 4 + (map.cols * row + col) * 5;
			MapTile &t = map.tileData[row * map.cols + col];
			t.bg = READ_LE_UINT16(tdata + 0);
			t.fg = READ_LE_UINT16(tdata + 2);
			t.collides = !!tdata[4];
		}
	}
}

void mapLoadTiles() {
	// load on-screen tiles into tiles array
	for (uint row = 0; row < map.screen.th; row++) {
		for (uint col = 0; col < map.screen.tw; col++) {
			uint i = (row + map.screen.ty) * map.cols + map.screen.tx + col;
			map.tiles[row][col].bg = map.tileData[i].bg;
			map.tiles[row][col].fg = map.tileData[i].fg;
			map.tiles[row][col].sp = kNoTile;
			map.tiles[row][col].sp2 = kNoTile;
			map.tiles[row][col].fgCha = false;
		}
	}
}

static int bmpOffset(uint tileNo, int w, int h) {
	uint ty = tileNo / (w / 16);
	uint tx = tileNo % (w / 16);
	uint x = tx * 16;
	uint y = ty * 16;
	return ((h - y) - 1) * w + x;
}

static void blitTile(GfxSurface *dst, int x, int y, byte *bmp, GfxColor pal[256],
		uint tileNo, uint bmpW, uint bmpH) {
	byte *dstRow = dst->pixels + (y * dst->pitch + x * 3);
	byte *bmpRow = bmp + bmpOffset(tileNo, bmpW, bmpH);

	for (int row = 0; row < 16; row++, dstRow += dst->pitch, bmpRow -= bmpW) {
		byte *dstP = dstRow;
		byte *bmpP = bmpRow;
		for (int col = 0; col < 16; col++, dstP += 3, bmpP++) {
			GfxColor *c = &pal[*bmpP];
			dstP[0] = c->r;
			dstP[1] = c->g;
			dstP[2] = c->b;
		}
	}
}

static void blitTileMasked(GfxSurface *dst, int x, int y, byte *bmp, GfxColor pal[256],
		uint tileNo, uint bmpW, uint bmpH) {
	byte *dstRow = dst->pixels + (y * dst->pitch + x * 3);
	byte *bmpRow = bmp + bmpOffset(tileNo, bmpW, bmpH);

	for (int row = 0; row < 16; row++, dstRow += dst->pitch, bmpRow -= bmpW) {
		byte *dstP = dstRow;
		byte *bmpP = bmpRow;
		for (int col = 0; col < 16; col++, dstP += 3, bmpP++) {
			if (*bmpP == 0)
				continue;
			GfxColor *c = &pal[*bmpP];
			dstP[0] = c->r;
			dstP[1] = c->g;
			dstP[2] = c->b;
		}
	}
}

static void drawTile(MapScreenTile *tile, int x, int y) {
	GfxSurface *dst = gfxGetSurface(0);

	if (tile->bg != kNoTile) {
		blitTile(dst, x, y, map.bmpMap, map.palMap, tile->bg, 1280, 960);
	}
	if (tile->sp != kNoTile) {
		blitTileMasked(dst, x, y, map.bmpCha, map.palCha, tile->sp, 640, 192);
		if (tile->sp2 != kNoTile) {
			blitTileMasked(dst, x, y, map.bmpCha, map.palCha, tile->sp2, 640, 192);
		}
	}
	if (tile->fg != kNoTile) {
		if (tile->fgCha) {
			blitTileMasked(dst, x, y, map.bmpCha, map.palCha, tile->fg, 640, 192);
		} else {
			blitTileMasked(dst, x, y, map.bmpMap, map.palMap, tile->fg, 1280, 960);
		}
	}
}

static int surfOffset(uint tileNo, GfxSurface *s) {
	uint ty = tileNo / (s->w / 16);
	uint tx = tileNo % (s->w / 16);
	uint x = tx * 16;
	uint y = ty * 16;
	return y * s->pitch + x;
}

static void blitTileIndexed(GfxSurface *dst, int x, int y, GfxSurface *src,
		uint tileNo) {
	byte *dstRow = dst->pixels + (y * dst->pitch + x);
	byte *srcRow = src->pixels + surfOffset(tileNo, src);

	for (int row = 0; row < 16; row++, dstRow += dst->pitch, srcRow += src->pitch) {
		memcpy(dstRow, srcRow, 16);
	}
}

static void blitTileMaskedIndexed(GfxSurface *dst, int x, int y, GfxSurface *src,
		uint tileNo) {
	byte mask = memGetSysvar16(kSysVarMaskColor);
	byte *dstRow = dst->pixels + (y * dst->pitch + x);
	byte *srcRow = src->pixels + surfOffset(tileNo, src);

	for (int row = 0; row < 16; row++, dstRow += dst->pitch, srcRow += src->pitch) {
		byte *dstP = dstRow;
		byte *srcP = srcRow;
		for (int col = 0; col < 16; col++, dstP++, srcP++) {
			if (*srcP != mask)
				*dstP = *srcP;
		}
	}
}

static void drawTileKakyuusei(MapScreenTile *tile, int x, int y) {
	GfxSurface *dst = gfxGetSurface(0);
	GfxSurface *mapS = gfxGetSurface(2);
	GfxSurface *spriteS = gfxGetSurface(3);

	if (tile->bg != kNoTile) {
		blitTileIndexed(dst, x, y, mapS, tile->bg);
	}
	if (tile->sp != kNoTile) {
		blitTileMaskedIndexed(dst, x, y, spriteS, tile->sp);
		if (tile->sp2 != kNoTile)
			blitTileMaskedIndexed(dst, x, y, spriteS, tile->sp2);
	}
	if (tile->fg != kNoTile) {
		if (tile->fgCha) {
			blitTileMaskedIndexed(dst, x, y, spriteS, tile->fg);
		} else {
			blitTileMaskedIndexed(dst, x, y, mapS, tile->fg);
		}
	}
}

void mapDrawTiles() {
	for (uint row = 0; row < map.screen.th; row++) {
		for (uint col = 0; col < map.screen.tw; col++) {
			if (g_mapVersion == kMapVersionOld)
				drawTileKakyuusei(&map.tiles[row][col], col * 16, row * 16);
			else
				drawTile(&map.tiles[row][col], col * 16, row * 16);
		}
	}

	gfxWholeSurfaceDirty(0);

	// XXX: If shift is held down, we double the frame rate.
	//      This is not what AI5WIN.EXE does (it doubles the amount of movement that
	//      occurs per-frame), but the end result is the same, and this method results
	//      in smoother movement.
	if (g_game->id != GID_DOUKYUUSEI2)
		vmTimerTick(map.timer, inputDown(kInputShift) ? kMapFrameTime/2 : kMapFrameTime);
}

#define FRAME_BITS (g_mapVersion == kMapVersionOld ? 2 : 4)
#define FRAME_MASK (g_mapVersion == kMapVersionOld ? 0x3 : 0xf)

static byte *getCcd() {
	return g_mem->fileData + memGetSysvar32(kSysVarCcdOffset);
}

static bool checkSpriteNo(uint no) {
	if (no >= map.sprites.size()) {
		//warning("Invalid sprite index: %u", no);
		return false;
	}
	return true;
}

static CcdSprite *getSprite(uint no) {
	if (!checkSpriteNo(no))
		return nullptr;
	return &map.sprites[no];
}

void mapLoadSpriteScripts() {
	MAP_LOG("mapLoadSpriteScripts()");
	map.sprites.clear();

	// XXX: AI5WIN.EXE modifies the ccd data in user-visible memory, but the
	//      game doesn't seem to access this memory so we just load it into
	//      private VM memory.
	byte *ccd = getCcd();
	uint16 scriptOff = READ_LE_UINT16(ccd + 2);

	for (uint i = 0; true; i++) {
		CcdSprite sp = ccdLoadSprite(i, ccd);
		if (sp.state == 0xff)
			break;
		sp.scriptPtr = READ_LE_UINT16(ccd + scriptOff + sp.scriptIndex * 2);
		sp.scriptCmd = ccd[sp.scriptPtr] >> 4;
		sp.scriptRepetitions = (ccd[sp.scriptPtr] & 0xf) + 1;
		sp.scriptPtr++;
		map.sprites.push_back(sp);
	}
}

void mapSetSpriteScript(uint spNo, uint scriptNo) {
	MAP_LOG("mapSetSpriteScript(%u,%u)", spNo, scriptNo);
	CcdSprite *sp = getSprite(spNo);
	if (!sp)
		return;

	byte *ccd = getCcd();
	uint16 scriptOff = READ_LE_UINT16(ccd + 2);
	sp->scriptIndex = scriptNo;
	sp->scriptPtr = READ_LE_UINT16(ccd + scriptOff + scriptNo * 2);
	sp->scriptCmd = ccd[sp->scriptPtr] >> 4;
	sp->scriptRepetitions = (ccd[sp->scriptPtr] & 0xf) + 1;
	sp->scriptPtr++;
}

void mapSetSpriteAnim(uint spNo, byte animNo) {
	MAP_LOG("mapSetSpriteAnim(%u,%u)", spNo, (uint)animNo);
	if (!checkSpriteNo(spNo))
		return;
	map.sprites[spNo].frame = animNo << FRAME_BITS;
}

void mapSpawnSprite(uint spawnNo, uint spNo, byte animNo) {
	MAP_LOG("mapSpawnSprite(%u,%u,%u)", spawnNo, spNo, (uint)animNo);
	CcdSprite *sp = getSprite(spNo);
	if (!sp)
		return;

	CcdSpawn spawn = ccdLoadSpawn(spawnNo, getCcd());

	uint32 screenTx = spawn.screenX;
	uint32 screenTy = spawn.screenY;

	// adjust screen location if spawn is near edge of map
	if (screenTx + map.screen.tw >= map.cols)
		screenTx = map.cols - map.screen.tw;
	if (screenTy + map.screen.th >= map.rows)
		screenTy = map.rows - map.screen.th;

	map.screen.tx = screenTx;
	map.screen.ty = screenTy;
	sp->x = spawn.spriteX;
	sp->y = spawn.spriteY;
	sp->frame = animNo << FRAME_BITS;

	for (uint i = 0; i < ARRAYSIZE(map.posHistory); i++) {
		map.posHistory[i].tx = sp->x;
		map.posHistory[i].ty = sp->y;
		map.posHistory[i].frame = sp->frame;
	}
	map.posHistoryPtr = 0;
}

void mapSetSpriteState(uint no, byte state) {
	MAP_LOG("mapSetSpriteState(%u,0x%x)", no, (uint)state);
	if (!checkSpriteNo(no))
		return;
	map.sprites[no].state = state;
}

#define TILES_PER_FRAME (g_mapVersion == kMapVersionOld ? 4 : 9)
#define FRAMES_PER_ANIM (g_mapVersion == kMapVersionOld ? 4 : 12)
#define ANIMS_PER_SHEET 4

#define BYTES_PER_FRAME (TILES_PER_FRAME * 2)
#define BYTES_PER_ANIM  (BYTES_PER_FRAME * FRAMES_PER_ANIM)
#define BYTES_PER_SHEET (BYTES_PER_ANIM * ANIMS_PER_SHEET)

static uint spriteTileOffset(CcdSprite *sp) {
	return sp->no * BYTES_PER_SHEET
		+ (sp->frame >> FRAME_BITS) * BYTES_PER_ANIM
		+ (sp->frame & FRAME_MASK) * BYTES_PER_FRAME;
}

static void placeSprite(CcdSprite *sp) {
	byte *ccd = getCcd();
	uint spTileOff = spriteTileOffset(sp);
	byte *spTiles = ccd + READ_LE_UINT16(ccd + 6) + spTileOff;

	int offTx = (int)sp->x - map.screen.tx;
	int offTy = (int)sp->y - map.screen.ty;
	int screenTw = map.screen.tw;
	int screenTh = map.screen.th;
	for (int row = 0, spT = 0; row < sp->h && offTy + row < screenTh; row++) {
		for (int col = 0; col < sp->w && offTx + col < screenTw; col++, spT++) {
			if (offTx + col < 0 || offTy + row < 0)
				continue;
			uint16 tileNo = READ_LE_UINT16(spTiles + spT * 2);
			int charTx = offTx + col;
			int charTy = offTy + row;
			if (sp->state & kMapSpNonchara) {
				map.tiles[charTy][charTx].fg = tileNo;
				map.tiles[charTy][charTx].fgCha = true;
			} else {
				if (map.tiles[charTy][charTx].sp == kNoTile) {
					map.tiles[charTy][charTx].sp = tileNo;
				} else if (map.tiles[charTy][charTx].sp2 == kNoTile) {
					if (map.sprites[0].y < sp->y) {
						map.tiles[charTy][charTx].sp2 = tileNo;
					} else {
						map.tiles[charTy][charTx].sp2 =
							map.tiles[charTy][charTx].sp;
						map.tiles[charTy][charTx].sp = tileNo;
					}
				}
			}
		}
	}
}

void mapPlaceSprites() {
	MAP_LOG("mapPlaceSprites()");
	for (CcdSprite *sp = map.sprites.begin(); sp != map.sprites.end(); ++sp) {
		if (sp->state & kMapSpEnabled)
			placeSprite(sp);
	}
}

static void spritePosHistoryPush(CcdSprite *sp) {
	SpritePos &pos = map.posHistory[map.posHistoryPtr];
	pos.tx = sp->x;
	pos.ty = sp->y;
	pos.frame = sp->frame;
	map.posHistoryPtr = (map.posHistoryPtr + 1) % ARRAYSIZE(map.posHistory);
}

static uint16 spriteRewindPos(CcdSprite *sp, uint d) {
	uint i = ((map.posHistoryPtr - 1) - d * 2) % ARRAYSIZE(map.posHistory);
	sp->x = map.posHistory[i].tx;
	sp->y = map.posHistory[i].ty;
	sp->frame = map.posHistory[i].frame;
	return 0;
}

void mapRewindSpritePos(uint spNo, uint d) {
	MAP_LOG("mapRewindSpritePos(%u,%u)", spNo, d);
	CcdSprite *sp = getSprite(spNo);
	if (!sp)
		return;
	spriteRewindPos(sp, d);
}

/*
 * Check if any tiles within the given rectangle have collision enabled.
 * The collision state is additionally written to `result` for each tile.
 * This allows the move functions to decide whether or not to hook around
 * corners.
 *
 * Note that this is NOT what the original game does: the original game
 * has your character slide up/right along walls, which makes navigating
 * with the keyboard very annoying.
 */
static bool mapTilesCollide(uint tx, uint ty, uint tw, uint th,
		bool *result) {
	bool r = false;
	for (uint row = 0, i = 0; row < th; row++) {
		MapTile *t = &map.tileData[(ty + row) * map.cols + tx];
		for (uint col = 0; col < tw; col++, t++, i++) {
			if ((result[i] = t->collides))
				r = true;
		}
	}
	return r;
}

static bool spriteCanMoveUp(CcdSprite *sp, bool *r) {
	if (sp->y == 0) {
		r[0] = true;
		r[1] = true;
		r[2] = true;
		return false;
	}
	return !mapTilesCollide(sp->x, sp->y, sp->w, 1, r);
}

static bool spriteCanMoveDown(CcdSprite *sp, bool *r) {
	if (sp->y + sp->h >= map.rows) {
		r[0] = true;
		r[1] = true;
		r[2] = true;
		return false;
	}
	return !mapTilesCollide(sp->x, sp->y + sp->h, sp->w, 1, r);
}

static bool spriteCanMoveLeft(CcdSprite *sp, bool *r) {
	if (sp->x == 0) {
		r[0] = true;
		r[1] = true;
		return false;
	}
	return !mapTilesCollide(sp->x - 1, sp->y + 1, 1, sp->h - 1, r);
}

static bool spriteCanMoveRight(CcdSprite *sp, bool *r) {
	if (sp->x + sp->w >= map.cols) {
		r[0] = true;
		r[1] = true;
		return false;
	}
	return !mapTilesCollide(sp->x + sp->w, sp->y + 1, 1, sp->h - 1, r);
}

static void spriteAdvanceFrame(CcdSprite *sp, MapDirection dir) {
	if (g_mapVersion == kMapVersionOld) {
		sp->frame = (sp->frame + 1) & FRAME_MASK;
	} else {
		sp->frame = (sp->frame & FRAME_MASK) + 1;
		if (sp->frame >= FRAMES_PER_ANIM)
			sp->frame = 1;
	}
	sp->frame |= (dir << FRAME_BITS);
}

static uint16 spriteMoveLeft(CcdSprite *sp, bool advance, bool slideOk);
static uint16 spriteMoveRight(CcdSprite *sp, bool advance, bool slideOk);

static void doSpriteMoveUp(CcdSprite *sp) {
	if (sp->y > 0) {
		sp->y--;
		if (sp->state & kMapSpCamera) {
			if (sp->y < map.camOffTy + map.screen.ty && map.screen.ty > 0)
				map.screen.ty--;
		}
	}
}

static uint16 spriteMoveUp(CcdSprite *sp, bool advance, bool slideOk) {
	if (advance)
		spriteAdvanceFrame(sp, kMapUp);

	// check if sprite would collide with terrain
	bool result[3];
	if (sp->state & kMapSpCollides && !spriteCanMoveUp(sp, result)) {
		if (!result[0])
			return spriteMoveLeft(sp, false, false);
		if (!result[sp->w - 1] || (slideOk && !g_config.mapNoWallslide))
			return spriteMoveRight(sp, false, false);
		return 0xffff;
	}

	if (!advance)
		sp->frame = (sp->frame & FRAME_MASK) | (kMapUp << FRAME_BITS);

	// move sprite
	doSpriteMoveUp(sp);
	return 0;
}

static void doSpriteMoveDown(CcdSprite *sp) {
	if (sp->y + sp->h < map.rows) {
		sp->y++;
		if (sp->state & kMapSpCamera) {
			int maxTy = map.rows - map.screen.th;
			if (sp->y > map.camOffTy + map.screen.ty && map.screen.ty < (uint)maxTy)
				map.screen.ty++;
		}
	}
}

static uint16 spriteMoveDown(CcdSprite *sp, bool advance, bool slideOk) {
	if (advance)
		spriteAdvanceFrame(sp, kMapDown);

	// check if sprite would collide with terrain
	bool result[3];
	if (sp->state & kMapSpCollides && !spriteCanMoveDown(sp, result)) {
		if (!result[0])
			return spriteMoveLeft(sp, false, false);
		if (!result[sp->w - 1] || (slideOk && !g_config.mapNoWallslide))
			return spriteMoveRight(sp, false, false);
		return 0xffff;
	}

	if (!advance)
		sp->frame = (sp->frame & FRAME_MASK) | (kMapDown << FRAME_BITS);

	// move sprite
	doSpriteMoveDown(sp);
	return 0;
}

static void doSpriteMoveLeft(CcdSprite *sp) {
	if (sp->x > 0) {
		sp->x--;
		if (sp->state & kMapSpCamera) {
			if (sp->x < map.camOffTx + map.screen.tx && map.screen.tx > 0)
				map.screen.tx--;
		}
	}
}

static uint16 spriteMoveLeft(CcdSprite *sp, bool advance, bool slideOk) {
	if (advance)
		spriteAdvanceFrame(sp, kMapLeft);

	// check if sprite would collide with terrain
	bool result[2];
	if (sp->state & kMapSpCollides && !spriteCanMoveLeft(sp, result)) {
		if (!result[1])
			return spriteMoveDown(sp, false, false);
		if (!result[0] || (slideOk && !g_config.mapNoWallslide))
			return spriteMoveUp(sp, false, false);
		return 0xffff;
	}

	if (!advance)
		sp->frame = (sp->frame & FRAME_MASK) | (kMapLeft << FRAME_BITS);

	// move sprite
	doSpriteMoveLeft(sp);
	return 0;
}

static void doSpriteMoveRight(CcdSprite *sp) {
	if (sp->x + sp->w < map.cols) {
		sp->x++;
		if (sp->state & kMapSpCamera) {
			int maxTx = map.cols - map.screen.tw;
			if (sp->x > map.camOffTx + map.screen.tx && map.screen.tx < (uint)maxTx)
				map.screen.tx++;
		}
	}
}

static uint16 spriteMoveRight(CcdSprite *sp, bool advance, bool slideOk) {
	if (advance)
		spriteAdvanceFrame(sp, kMapRight);

	// check if sprite would collide with terrain
	bool result[2];
	if (sp->state & kMapSpCollides && !spriteCanMoveRight(sp, result)) {
		if (!result[1])
			return spriteMoveDown(sp, false, false);
		if (!result[0] || (slideOk && !g_config.mapNoWallslide))
			return spriteMoveUp(sp, false, false);
		return 0xffff;
	}

	if (!advance)
		sp->frame = (sp->frame & FRAME_MASK) | (kMapRight << FRAME_BITS);

	// move sprite
	doSpriteMoveRight(sp);
	return 0;
}

static uint16 spriteMoveUpLeft(CcdSprite *sp, bool advance) {
	if (advance)
		spriteAdvanceFrame(sp, kMapUp);

	// check if sprite would collide with terrain
	if (sp->state & kMapSpCollides) {
		bool r[3];
		if (!spriteCanMoveUp(sp, r)) {
			if (!spriteCanMoveLeft(sp, r))
				return 0xffff;
			doSpriteMoveLeft(sp);
			return 0;
		}
		doSpriteMoveUp(sp);
		if (spriteCanMoveLeft(sp, r))
			doSpriteMoveLeft(sp);
		return 0;
	}

	doSpriteMoveUp(sp);
	doSpriteMoveLeft(sp);
	return 0;
}

static uint16 spriteMoveUpRight(CcdSprite *sp, bool advance) {
	if (advance)
		spriteAdvanceFrame(sp, kMapUp);

	// check if sprite would collide with terrain
	if (sp->state & kMapSpCollides) {
		bool r[3];
		if (!spriteCanMoveUp(sp, r)) {
			if (!spriteCanMoveRight(sp, r))
				return 0xffff;
			doSpriteMoveRight(sp);
			return 0;
		}
		doSpriteMoveUp(sp);
		if (spriteCanMoveRight(sp, r))
			doSpriteMoveRight(sp);
		return 0;
	}

	doSpriteMoveUp(sp);
	doSpriteMoveRight(sp);
	return 0;
}

static uint16 spriteMoveDownLeft(CcdSprite *sp, bool advance) {
	if (advance)
		spriteAdvanceFrame(sp, kMapDown);

	// check if sprite would collide with terrain
	if (sp->state & kMapSpCollides) {
		bool r[3];
		if (!spriteCanMoveDown(sp, r)) {
			if (!spriteCanMoveLeft(sp, r))
				return 0xffff;
			doSpriteMoveLeft(sp);
			return 0;
		}
		doSpriteMoveDown(sp);
		if (spriteCanMoveLeft(sp, r))
			doSpriteMoveLeft(sp);
		return 0;
	}

	doSpriteMoveDown(sp);
	doSpriteMoveLeft(sp);
	return 0;
}

static uint16 spriteMoveDownRight(CcdSprite *sp, bool advance) {
	if (advance)
		spriteAdvanceFrame(sp, kMapDown);

	// check if sprite would collide with terrain
	if (sp->state & kMapSpCollides) {
		bool r[3];
		if (!spriteCanMoveDown(sp, r)) {
			if (!spriteCanMoveRight(sp, r))
				return 0xffff;
			doSpriteMoveRight(sp);
			return 0;
		}
		doSpriteMoveDown(sp);
		if (spriteCanMoveRight(sp, r))
			doSpriteMoveRight(sp);
		return 0;
	}

	doSpriteMoveDown(sp);
	doSpriteMoveRight(sp);
	return 0;
}

static void spriteMovePath(CcdSprite *sp) {
	if (map.path.pathPtr == 0) {
		mapStopPathing();
		return;
	}

	if (g_game->id == GID_DOUKYUUSEI && inputDown(kInputCancel) && memGetVar4(4067)) {
		memSetVar32(18, 1);
		mapStopPathing();
		return;
	}

	map.path.pathPtr--;
	MapPos next = map.path.path[map.path.pathPtr];
	if (next.y < sp->y) {
		if (next.x < sp->x)
			spriteMoveUpLeft(sp, true);
		else if (next.x > sp->x)
			spriteMoveUpRight(sp, true);
		else
			spriteMoveUp(sp, true, false);
	} else if (next.y > sp->y) {
		if (next.x < sp->x)
			spriteMoveDownLeft(sp, true);
		else if (next.x > sp->x)
			spriteMoveDownRight(sp, true);
		else
			spriteMoveDown(sp, true, false);
	} else if (next.x < sp->x) {
		spriteMoveLeft(sp, true, false);
	} else if (next.x > sp->x) {
		spriteMoveRight(sp, true, false);
	}
	if (sp->x != next.x || sp->y != next.y) {
		warning("pathed to wrong tile?");
		sp->x = next.x;
		sp->y = next.y;
	}
	memSetVar16(3, sp->frame >> FRAME_BITS);
	spritePosHistoryPush(sp);
}

static void spriteSkipPath(CcdSprite *sp) {
	if (!map.path.active)
		return;
	while (map.path.pathPtr > 20)
		spriteMovePath(sp);
}

enum {
	kSpInputUp = 1,
	kSpInputDown = 2,
	kSpInputLeft = 4,
	kSpInputRight = 8,
};

static uint mapGetKeyboardInputs() {
	uint inputs = 0;
	if (inputDown(kInputUp))
		inputs |= kSpInputUp;
	if (inputDown(kInputDown))
		inputs |= kSpInputDown;
	if (inputDown(kInputLeft))
		inputs |= kSpInputLeft;
	if (inputDown(kInputRight))
		inputs |= kSpInputRight;
	if (inputDown(kInputCancel) && !inputs)
		return 0xfffe;
	return inputs;
}

static uint mapGetMouseInputs(CcdSprite *sp) {
	uint curX, curY;
	cursorGetPos(curX, curY);

	uint spX = (sp->x - map.screen.tx) * 16;
	uint spY = ((sp->y + 1) - map.screen.ty) * 16;

	uint inputs = 0;
	if (curY < spY)
		inputs |= kSpInputUp;
	if (curY > spY + 32)
		inputs |= kSpInputDown;
	if (curX < spX)
		inputs |= kSpInputLeft;
	if (curX > spX + 48)
		inputs |= kSpInputRight;

	return inputs;
}

static uint16 spriteDoHandleInput(CcdSprite *sp, uint inputs) {
	uint tx = sp->x;
	uint ty = sp->y;

	switch (inputs) {
	case kSpInputUp: spriteMoveUp(sp, true, true); break;
	case kSpInputDown: spriteMoveDown(sp, true, true); break;
	case kSpInputLeft: spriteMoveLeft(sp, true, true); break;
	case kSpInputRight: spriteMoveRight(sp, true, true); break;
	case kSpInputUp | kSpInputLeft: spriteMoveUpLeft(sp, true); break;
	case kSpInputUp | kSpInputRight: spriteMoveUpRight(sp, true); break;
	case kSpInputDown | kSpInputLeft: spriteMoveDownLeft(sp, true); break;
	case kSpInputDown | kSpInputRight: spriteMoveDownRight(sp, true); break;
	case 0xfffe: return 0xfffe;
	default: return 0xffff;
	}

	if (sp->x != tx || sp->y != ty)
		spritePosHistoryPush(sp);
	return 0;

}

static uint16 spriteHandleInput(CcdSprite *sp) {
	uint inputs;
	if (inputDown(kInputActivate))
		inputs = mapGetMouseInputs(sp);
	else
		inputs = mapGetKeyboardInputs();

	return spriteDoHandleInput(sp, inputs);
}

static uint16 execSprite(CcdSprite *sp) {
	// load next command
	if (!sp->scriptRepetitions) {
		byte *ccd = getCcd();
		byte *script = ccd + sp->scriptPtr;
		if (*script == 0) {
			// loop
			uint16 scriptOff = READ_LE_UINT16(ccd + 2);
			sp->scriptPtr = READ_LE_UINT16(ccd + scriptOff + sp->scriptIndex * 2);
			script = ccd + sp->scriptPtr;
		}
		sp->scriptPtr++;
		sp->scriptCmd = *script >> 4;
		sp->scriptRepetitions = *script & 0xf;
		if (g_mapVersion == kMapVersionOld)
			sp->scriptRepetitions++;
	}

	//debug(1, "EXEC %x:%d", sp->scriptCmd, sp->scriptRepetitions);

	// exec command
	uint16 r = 0;
	switch (sp->scriptCmd) {
	case 0:
		SPRITE_LOG("spriteExec0(%u)", sp->no);
		return 0; // ???
	case 2:
		SPRITE_LOG("spriteMoveUp(%u)", sp->no);
		spriteMoveUp(sp, true, false);
		break;
	case 3:
		SPRITE_LOG("spriteMoveDown(%u)", sp->no);
		spriteMoveDown(sp, true, false);
		break;
	case 4:
		SPRITE_LOG("spriteMoveLeft(%u)", sp->no);
		spriteMoveLeft(sp, true, false);
		break;
	case 5:
		SPRITE_LOG("spriteMoveRight(%u)", sp->no);
		spriteMoveRight(sp, true, false);
		break;
	case 6:
		SPRITE_LOG("spriteRewindPos(%u, %u)", sp->no, sp->scriptRepetitions);
		r = spriteRewindPos(sp, sp->scriptRepetitions);
		break;
	case 8:
		SPRITE_LOG("spriteSetFrame(%u, %d)", sp->no, sp->scriptRepetitions - 1);
		sp->frame = sp->scriptRepetitions - 1;
		sp->scriptRepetitions = 1;
		r = 0;
		break;
	case 9:
		sp->scriptRepetitions = (sp->scriptRepetitions / 2) + 1;
		SPRITE_LOG("spriteHalveReps(%u) -> %u", sp->no, sp->scriptRepetitions);
		r = 0;
		break;
	case 13:
		SPRITE_LOG("spriteMovePath(%u)", sp->no);
		spriteMovePath(sp);
		break;
	case 14:
		SPRITE_LOG("spriteHandleInput(%u)", sp->no);
		r = spriteHandleInput(sp);
		break;
	case 15:
		SPRITE_LOG("spriteSkipPath(%u)", sp->no);
		spriteSkipPath(sp);
		sp->scriptCmd = 13;
		spriteMovePath(sp);
		break;
	default:
		error("Unimplemented sprite command: %u\n"
				"sprite %d\n"
				"\tscriptIndex = %u\n"
				"\tscriptPtr = %u\n"
				"\tscriptCmd = %u\n"
				"\tscriptRepetitions = %u\n",
				sp->scriptCmd,
				(int)(sp - &map.sprites[0]),
				(uint)sp->scriptIndex,
				(uint)sp->scriptPtr,
				(uint)sp->scriptCmd,
				(uint)sp->scriptRepetitions);
	}

	if ((r & 0xff) == 0) {
		if (sp->scriptRepetitions != 0xff)
			sp->scriptRepetitions--;
		if (map.locationMode == kMapLocationModeOneshot && sp->state & kMapSpPlayer)
			map.getLocationEnabled = true;
		return 0xffff;
	}
	if ((r & 0xff) == 0xff)
		return 0;
	return r;
}

static uint16 execSprites() {
	uint i = 0;
	uint16 r = 0xffff;
	for (CcdSprite *sp = map.sprites.begin(); sp != map.sprites.end(); ++sp) {
		if (sp->state) {
			uint16 v = execSprite(sp);
			if (sp->state & 0x40 && i == 0)
				r = v;
		}
		i++;
	}
	return r;
}

void mapExecSprites() {
	memSetVar16(18, execSprites());
}

void mapExecSpritesAndRedraw() {
	assert(map.sprites.size() > 0);
	uint16 r = execSprites();
	if (r) {
		mapLoadTiles();
		mapPlaceSprites();
		mapDrawTiles();
		memSetVar16(3, map.sprites[0].frame >> FRAME_BITS);
	}
	memSetVar16(18, r);
}

void mapMoveSprite(uint spNo, MapDirection dir) {
	MAP_LOG("mapMoveSprite(%u,%d)", spNo, dir);
	CcdSprite *sp = getSprite(spNo);
	if (!sp)
		return;

	switch (dir) {
	case kMapUp: spriteMoveUp(sp, true, false); break;
	case kMapDown: spriteMoveDown(sp, true, false); break;
	case kMapLeft: spriteMoveLeft(sp, true, false); break;
	case kMapRight: spriteMoveRight(sp, true, false); break;
	default: warning("Invalid move direction: %d", dir);
	}
	spritePosHistoryPush(sp);
	memSetVar16(18, 0); // ???
}

void mapSetLocationMode(MapLocationMode mode) {
	map.locationMode = mode;
	map.getLocationEnabled = mode != 0;
	map.prevLocation = kNoLocation;
}

static CcdSprite *getPlayer() {
	for (CcdSprite *sp = map.sprites.begin(); sp != map.sprites.end(); ++sp) {
		if (sp->state & kMapSpPlayer)
			return sp;
	}
	return nullptr;
}

static uint16 getSpriteLocation(CcdSprite *sp) {
	byte *eve = g_mem->fileData + memGetSysvar32(kSysVarEveOffset);
	for (; READ_LE_UINT16(eve + 0) != 0xffff; eve += 12) {
		uint16 xLeft = READ_LE_UINT16(eve + 2);
		uint16 yTop = READ_LE_UINT16(eve + 4);
		uint16 xRight = READ_LE_UINT16(eve + 6);
		uint16 yBot = READ_LE_UINT16(eve + 8);
		uint16 dirMask = eve[10];

		if (sp->x + (sp->w - 1) < xLeft || sp->x > xRight)
			continue;
		if (sp->y + sp->h <= yTop || sp->y >= yBot)
			continue;
		if (!(dirMask & (1 << (sp->frame >> FRAME_BITS))))
			continue;
		return READ_LE_UINT16(eve + 0);
	}
	return kNoLocation;
}

static uint16 getLocation() {
	if (!map.getLocationEnabled)
		return kNoLocation;

	CcdSprite *sp = getPlayer();
	if (!sp) {
		warning("no player sprite?");
		return kNoLocation;
	}

	uint16 loc = getSpriteLocation(sp);
	if (map.locationMode == kMapLocationModeOneshot) {
		map.getLocationEnabled = false;
	} else if (map.locationMode == kMapLocationModeNoRepeat) {
		if (loc == map.prevLocation)
			return kNoLocation;
		map.prevLocation = loc;
	}
	if (loc != kNoLocation) {
		MAP_LOG("mapGetLocation() -> %u", (uint)loc);
	}
	return loc;
}

void mapGetLocation() {
	memSetVar16(18, getLocation());
}

static bool mapPosEqual(MapPos a, MapPos b) {
	return a.x == b.x && a.y == b.y;
}

static uint hDistance(MapPos from, MapPos to) {
	// XXX: taxicab distance
	return ABS((int)to.x - (int)from.x) + ABS((int)to.y - (int)from.y);
}

static PathData *getPathData(MapPos pos) {
	return &map.path.tiles[pos.y][pos.x];
}

static bool frontierLessThan(uint16 a, uint16 b) {
	PathData *aData = getPathData(map.path.frontier[a]);
	PathData *bData = getPathData(map.path.frontier[b]);
	return aData->fScore < bData->fScore;
}

static uint16 frontierMin(uint16 a, uint16 b) {
	if (a >= map.path.frontier.size())
		a = 0xffff;
	if (b >= map.path.frontier.size())
		b = 0xffff;
	if (a == 0xffff)
		return b;
	if (b == 0xffff)
		return a;
	if (frontierLessThan(a, b))
		return a;
	return b;
}

static void frontierSink(uint16 node) {
	uint16 lChild = node * 2 + 1;
	uint16 rChild = node * 2 + 2;

	uint16 minI = frontierMin(node, frontierMin(lChild, rChild));
	if (minI != node) {
		MapPos tmp = map.path.frontier[minI];
		map.path.frontier[minI] = map.path.frontier[node];
		map.path.frontier[node] = tmp;
		frontierSink(minI);
	}
}

static MapPos frontierPop() {
	assert(map.path.frontier.size() > 0);
	MapPos r = map.path.frontier[0];
	if (map.path.frontier.size() > 1) {
		map.path.frontier[0] = popBack(map.path.frontier);
		frontierSink(0);
	}

	return r;
}

static void frontierSwim(uint16 node) {
	if (node == 0)
		return;

	uint16 parent = (node - 1) / 2;
	if (frontierLessThan(node, parent)) {
		MapPos tmp = map.path.frontier[parent];
		map.path.frontier[parent] = map.path.frontier[node];
		map.path.frontier[node] = tmp;
		frontierSwim(parent);
	}
}

static void frontierPush(MapPos pos) {
	map.path.frontier.push_back(pos);
	frontierSwim(map.path.frontier.size() - 1);
}

static bool mapTileCollides(uint x, uint y) {
	return map.tileData[y * map.cols + x].collides;
}

/* Character sprite collision differs between games:
 *
 * Doukyuusei  Kakyuusei
 * o--+--+--+  o--+--+
 * |  |  |  |  |  |  |
 * +--+--+--+  +--+--+
 * |//|//|//|  |//|//|
 * +--+--+--+  +--+--+
 * |//|//|//|
 * +--+--+--+
 */
static bool spritePosValid(uint x, uint y) {
	if (g_mapVersion == kMapVersionOld) {
		return !mapTileCollides(x, y + 1) && !mapTileCollides(x + 1, y + 1);
	}
	return !mapTileCollides(x, y + 1) && !mapTileCollides(x, y + 2)
		&& !mapTileCollides(x + 1, y + 1) && !mapTileCollides(x + 1, y + 2)
		&& !mapTileCollides(x + 2, y + 1) && !mapTileCollides(x + 2, y + 2);
}

static MapPos getNeighbor(MapPos pos, int dir) {
	switch (dir) {
	case kMapUp:
		if (pos.y == 0)
			goto noNeighbor;
		if (!spritePosValid(pos.x, pos.y - 1))
			goto noNeighbor;
		return makePos(pos.x, pos.y - 1);
	case kMapDown:
		if (pos.y >= map.rows - 1)
			goto noNeighbor;
		if (!spritePosValid(pos.x, pos.y + 1))
			goto noNeighbor;
		return makePos(pos.x, pos.y + 1);
	case kMapLeft:
		if (pos.x == 0)
			goto noNeighbor;
		if (!spritePosValid(pos.x - 1, pos.y))
			goto noNeighbor;
		return makePos(pos.x - 1, pos.y);
	case kMapRight:
		if (pos.x >= map.cols - 1)
			goto noNeighbor;
		if (!spritePosValid(pos.x + 1, pos.y))
			goto noNeighbor;
		return makePos(pos.x + 1, pos.y);
	case kMapUpLeft:
		if (pos.x == 0 || pos.y == 0)
			goto noNeighbor;
		if (!spritePosValid(pos.x - 1, pos.y - 1))
			goto noNeighbor;
		return makePos(pos.x - 1, pos.y - 1);
	case kMapUpRight:
		if (pos.x >= map.cols - 1 || pos.y == 0)
			goto noNeighbor;
		if (!spritePosValid(pos.x + 1, pos.y - 1))
			goto noNeighbor;
		return makePos(pos.x + 1, pos.y - 1);
	case kMapDownLeft:
		if (pos.x == 0 || pos.y >= map.rows - 1)
			goto noNeighbor;
		if (!spritePosValid(pos.x - 1, pos.y + 1))
			goto noNeighbor;
		return makePos(pos.x - 1, pos.y + 1);
	case kMapDownRight:
		if (pos.x >= map.cols - 1 || pos.y >= map.rows - 1)
			goto noNeighbor;
		if (!spritePosValid(pos.x + 1, pos.y + 1))
			goto noNeighbor;
		return makePos(pos.x + 1, pos.y + 1);
	}
noNeighbor:
	return makePos(0xffff, 0xffff);
}

/*
 * A* pathfinding algorithm.
 */
void mapPathSprite(uint spNo, uint tx, uint ty) {
	MAP_LOG("mapPathSprite(%u,%u,%u)", spNo, tx, ty);
	CcdSprite *sp = getSprite(spNo);
	if (!sp) {
		MAP_LOG("No sprite for pathing");
		return;
	}

	if (tx + 2 >= map.cols || ty < 1 || ty + 1 >= map.rows || map.tileData[ty * map.cols + tx].collides) {
		warning("Invalid pathing target: (%u,%u)", tx, ty);
		return;
	}

	// XXX: y-coord is center of character?
	ty--;

	if (!spritePosValid(tx, ty)) {
		warning("Invalid pathing target (collides): (%u,%u)", tx, ty);
		return;
	}

	MapPos start = makePos(sp->x, sp->y);
	map.path.goal = makePos(tx, ty);
	if (mapPosEqual(start, map.path.goal)) {
		MAP_LOG("Already at pathing goal");
		return;
	}

	// initialize path data
	memset(map.path.tiles, 0xff, sizeof(map.path.tiles));
	map.path.tiles[sp->y][sp->x].gScore = 0;
	map.path.tiles[sp->y][sp->x].fScore = hDistance(start, map.path.goal);

	// put start node into frontier
	map.path.frontier.clear();
	map.path.frontier.push_back(start);
	map.path.tiles[start.y][start.x].notInFrontier = 0;

	while (true) {
		if (map.path.frontier.size() == 0) {
			warning("pathing failed");
			return;
		}
		MapPos cur = frontierPop();
		map.path.tiles[cur.y][cur.x].notInFrontier = 1;
		if (mapPosEqual(cur, map.path.goal))
			break;

		// loop over neighbors
		for (int i = 0; i < 8; i++) {
			MapPos neighborPos = getNeighbor(cur, i);
			if (neighborPos.x == 0xffff)
				continue;

			PathData *neighbor = getPathData(neighborPos);
			uint16 g = map.path.tiles[cur.y][cur.x].gScore + (i <= 3 ? 1 : 2);
			if (g < neighbor->gScore) {
				neighbor->pred = cur;
				neighbor->gScore = g;
				neighbor->fScore = g + hDistance(neighborPos, map.path.goal);
				if (neighbor->notInFrontier) {
					frontierPush(neighborPos);
					neighbor->notInFrontier = 0;
				}
			}
		}
	}

	// reconstruct the path
	map.path.path.clear();
	MapPos cur = map.path.goal;
	do {
		map.path.path.push_back(cur);
		cur = map.path.tiles[cur.y][cur.x].pred;
	} while (!mapPosEqual(cur, start));
	map.path.pathPtr = map.path.path.size();


	// put sprite into pathing state
	map.path.active = true;
	map.path.saved.sp = sp;
	map.path.saved.state = sp->state;
	map.path.saved.cmd = sp->scriptCmd;
	map.path.saved.repetitions = sp->scriptRepetitions;
	sp->state = (sp->state & kMapSpCamera) | kMapSpPlayer | kMapSpEnabled;
	sp->scriptCmd = 13;
	sp->scriptRepetitions = 0xff;
}

void mapStopPathing() {
	MAP_LOG("mapStopPathing()");
	if (map.path.active) {
		map.path.active = false;
		map.path.saved.sp->state = map.path.saved.state;
		map.path.saved.sp->scriptCmd = map.path.saved.cmd;
		map.path.saved.sp->scriptRepetitions = map.path.saved.repetitions;
		if (map.locationMode != kMapLocationDisabled)
			map.getLocationEnabled = true;
	}
}

bool mapIsPathing() {
	return map.path.active;
}

void mapGetPathing() {
	//MAP_LOG("mapGetPathing()");
	memSetVar16(18, map.path.active ? 0xffff : 0);
}

void mapSkipPathing(uint spNo) {
	CcdSprite *sp = getSprite(spNo);
	if (!sp)
		return;
	sp->scriptCmd = 15;
}


} // End of namespace AI5
