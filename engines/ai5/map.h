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

#ifndef AI5_MAP_H
#define AI5_MAP_H

#include "common/scummsys.h"

namespace AI5 {

enum MapVersion {
	// Kakyuusei, Doukyuusei 2
	kMapVersionOld,
	// Doukyuusei
	kMapVersionNew
};

enum MapLocationMode {
	// Map.get_location is disabled
	kMapLocationDisabled = 0,
	// Map.get_location is enabled
	kMapLocationEnabled = 1,
	// Map.get_location is disabled after a valid location is returned
	kMapLocationModeOneshot = 2,
	// Map.get_location returns -1 if the previous location would be returned
	kMapLocationModeNoRepeat = 3
};

enum MapSpriteFlag {
	kMapSpEnabled     = 0x01,
	kMapSpTrigger     = 0x02,
	kMapSpNonchara    = 0x04,
	kMapSpTriggerable = 0x08,
	kMapSpCollides    = 0x10,
	kMapSpCamera      = 0x20,
	kMapSpPlayer      = 0x40
};

enum MapDirection {
	kMapUp = 0,
	kMapDown = 1,
	kMapLeft = 2,
	kMapRight = 3
};

enum MapDiagonal {
	kMapUpLeft = 4,
	kMapUpRight = 5,
	kMapDownLeft = 6,
	kMapDownRight = 7
};

extern MapVersion g_mapVersion;

void mapInit();
void mapFini();
void mapLoadBitmap(const char *name, uint col, uint row, uint which);
void mapLoadPalette(const char *name, uint which);
void mapLoadTilemap();
void mapLoadTiles();
void mapDrawTiles();
void mapLoadSpriteScripts();
void mapSetSpriteScript(uint spNo, uint scriptNo);
void mapSetSpriteState(uint no, byte state);
void mapSetSpriteAnim(uint spNo, byte animNo);
void mapSpawnSprite(uint spawnNo, uint spNo, byte animNo);
void mapRewindSpritePos(uint spNo, uint d);
void mapPlaceSprites();
void mapExecSprites();
void mapExecSpritesAndRedraw();
void mapMoveSprite(uint spNo, MapDirection dir);
void mapPathSprite(uint spNo, uint tx, uint ty);
void mapStopPathing();
bool mapIsPathing();
void mapGetPathing();
void mapSkipPathing(uint spNo);
void mapSetLocationMode(MapLocationMode mode);
void mapGetLocation();

} // End of namespace AI5

#endif // AI5_MAP_H
