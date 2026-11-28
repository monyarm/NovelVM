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

#include "common/file.h"
#include "common/list.h"
#include "common/memstream.h"
#include "common/textconsole.h"

#include "ai5/asset.h"
#include "ai5/config.h"

namespace AI5 {

static ArcArchive *g_arc[kNrAssetTypes];
static GameId g_assetGame;

Common::String g_assetMesName;
Common::String g_assetCgName;
Common::String g_assetBgmName;
Common::String g_assetEffectName;
Common::String g_assetVoiceName;
Common::String g_assetDataName;
bool g_assetEffectIsBgm = true;

struct CachedCG {
	Common::String name;
	Common::SharedPtr<CG> cg;
};

enum {
	kCGCacheSize = 16
};

static Common::List<CachedCG> g_cgCache;

static ArcArchive *openArc(const Common::String &name, uint flags) {
	return ArcArchive::open(Common::Path(name), g_assetGame, flags);
}

void assetInit(GameId game) {
	g_assetGame = game;
	uint lzss = game == GID_SHUUSAKU ? kArcCompressed | kArcBitwise : kArcCompressed;
	uint mesFlags = g_config.mesType ? lzss : 0;
	uint dataFlags = g_config.dataType ? lzss : 0;

	const struct {
		AssetType type;
		const ConfigArcFile *file;
		uint flags;
		bool required;
	} arcs[] = {
		{ kAssetBg, &g_config.file.bg, 0, false },
		{ kAssetMes, &g_config.file.mes, mesFlags, true },
		{ kAssetBgm, &g_config.file.bgm, 0, false },
		{ kAssetVoice, &g_config.file.voice, 0, false },
		{ kAssetVoice2, &g_config.file.voice2, 0, false },
		{ kAssetVoicesub, &g_config.file.voicesub, 0, false },
		{ kAssetEffect, &g_config.file.effect, 0, false },
		{ kAssetData, &g_config.file.data, dataFlags, false },
		{ kAssetPriv, &g_config.file.priv, 0, false },
		{ kAssetMovie, &g_config.file.movie, 0, false },
		{ kAssetSysse, &g_config.file.sysse, 0, false }
	};
	for (uint i = 0; i < ARRAYSIZE(arcs); i++) {
		if (!arcs[i].file->arc)
			continue;
		if (!(g_arc[arcs[i].type] = openArc(arcs[i].file->name, arcs[i].flags))) {
			if (arcs[i].required)
				error("Failed to open archive \"%s\"", arcs[i].file->name.c_str());
			warning("Failed to open archive \"%s\"", arcs[i].file->name.c_str());
		}
	}
	g_cgCache.clear();
}

void assetFini() {
	for (uint i = 0; i < kNrAssetTypes; i++) {
		delete g_arc[i];
		g_arc[i] = nullptr;
	}
	g_cgCache.clear();
}

static bool setArchive(const char *name, AssetType t, ConfigArcFile &cfg) {
	if (cfg.arc && cfg.name.equalsIgnoreCase(name))
		return true;

	ArcArchive *ar = openArc(name, 0);
	if (!ar) {
		warning("Failed to open %s", name);
		return false;
	}
	delete g_arc[t];
	g_arc[t] = ar;
	cfg.arc = true;
	cfg.name = name;
	return true;
}

bool assetSetVoiceArchive(const char *name) { return setArchive(name, kAssetVoice, g_config.file.voice); }
bool assetSetVoice2Archive(const char *name) { return setArchive(name, kAssetVoice2, g_config.file.voice2); }
bool assetSetVoice3Archive(const char *name) { return setArchive(name, kAssetVoice3, g_config.file.voice3); }
bool assetSetVoice4Archive(const char *name) { return setArchive(name, kAssetVoice4, g_config.file.voice4); }

static Common::SeekableReadStream *assetFsLoad(const char *name) {
	Common::String path(name);
	path.replace('\\', '/');

	Common::File *f = new Common::File();
	if (f->open(Common::Path(path, '/')))
		return f;

	// YU-NO English translation: load .ogg if .wav is not found
	if (path.hasSuffixIgnoreCase(".wav")) {
		path = path.substr(0, path.size() - 3) + "ogg";
		if (f->open(Common::Path(path, '/')))
			return f;
	}
	delete f;
	return nullptr;
}

static Common::SeekableReadStream *arcLoad(AssetType t, const char *name, Common::String *lastName) {
	if (!g_arc[t])
		return assetFsLoad(name);
	Common::SeekableReadStream *s = g_arc[t]->createReadStreamForMember(Common::Path(name));
	if (s && lastName)
		*lastName = name;
	return s;
}

Common::SeekableReadStream *assetLoad(AssetType t, const char *name) {
	return arcLoad(t, name, nullptr);
}

Common::SeekableReadStream *assetMesLoad(const char *name) {
	return arcLoad(kAssetMes, name, &g_assetMesName);
}

Common::SeekableReadStream *assetBgmLoad(const char *name) {
	return arcLoad(kAssetBgm, name, &g_assetBgmName);
}

Common::SeekableReadStream *assetEffectLoad(const char *name) {
	if (g_assetEffectIsBgm)
		return assetBgmLoad(name);
	return arcLoad(kAssetEffect, name, &g_assetEffectName);
}

Common::SeekableReadStream *assetVoiceLoad(const char *name) {
	static const AssetType voices[] = { kAssetVoice, kAssetVoice2, kAssetVoice3, kAssetVoice4 };
	for (uint i = 0; i < ARRAYSIZE(voices); i++) {
		if (!g_arc[voices[i]])
			continue;
		Common::SeekableReadStream *s = g_arc[voices[i]]->createReadStreamForMember(Common::Path(name));
		if (s) {
			g_assetVoiceName = name;
			return s;
		}
	}
	return assetFsLoad(name);
}

const ArcArchive::Entry *assetVoiceEntry(const char *name, ArcArchive::Type &type) {
	static const AssetType voices[] = { kAssetVoice, kAssetVoice2, kAssetVoice3, kAssetVoice4 };
	for (uint i = 0; i < ARRAYSIZE(voices); i++) {
		if (!g_arc[voices[i]])
			continue;
		const ArcArchive::Entry *e = g_arc[voices[i]]->findEntry(Common::Path(name));
		if (e) {
			type = g_arc[voices[i]]->getType();
			return e;
		}
	}
	return nullptr;
}

Common::SeekableReadStream *assetVoicesubLoad(const char *name) {
	return arcLoad(kAssetVoicesub, name, &g_assetVoiceName);
}

Common::SeekableReadStream *assetDataLoad(const char *name) {
	return arcLoad(kAssetData, name, &g_assetDataName);
}

bool assetCgLoadRaw(const char *name, Common::Array<byte> &data) {
	Common::ScopedPtr<Common::SeekableReadStream> s(arcLoad(kAssetBg, name, &g_assetCgName));
	if (!s) {
		// The 2003 Kawarazaki re-release keeps its images in .gcc
		// members while its scripts ask for .bmp and .gpx names.
		Common::String alt(name);
		if (alt.hasSuffixIgnoreCase(".bmp") ||
				alt.hasSuffixIgnoreCase(".gpx")) {
			alt.erase(alt.size() - 4);
			alt += ".gcc";
			s.reset(arcLoad(kAssetBg, alt.c_str(), &g_assetCgName));
		}
	}
	if (!s)
		return false;
	data.resize(s->size());
	return s->read(data.data(), data.size()) == data.size();
}

Common::SharedPtr<CG> assetCgDecode(const char *name, const Common::Array<byte> &data) {
	for (Common::List<CachedCG>::iterator it = g_cgCache.begin(); it != g_cgCache.end(); ++it) {
		if (it->name.equalsIgnoreCase(name)) {
			// move to front of cache
			CachedCG entry = *it;
			g_cgCache.erase(it);
			g_cgCache.push_front(entry);
			return entry.cg;
		}
	}

	CGType type;
	if (!cgTypeFromName(name, type)) {
		warning("Unrecognized CG type: %s", name);
		return Common::SharedPtr<CG>();
	}
	Common::SharedPtr<CG> cg(loadCG(data.data(), data.size(), type));
	if (!cg)
		return cg;

	if (g_cgCache.size() >= kCGCacheSize)
		g_cgCache.pop_back();
	CachedCG entry;
	entry.name = name;
	entry.cg = cg;
	g_cgCache.push_front(entry);
	return cg;
}

Common::SharedPtr<CG> assetCgLoad(const char *name) {
	Common::Array<byte> data;
	if (!assetCgLoadRaw(name, data))
		return Common::SharedPtr<CG>();
	// Decode by the member name that was actually loaded; the 2003
	// Kawarazaki re-release names its .bmp and .gpx images .gcc.
	return assetCgDecode(g_assetCgName.empty() ? name : g_assetCgName.c_str(), data);
}

} // End of namespace AI5
