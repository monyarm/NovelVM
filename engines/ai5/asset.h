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

#ifndef AI5_ASSET_H
#define AI5_ASSET_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "ai5/archive.h"
#include "ai5/cg.h"

namespace Common {
class SeekableReadStream;
}

namespace AI5 {

enum AssetType {
	kAssetBg,
	kAssetMes,
	kAssetBgm,
	kAssetVoice,
	kAssetVoice2,
	kAssetVoice3,
	kAssetVoice4,
	kAssetVoicesub,
	kAssetEffect,
	kAssetData,
	kAssetPriv,
	kAssetMovie,
	kAssetSysse,
	kNrAssetTypes
};

void assetInit(GameId game);
void assetFini();

bool assetSetVoiceArchive(const char *name);
bool assetSetVoice2Archive(const char *name);
bool assetSetVoice3Archive(const char *name);
bool assetSetVoice4Archive(const char *name);

Common::SeekableReadStream *assetLoad(AssetType t, const char *name);
Common::SeekableReadStream *assetMesLoad(const char *name);
Common::SeekableReadStream *assetBgmLoad(const char *name);
Common::SeekableReadStream *assetEffectLoad(const char *name);
Common::SeekableReadStream *assetVoiceLoad(const char *name);
Common::SeekableReadStream *assetVoicesubLoad(const char *name);
Common::SeekableReadStream *assetDataLoad(const char *name);

/** Returns the archive entry for a voice file, for AWD loop points. */
const ArcArchive::Entry *assetVoiceEntry(const char *name, ArcArchive::Type &type);

/** Read a CG file's raw bytes from the BG archive. */
bool assetCgLoadRaw(const char *name, Common::Array<byte> &data);
/** Decode a CG (cached by name). */
Common::SharedPtr<CG> assetCgDecode(const char *name, const Common::Array<byte> &data);
Common::SharedPtr<CG> assetCgLoad(const char *name);

extern Common::String g_assetMesName;
extern Common::String g_assetCgName;
extern Common::String g_assetBgmName;
extern Common::String g_assetEffectName;
extern Common::String g_assetVoiceName;
extern Common::String g_assetDataName;
// default: true
extern bool g_assetEffectIsBgm;

} // End of namespace AI5

#endif // AI5_ASSET_H
