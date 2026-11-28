/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 */

#ifndef AI5_GAMES_SHUUSAKU_H
#define AI5_GAMES_SHUUSAKU_H

#include "common/events.h"

namespace AI5 {

struct Game;

enum SchedLocation {
	kLocNagisa,
	kLocKaori,
	kLocShiho,
	kLocChiaki,
	kLocAsami,
	kLocMoeko,
	kLocEri,
	kLocAyaka,
	kLocToilet,
	kLocChanging,
	kLocKanrinin,
	kNrLoc
};

enum SchedCharacter {
	kCharNagisa,
	kCharKaori,
	kCharShiho,
	kCharChiaki,
	kCharAsami,
	kCharMoeko,
	kCharEri,
	kCharAyaka,
	kNrChar
};

enum SchedCamera {
	kCamTypeNone,
	kCamTypeVideo,
	kCamTypeDigi
};

enum SchedDay {
	kDaySun = 0,
	kDayMon = 1,
	kDaySat = 6
};

enum SchedFlag {
	kSchedFlagUnknown = 0,
	kSchedFlagEmpty = 1,
	kSchedFlagOccupied = 2,
	kSchedFlagEvent = 4,
	kSchedFlagPink = 8
};

#define kNrIntervals 144
#define kMaskColor 10

struct SchedAwayEvent {
	uint8 t;
	uint8 character;
	uint16 flagNo;
};

struct SchedCamEventEntry {
	uint16 flagNo;
	const char *name;
	const char *zoomName;
};

struct SchedCamEvent {
	uint8 t;
	SchedCamEventEntry entries[3];
};

struct Scene {
	uint16 flagNo;
	int sceneId;
	const char *cgName;
};

struct ShuusakuMenuEntry {
	uint32 bodyAddr;
	int index;
};

// schedule window (in-screen overlay)
void shuusakuScheduleWindowInit();
void shuusakuScheduleWindowToggle();
bool shuusakuScheduleWindowEvent(const Common::Event &e);
void shuusakuScheduleTick();
void shuusakuScheduleUpdate();
void shuusakuScheduleSetFlag(int loc, int day, int t, int flag);
void shuusakuScheduleSetPlanTime(int day, int t);
void shuusakuScheduleClearPlan();
int shuusakuAbsoluteTime(int day, int t);
bool shuusakuSubwindowValid();

// status window (in-screen overlay)
void shuusakuStatusInit();
void shuusakuStatusWindowToggle();
bool shuusakuStatusWindowEvent(const Common::Event &e);
void shuusakuStatusUpdate();

// selection menu
int shuusakuMenuexec(ShuusakuMenuEntry *entries, int nr, int mode);

// name input
void shuusakuNameInputScreen(byte *myouji, byte *namae);

// scene viewer
int shuusakuSceneViewerCharSelect();
int shuusakuSceneViewerSceneSelect(int ch, bool needBg);

// shared drawing/effects (shuusaku.cpp)
void shuusakuDrawText(const char *text);
void shuusakuUpdatePalette(const byte *pal);
void shuusakuCrossfade(const byte *pal, bool allow1632);
void shuusakuCrossfadeTo(int r, int g, int b);
void shuusakuCrossfade2(const byte *pal);
void shuusakuPlayMovie(const char *name);
void shuusakuAfterMovieCrossfade();
void shuusakuZoom(int x, int y, int w, int h, int srcI);
void shuusakuCamEventZoom(int cgX, int cgY, int cgW, int cgH);

extern bool g_shuusakuRunningCamEvent;

void initGameShuusaku(Game &g);

} // namespace AI5

#endif // AI5_GAMES_SHUUSAKU_H
