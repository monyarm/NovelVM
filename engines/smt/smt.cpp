#include "audio/audiostream.h"
#include "audio/decoders/atrac3plus.h"
#include "audio/mixer.h"
#include "common/algorithm.h"
#include "common/list.h"
#include "common/memstream.h"
#include "video/mpegps_decoder.h"
#include "common/formats/cpk.h"
#include "common/formats/cvm.h"
#include "common/formats/adx.h"
#include "common/formats/dds.h"
#include "common/formats/pmf.h"
#include "common/formats/usm.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/surface.h"
#include "smt/formats/archive/pac.h"
#include "smt/formats/script/bmd.h"
#include "smt/formats/texture/tmx.h"
#include "graphics/gmo_render.h"
#include "common/formats/rmd.h"
#include "graphics/rmd_render.h"
#include "graphics/rmd_render_mesh.h"
#include "graphics/epl_effect.h"
#include "graphics/afpack_trail.h"
#include "common/formats/epl.h"
#ifdef USE_OPENGL_SHADERS
#include "graphics/opengl/blitter.h"
#endif

#include "common/debug_demo_view.h"
#include "smt/smt.h"
#include "engines/util.h"

using Common::DebugDemoView;



namespace {

// Every phase of the demo now renders through OpenGL. These helpers keep the
// loop free of #ifdef noise; without shader support they are no-ops (the
// engine errors out at startup before reaching the loop in that case).
#ifdef USE_OPENGL_SHADERS
void renderClear(byte r, byte g, byte b) {
	OpenGL::Blitter::clear(r, g, b);
}

void renderSurface(const Graphics::Surface &s, int x, int y, int w, int h, bool refresh = true) {
	OpenGL::Blitter::drawSurface(s, x, y, w, h, refresh);
}

void renderText(const Graphics::Font &font, const Common::String &text, int x, int y, int maxWidth,
                uint32 color, Graphics::TextAlign align = Graphics::kTextAlignLeft) {
	OpenGL::Blitter::drawText(font, text, x, y, maxWidth, color, align);
}
#else
void renderClear(byte, byte, byte) {}
void renderSurface(const Graphics::Surface &, int, int, int, int, bool = true) {}
void renderText(const Graphics::Font &, const Common::String &, int, int, int, uint32,
                Graphics::TextAlign = Graphics::kTextAlignLeft) {}
#endif

/**
 * Scale a source size to fit inside the screen preserving aspect ratio,
 * letterboxing (centering) the result.
 */
void fitToScreen(int srcW, int srcH, int screenW, int screenH, int &dstW, int &dstH, int &dstX, int &dstY) {
	dstW = srcW;
	dstH = srcH;
	if (srcW <= 0 || srcH <= 0) {
		dstX = dstY = 0;
		return;
	}
	if ((int)srcW * screenH > screenW * srcH) {
		dstW = screenW;
		dstH = srcH * screenW / srcW;
	} else {
		dstH = screenH;
		dstW = srcW * screenH / srcH;
	}
	dstX = (screenW - dstW) / 2;
	dstY = (screenH - dstH) / 2;
}

/**
 * Real per-game screen resolution, matching each title's actual console:
 * PS2 titles (P3/P3F/P4) already had 640x480 verified against a real P3
 * FES opening (real video letterboxed to 640x368 within it); PSP's P3P
 * uses its real, well-known native resolution (no local PSP corpus was
 * checked for an exact in-game framebuffer size this session). P4G is
 * real Vita hardware native res (also unverified locally). P5/P5R use
 * real-data-verified 1920x1080 -- confirmed via this session's USM work
 * (mov050.usm's real video metadata) -- rather than a guessed Vita native
 * res, since detection.cpp's P5/P5R entries are matched against a PC-
 * distributed data.cpk whose real assets are authored at that resolution,
 * not the Vita's 960x544 screen.
 */
void getScreenSizeForGame(const Common::String &gameId, int16 &width, int16 &height) {
	if (gameId == "P3P") {
		width = 480;
		height = 272;
	} else if (gameId == "P4G") {
		width = 960;
		height = 544;
	} else if (gameId == "P5" || gameId == "P5R") {
		width = 1920;
		height = 1080;
	} else {
		// P3 / P3F / P4 (PS2)
		width = 640;
		height = 480;
	}
}

// Game -> real opening video filename. Multiple rows per gameId are tried
// in order (currently only used by the archive-suffix-scan path below; P3P
// has a single loose-file path, not a fallback chain -- its "B" file is
// unrelated content, not an alternate opening). P3F's real DATA.CVM (FES)
// carries both the original P3CTOP.SFD (2006) and P3CTOP2.SFD (2007-12,
// added for the FES re-release); FES's real opening is the latter.
struct OpeningVideoCandidate { const char *gameId; const char *path; };
static const OpeningVideoCandidate kOpeningVideoCandidates[] = {
	{ "P3",  "P3CTOP.SFD" },
	{ "P3F", "P3CTOP2.SFD" },
	{ "P4",  "P4CTOP1.SFD" },
	{ "P3P", "sound/pmsf/P3OPMV_P3P.pmsf" },
};

// First table path for gameId, or empty if none (used by loose-file titles
// like P3P that don't scan an archive).
Common::String openingVideoPathFor(const Common::String &gameId) {
	for (uint c = 0; c < ARRAYSIZE(kOpeningVideoCandidates); ++c)
		if (gameId == kOpeningVideoCandidates[c].gameId)
			return kOpeningVideoCandidates[c].path;
	return Common::String();
}

/**
 * Finds title-specific PS2 opening names in DATA.CVM via kOpeningVideoCandidates.
 */
Common::Path findOpeningVideo(Common::Archive &archive, const Common::String &gameId) {
	Common::ArchiveMemberList list;
	archive.listMembers(list);

	for (uint c = 0; c < ARRAYSIZE(kOpeningVideoCandidates); ++c) {
		if (gameId != kOpeningVideoCandidates[c].gameId)
			continue;
		Common::String expected = kOpeningVideoCandidates[c].path;
		for (Common::ArchiveMemberList::iterator it = list.begin(); it != list.end(); ++it) {
			Common::String name = (*it)->getName();
			Common::String upper = name;
			upper.toUppercase();
			if (upper.hasSuffix(expected))
				return Common::Path(name);
		}
	}
	return Common::Path();
}

// P4G/P5/P5R's data.cpk best-effort PMF/USM scan is disabled -- see the
// commented-out call site below (data.cpk is CRI-encrypted for these
// titles, confirmed on a real P5R USM; findOpeningVideo(archive, ext)
// can't reach anything real until that's solved). Kept for when that
// decryption work lands.
// Common::Path findOpeningVideo(Common::Archive &archive, const char *ext) {
// 	Common::ArchiveMemberList list;
// 	archive.listMembers(list);
// 	for (Common::ArchiveMemberList::iterator it = list.begin(); it != list.end(); ++it) {
// 		Common::String name = (*it)->getName();
// 		Common::String upper = name;
// 		upper.toUppercase();
// 		if (upper.hasSuffix(ext) && (upper.contains("TOP") || upper.contains("OP")))
// 			return Common::Path(name);
// 	}
// 	return Common::Path();
// }

struct RmdEntry {
	Common::String label;
	Common::String source;
	Common::String archivePath;
	Common::String member;
	Common::String modelPath;
	uint32 offset = 0;
	uint32 size = 0;
	Common::Archive *archive = nullptr;
	bool battle = false;
	bool persona = false;
	bool npc = false;
};

// One archive still waiting to be scanned for RMD/nested-archive members.
// discoverRmdMembers used to recurse into a nested PAC/BIN/FPC immediately;
// stepRmdDiscovery instead pushes it here and returns, so a caller can
// spread the full tree walk (real corpora can nest thousands of archives)
// across many small steps instead of blocking until the whole tree is done.
struct RmdDiscoveryWork {
	Common::Archive *archive;
	Common::Array<Common::String> parentPath;
	uint depth;
	bool battle;
	Common::String source;
};

// Scans exactly one archive's member list, appending any RMD members found
// to `entries` and queuing any nested PAC/BIN/FPC archives onto `work`
// (instead of recursing into them) for a later step to pick up.
void discoverRmdMembersStep(const RmdDiscoveryWork &item, Common::Array<RmdEntry> &entries,
		Common::Array<SMT::Format::Archive::PAC *> &nestedArchives, Common::Array<RmdDiscoveryWork> &work) {
	if (item.depth > 2)
		return;
	Common::ArchiveMemberList members;
	item.archive->listMembers(members);
	Common::Array<Common::String> names;
	for (Common::ArchiveMemberList::iterator it = members.begin(); it != members.end(); ++it)
		names.push_back((*it)->getName());
	Common::sort(names.begin(), names.end());

	for (uint i = 0; i < names.size(); ++i) {
		Common::String upper = names[i];
		upper.toUppercase();
		Common::Array<Common::String> path = item.parentPath;
		path.push_back(names[i]);
		if (upper.hasSuffix(".RMD")) {
			RmdEntry entry;
			entry.source = item.source;
			entry.archivePath = item.parentPath.empty() ? item.source : item.parentPath[0];
			for (uint p = 1; p < item.parentPath.size(); ++p)
				entry.archivePath = Common::String::format("%s/%s", entry.archivePath.c_str(), item.parentPath[p].c_str());
			entry.member = names[i];
			entry.modelPath = entry.archivePath.empty() ? entry.member :
				Common::String::format("%s/%s", entry.archivePath.c_str(), entry.member.c_str());
			entry.archive = item.archive;
			SMT::Format::Archive::PAC *pac = dynamic_cast<SMT::Format::Archive::PAC *>(item.archive);
			if (pac)
				pac->getMemberRange(Common::Path(names[i]), entry.offset, entry.size);
			entry.battle = item.battle;
			entry.label = names[i];
			for (uint p = 0; p + 1 < path.size(); ++p)
				entry.label = Common::String::format("%s/%s", path[p].c_str(), entry.label.c_str());
			// Real, confirmed directory convention: MODEL/PERSONA/PS<NNN>.RMD
			// (verified against the real P3 FES corpus). Check the full
			// assembled path/label, not just the bare per-container member
			// name -- nested archives may store either a leaf filename or a
			// path-qualified one depending on how they're packed, so a
			// filename-only check missed real personas here before.
			Common::String upperLabel = entry.label;
			upperLabel.toUppercase();
			entry.persona = upperLabel.contains("PERSONA/");
			entry.npc = upperLabel.contains("NPC/");
			entries.push_back(entry);
		} else if (upper.hasSuffix(".PAC") || upper.hasSuffix(".BIN") || upper.hasSuffix(".FPC")) {
			Common::SeekableReadStream *stream = item.archive->createReadStreamForMember(Common::Path(names[i]));
			if (!stream)
				continue;
			SMT::Format::Archive::PAC *nested = new SMT::Format::Archive::PAC(stream);
			nestedArchives.push_back(nested);
			work.push_back({nested, path, item.depth + 1, item.battle, item.source});
		}
	}
}

// Pops and processes pending archives for up to budgetMs. Returns true once
// the worklist is empty (discovery complete). Time-boxed rather than
// count-boxed since individual archives vary hugely in cost (a top-level
// CVM listing vs. a small nested nametable).
bool stepRmdDiscovery(Common::Array<RmdDiscoveryWork> &work, Common::Array<RmdEntry> &entries,
		Common::Array<SMT::Format::Archive::PAC *> &nestedArchives, uint32 budgetMs) {
	uint32 start = g_system->getMillis();
	while (!work.empty()) {
		RmdDiscoveryWork item = work.back();
		work.pop_back();
		discoverRmdMembersStep(item, entries, nestedArchives, work);
		if (g_system->getMillis() - start >= budgetMs)
			break;
	}
	return work.empty();
}

Common::RMD *loadRmdEntry(const RmdEntry &entry) {
	if (!entry.archive || entry.member.empty())
		return nullptr;
	Common::SeekableReadStream *stream = entry.archive->createReadStreamForMember(Common::Path(entry.member));
	if (!stream)
		return nullptr;
	Common::RMD *model = new Common::RMD(stream);
	delete stream;
	return model;
}

// Effect textures: the 22 global ones in DATA.CVM effect/effpack.bin, and TMXs embedded in
// effect trees (decoded on first use, keyed by their bytes' address, which the trees keep).
class DemoEffectTextures : public Graphics::EffectTextureSource {
public:
	explicit DemoEffectTextures(Common::Archive *data) {
		_global.resize(Graphics::kTrailTextureSlot + 1, nullptr);
		Common::ScopedPtr<Common::SeekableReadStream> trail(data ? data->createReadStreamForMember(Common::Path("effect/z0.tmx")) : nullptr);
		if (trail)
			_global[Graphics::kTrailTextureSlot] = decode(*trail);
		Common::SeekableReadStream *stream = data ? data->createReadStreamForMember(Common::Path("effect/effpack.bin")) : nullptr;
		if (!stream)
			return;
		SMT::Format::Archive::PAC pack(stream);
		for (uint i = 0; i < 22; ++i) {
			Common::ScopedPtr<Common::SeekableReadStream> member(pack.createReadStreamForMember(Common::Path(Common::String::format("be_%04u.tmx", i + 1))));
			if (member)
				_global[i] = decode(*member);
		}
	}
	~DemoEffectTextures() override {
		for (uint i = 0; i < _global.size(); ++i)
			release(_global[i]);
		clearEmbedded();
	}
	/** Call when the effect trees go away: their addresses may be reused by new ones. */
	void clearEmbedded() {
		for (uint i = 0; i < _embedded.size(); ++i)
			release(_embedded[i].surface);
		_embedded.clear();
	}
	bool textureSize(const Graphics::EffectTextureRef &ref, int &width, int &height) override {
		const Graphics::Surface *surface = textureSurface(ref);
		if (!surface)
			return false;
		width = surface->w;
		height = surface->h;
		return true;
	}
	const Graphics::Surface *textureSurface(const Graphics::EffectTextureRef &ref) override {
		if (ref.globalSlot >= 0)
			return ref.globalSlot < (int)_global.size() ? _global[ref.globalSlot] : nullptr;
		if (!ref.embeddedTMX)
			return nullptr;
		for (uint i = 0; i < _embedded.size(); ++i)
			if (_embedded[i].data == ref.embeddedTMX->data())
				return _embedded[i].surface;
		SMT::TMX tmx(ref.embeddedTMX->data(), ref.embeddedTMX->size());
		Embedded entry = {ref.embeddedTMX->data(), tmx.getSurface()};
		_embedded.push_back(entry);
		return entry.surface;
	}

private:
	struct Embedded { const byte *data; Graphics::Surface *surface; };
	Common::Array<Graphics::Surface *> _global;
	Common::Array<Embedded> _embedded; // a handful per model

	static Graphics::Surface *decode(Common::SeekableReadStream &stream) {
		SMT::TMX tmx(&stream);
		return tmx.getSurface();
	}
	static void release(Graphics::Surface *surface) {
		if (surface) {
			surface->free();
			delete surface;
		}
	}
};

// One live effect of the demo model: an E0 block instanced at one helper (§ "first..last").
struct DemoEffect {
	Graphics::EPLEffect *effect = nullptr;
	uint32 helper = 0;
	int32 slot = -1;  ///< -1: model-wide
	uint32 flags = 0; ///< bit 0: restart when the slot's animation loops
};

void advanceDemoEffects(const Common::RMD &model, Common::Array<DemoEffect> &effects, int32 slot,
		const Common::RMD::BonePose *pose, const Graphics::EffectCamera &camera) {
	for (uint i = 0; i < effects.size(); ++i) {
		DemoEffect &e = effects[i];
		if (e.slot >= 0 && e.slot != slot)
			continue;
		Common::RMD::Matrix m;
		model.helperWorldMatrix(e.helper, pose, m); // falls back to the model matrix (identity)
		float position[3], rotation[4] = {0, 0, 0, 1};
		Graphics::EffectMath::matrixToPose(m.m, position, rotation);
		if (e.helper >= 1000)
			rotation[0] = rotation[1] = rotation[2] = 0, rotation[3] = 1;
		e.effect->setPose(position, rotation);
		e.effect->advance(camera);
	}
}

struct DemoHelperContext {
	const Common::RMD *model;
	const Common::RMD::BonePose *pose;
};

bool demoHelperPosition(void *context, uint32 helper, float position[3]) {
	const DemoHelperContext *c = (const DemoHelperContext *)context;
	Common::RMD::Matrix m;
	if (!c->model->helperWorldMatrix(helper, c->pose, m))
		return false;
	for (int k = 0; k < 3; ++k)
		position[k] = m.m[12 + k];
	return true;
}

bool rmdMatches(const RmdEntry &entry, const Common::String &name) {
	Common::String lower = entry.modelPath;
	lower.toLowercase();
	return lower.hasSuffix(name);
}

} // anonymous namespace

namespace SMT {


SMTEngine::SMTEngine(OSystem *syst, const ADGameDescription *desc)
    : Engine(syst), _gameDescription(desc), _console(nullptr)//, _gfx(0)
	{
	// Put your engine in a sane state, but do nothing big yet;
	// in particular, do not load data from files; rather, if you
	// need to do such things, do them from run().

	// Do not initialize graphics here
	// Do not initialize audio devices here

	// However this is the place to specify all default directories
	const Common::FSNode gameDataDir(Common::Path(ConfMan.get("path")));
	//SearchMan.addSubDirectoryMatching(gameDataDir, "sound/pmsf");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("smt");

	debug("SMTEngine::SMTEngine");
}

SMTEngine::~SMTEngine() {
	debug("SMTEngine::~SMTEngine");

	// Dispose your resources here
	delete _rnd;
}

Common::Error SMTEngine::run() {
	Common::String gameId(getGameId());
	int16 screenWidth, screenHeight;
	getScreenSizeForGame(gameId, screenWidth, screenHeight);

	// The whole demo (video, texture, model) is rendered through OpenGL.
	// initGraphics3d issues kGfxModeRender3d, which makes OSystem_SDL switch
	// to the OpenGL graphics manager, so GMORenderer::isAvailable() becomes
	// true before the model check below.
	initGraphics3d(screenWidth, screenHeight);
#ifdef USE_OPENGL_SHADERS
	if (!OpenGL::Blitter::init(screenWidth, screenHeight))
		warning("SMT: GLBlitter initialization failed");
#endif
	if (!g_system->hasFeature(OSystem::kFeatureOpenGLForGame))
		error("SMT requires OpenGL; run with --gfx-mode=opengl");
	debug("SMT: requested %dx%d, OpenGL screen now %dx%d", screenWidth, screenHeight,
	      g_system->getWidth(), g_system->getHeight());

	//_frameLimiter = new FrameLimiter(_system, ConfMan.getInt("engine_speed"));
	//CPK::CPKFile _cpk = CPK::CPKFile("umd0.cpk");

	//Format::Graphic::DDSFile _dds("test/DXT5.dds");

	// Real-data check for the rewritten ADX decoder: every track in the
	// game's BGM.CVM is a real, cycleable candidate for the "texture + BGM"
	// phase (see loadBgmIndex() below, declared once gameCpk -- needed for
	// P3P's loose-ADX fallback -- is known). Common::File (used internally
	// by CVM) resolves relative to SearchMan, not the filesystem directly --
	// the Engine base class already mounts the detected game directory
	// there, the same way "DATA.CVM" in detection.cpp resolves. BGM.CVM only
	// exists for the PS2 titles; CVM's own constructor warns and produces an
	// empty archive otherwise, handled below same as any other missing file.
	struct BgmEntry {
		Common::String label;   // e.g. "BGM.CVM/bgm042.adx"
		Common::String member;  // archive member name
		bool fromCpk = false;   // true => read via gameCpk, false => bgmCvm
	};
	Common::CVM bgmCvm("BGM.CVM");
	Common::ArchiveMemberList adxList;
	bgmCvm.listMembers(adxList);
	Audio::SoundHandle adxHandle;
	Audio::SeekableAudioStream *adxAudio = nullptr;
	// True once adxAudio has been handed to _mixer->playStream(). From then
	// on the mixer owns its deletion (DisposeAfterUse::YES) -- deleting it
	// ourselves right after _mixer->stopHandle() raced the mixer's own
	// audio thread (stopHandle() doesn't guarantee an in-flight read on
	// this object has finished), which segfaulted on real BGM cycling.
	bool adxOwnedByMixer = false;
	Common::Array<BgmEntry> bgmEntries;
	int bgmIndex = 0;
	// Cached once at load time -- never call adxAudio->getLength() from the
	// render/HUD path while it's playing: the mixer thread is concurrently
	// reading from the same stream object, and querying it live from the
	// main thread races that read (this crashed real runs).
	int bgmDurationSecs = 0;
	if (!adxList.empty()) {
		for (Common::ArchiveMemberList::iterator it = adxList.begin(); it != adxList.end(); ++it) {
			BgmEntry entry;
			entry.member = (*it)->getName();
			entry.label = Common::String::format("BGM.CVM/%s", entry.member.c_str());
			bgmEntries.push_back(entry);
		}
	} else {
		// P3P has no BGM.CVM -- its ADX tracks live loose in umd0.cpk. That
		// fallback list is built after gameCpk is known, below.
		debug("SMT: BGM.CVM has no members (or could not be opened) -- expected for non-PS2 titles; BGM ADX will be picked from the game CPK once it's open");
	}

	// Real per-game opening video: PS2 titles (P3/P3F/P4) keep their
	// cutscenes as SFD (MPEG-PS + embedded CRI ADX) inside DATA.CVM --
	// real-data-verified this session for P3F (P3CTOP.SFD) and P4
	// (P4CTOP1.SFD/P4CTOP2.SFD, discovered via the same real DATA.CVM).
	// P3P's openings are loose PSMF files (sound/pmsf, see below), not
	// inside its CPKs. P4G/P5/P5R have no local real corpus; the best-effort
	// CPK scan for a PMF/USM opening is untested for them.
	Video::MPEGPSDecoder openingDecoder;
	bool openingLoaded = false;
	Common::SeekableReadStream *openingStream = nullptr;
	Common::String openingSourceDesc;
	bool openingAudioStarted = false;

	Common::CVM *dataCvm = nullptr;
	Common::CVM *battleCvm = nullptr;
	Common::CPK *gameCpk = nullptr;
	Common::Array<RmdEntry> rmdEntries;
	Common::Array<SMT::Format::Archive::PAC *> nestedRmdArchives;
	// Real DATA.CVM/BTL.CVM trees nest thousands of archives (P4's corpus
	// alone measured ~4s of real work here) -- discovery runs incrementally
	// via stepRmdDiscovery() from the main loop's video phase instead of
	// blocking here, so the opening video starts immediately.
	Common::Array<RmdDiscoveryWork> rmdDiscoveryWork;
	bool rmdDiscoveryDone = false;
	Common::RMD *rmdModel = nullptr;
	Common::ScopedPtr<DemoEffectTextures> effectTextures;
	Common::Array<DemoEffect> rmdEffects;
	int32 rmdEffectSlot = -1;
	Common::ScopedPtr<Graphics::AfPackTrail> rmdTrail; // the playing slot's weapon trail
	uint32 rmdEffectTicks = 0, rmdEffectStart = 0;
	auto clearRmdEffects = [&]() {
		for (uint i = 0; i < rmdEffects.size(); ++i)
			delete rmdEffects[i].effect;
		rmdEffects.clear();
		rmdEffectSlot = -1;
		rmdTrail.reset();
		if (effectTextures)
			effectTextures->clearEmbedded();
	};
	Graphics::RMDBounds rmdFixedBounds;
	int rmdIndex = 0;
	// Texture-phase candidate list: real bust portraits + persona icons
	// (BUSTUP/*.BIN, CAMP/PERSONA/PST_*.TMX for PS2 titles; data/commu/bustup/
	// for P3P/GMO titles) -- independent of whichever 3D model is loaded, so
	// discovered once below and decoded lazily on selection via
	// loadTextureIndex(), the same lazy pattern as RMD/GMO models and BGM.
	struct TextureEntry {
		Common::String label;
		Common::String member;
		bool fromCpk = false; // true => gameCpk, false => dataCvm
		bool isBustup = false; // for grouping: bustups first, then persona icons
		// BUSTUP/*.BIN entries are PAC containers that can hold more than
		// one TMX (e.g. a full portrait plus a small mouth/eye overlay
		// sprite for lip-sync) -- this is the name of the largest one,
		// resolved once at discovery time.
		Common::String innerMember;
	};
	Common::Array<TextureEntry> textureEntries;
	int textureIndex = 0;
	Graphics::Surface *textureSurface = nullptr; // decoded on demand, owned

	if (gameId == "P3" || gameId == "P3F" || gameId == "P4") {
		dataCvm = new Common::CVM("DATA.CVM");
		battleCvm = new Common::CVM("BTL.CVM");
		rmdDiscoveryWork.push_back({dataCvm, Common::Array<Common::String>(), 0, false, "DATA.CVM"});
		rmdDiscoveryWork.push_back({battleCvm, Common::Array<Common::String>(), 0, true, "BTL.CVM"});
		Common::Path sfdPath = findOpeningVideo(*dataCvm, gameId);
		if (!sfdPath.toString().empty()) {
			openingStream = dataCvm->createReadStreamForMember(sfdPath);
			openingSourceDesc = Common::String::format("DATA.CVM/%s", sfdPath.toString().c_str());
		} else {
			warning("SMT: no expected opening SFD found in DATA.CVM for %s", gameId.c_str());
		}
	} else if (gameId == "P3P") {
		// P3P stores its opening movies loose (not inside a CPK) under
		// USRDIR/sound/pmsf/*.pmsf -- real files P3OPMV_P3P.pmsf and
		// P3OPMV_P3PB.pmsf. The game directory is already mounted by the
		// Engine base, so Common::PMF(filename) resolves the same way the
		// CPKs above do.
		gameCpk = new Common::CPK("umd0.cpk");
		// P3OPMV_P3PB.pmsf is unrelated content, not an alternate opening --
		// no fallback to it here (see kOpeningVideoCandidates above).
		Common::String p3pOpeningPath = openingVideoPathFor(gameId);
		Common::PMF p3pOpening(p3pOpeningPath);
		if (p3pOpening.isValid()) {
			// P3P's PSP openings are H.264/AVC (PMF video codec flag !=
			// 0x44). With OpenH264 now linked (USE_OPENH264) the MPEG-PS
			// decoder detects the Annex-B H.264 stream and routes it to the
			// H264VideoTrack, so these play even though they are not MPEG.
			openingStream = p3pOpening.createMpegStream();
			openingSourceDesc = p3pOpeningPath;
			// Feed the PSMF audio parameters to the decoder so it owns and
			// clocks the ATRAC3+ track itself (see setATRACAudioInfo).
			Common::PMF::ATRAC3Audio at3 = p3pOpening.extractATRAC3Audio();
			if (at3.valid()) {
				openingDecoder.setATRACAudioInfo(Video::MPEGPSDecoder::kATRACCodecATRAC3Plus,
					at3.channels, at3.sampleRate, at3.blockAlign);
				debug("SMT: P3P opening audio (ATRAC3+), %d Hz %s, decoder-owned", at3.sampleRate, at3.channels == 2 ? "stereo" : "mono");
			}
		} else {
			debug("SMT: no sound/pmsf opening video found for P3P (loose USRDIR files not present)");
		}
	} else {
		// P4G / P5 / P5R
		gameCpk = new Common::CPK("data.cpk"); // still needed below for GMO model discovery
		// data.cpk assets are CRI-encrypted (confirmed on a real P5R USM) --
		// no opening video is reachable until that decryption is solved.
		// Common::Path pmfPath = findOpeningVideo(*gameCpk, ".PMF");
		// if (pmfPath.toString().empty())
		// 	pmfPath = findOpeningVideo(*gameCpk, ".USM");
		// if (!pmfPath.toString().empty()) {
		// 	openingStream = gameCpk->createReadStreamForMember(pmfPath);
		// 	openingSourceDesc = Common::String::format("data.cpk/%s", pmfPath.toString().c_str());
		// }
		debug("SMT: P4G/P5/P5R opening video not attempted -- data.cpk decryption unsolved");
	}

	// Parse one entry into the active RMD model. The texture-phase list is
	// independent of this (real bust/persona-icon assets, not model
	// textures -- see loadTextureIndex() below), so this no longer touches
	// textureEntries at all.
	auto loadRmdIndex = [&](int idx) {
	#ifdef USE_OPENGL_SHADERS
		Graphics::RMDRenderer::invalidate();
	#endif
		clearRmdEffects();
		delete rmdModel;
		rmdModel = nullptr;
		rmdFixedBounds = Graphics::RMDBounds();
		if (idx < 0 || idx >= (int)rmdEntries.size())
			return;
		rmdModel = loadRmdEntry(rmdEntries[idx]);
		if (!rmdModel || !rmdModel->ok()) {
			delete rmdModel;
			rmdModel = nullptr;
			return;
		}
		rmdIndex = idx;
		if (!effectTextures && !rmdModel->effectBlocks().empty())
			effectTextures.reset(new DemoEffectTextures(dataCvm));
		for (uint b = 0; b < rmdModel->effectBlocks().size(); ++b) {
			const Common::RMD::EffectBlock &block = rmdModel->effectBlocks()[b];
			Common::EPL epl(block.payload.data(), block.payload.size());
			if (!epl.ok())
				continue;
			// One independent instance per attachment index first..last.
			for (uint32 helper = block.first; helper <= block.last; ++helper) {
				DemoEffect e;
				e.effect = new Graphics::EPLEffect(epl, effectTextures.get());
				e.helper = helper;
				e.slot = block.slot;
				e.flags = block.flags;
				rmdEffects.push_back(e);
			}
		}
		rmdEffectTicks = 0;
		rmdEffectStart = g_system->getMillis();
		debug("SMT: RMD model %d/%u %s (%u frames, %u geometries, %u materials, %u textures)%s",
		      rmdIndex + 1, (unsigned)rmdEntries.size(), rmdEntries[rmdIndex].label.c_str(),
		      (unsigned)rmdModel->frames().size(), (unsigned)rmdModel->geometries().size(),
		      (unsigned)rmdModel->materials().size(), (unsigned)rmdModel->textures().size(),
		      rmdEntries[rmdIndex].persona ? " [persona]" : "");
	};

	// These archive paths were confirmed in the external P3 FES/P4 corpus;
	// only used as a fallback if the random persona pick below fails to load.
	Common::String preferredRmd = gameId == "P3" ? "field/rmd/m001_001.rmd" :
	                              gameId == "P3F" ? "model/facilityp/fp000.rmd" :
	                              gameId == "P4" ? "model/npc/n101_1.rmd" : "";
	// Runs once, the moment stepRmdDiscovery() finishes (see the main loop's
	// video phase): the model phase is meant to showcase personas and NPCs,
	// not every RMD in the archive -- real corpora also carry field scenery,
	// weapons, facility props, and UI symbols under this same .RMD
	// extension, most of which this project's RMD support was never
	// verified against (a real cycling session hit a 60% load-failure rate
	// before this filter existed, since those unverified categories vastly
	// outnumber personas+NPCs).
	auto finishRmdDiscoveryAndPickModel = [&]() {
		{
			Common::Array<RmdEntry> filtered;
			for (uint i = 0; i < rmdEntries.size(); ++i)
				if (rmdEntries[i].persona || rmdEntries[i].npc)
					filtered.push_back(rmdEntries[i]);
			rmdEntries = filtered;
		}
		Common::sort(rmdEntries.begin(), rmdEntries.end(), [](const RmdEntry &a, const RmdEntry &b) {
			if (a.persona != b.persona)
				return a.persona; // personas first, mirroring GMO's discovery order
			if (a.source != b.source)
				return a.source < b.source;
			if (a.modelPath != b.modelPath)
				return a.modelPath < b.modelPath;
			if (a.archivePath != b.archivePath)
				return a.archivePath < b.archivePath;
			return a.member < b.member;
		});
		// Random pick scoped to persona-flagged entries (a contiguous prefix
		// after the persona-first sort above); titles with none fall back to
		// a random pick over the whole list.
		int rmdPersonaCount = 0;
		while (rmdPersonaCount < (int)rmdEntries.size() && rmdEntries[rmdPersonaCount].persona)
			++rmdPersonaCount;
		int pickPool = rmdPersonaCount > 0 ? rmdPersonaCount : (int)rmdEntries.size();
		loadRmdIndex(DebugDemoView::pickRandomIndex(*_rnd, pickPool));

		if (!rmdModel) {
			// Random pick failed to parse -- fall back to the preferred
			// path, then the first entry that loads at all.
			int preferred = -1;
			for (uint i = 0; i < rmdEntries.size(); ++i) {
				if (!preferredRmd.empty() && rmdMatches(rmdEntries[i], preferredRmd)) {
					preferred = i;
					break;
				}
			}
			if (preferred >= 0)
				loadRmdIndex(preferred);
			for (uint candidate = 0; !rmdModel && candidate < rmdEntries.size(); ++candidate) {
				if ((int)candidate == preferred)
					continue;
				loadRmdIndex(candidate);
			}
		}
	};
	if (!dataCvm)
		rmdDiscoveryDone = true; // nothing to discover for this title; skip the deferred step entirely

	// Texture-phase candidates for PS2 titles: real bust portraits
	// (BUSTUP/*.BIN -- confirmed real TMX0 containers despite the .BIN
	// extension) and persona icons (CAMP/PERSONA/PST_*.TMX), both verified
	// against the real P3 FES corpus and decodable by the existing
	// SMT::TMX parser. Opening a nested PAC and decoding a TMX header per
	// BUSTUP entry is real, non-trivial work across hundreds/thousands of
	// entries (measured ~1-2s on a real P4 corpus) -- scanned incrementally
	// via stepTextureScan() from the main loop's video phase, same reasoning
	// as the RMD discovery worklist above.
	struct TextureScanState {
		Common::Array<Common::String> names;
		uint index = 0;
		bool started = false;
	};
	TextureScanState textureScanState;
	bool textureScanDone = !dataCvm;
	auto stepTextureScan = [&](uint32 budgetMs) -> bool {
		if (!dataCvm)
			return true;
		if (!textureScanState.started) {
			Common::ArchiveMemberList members;
			dataCvm->listMembers(members);
			for (Common::ArchiveMemberList::iterator it = members.begin(); it != members.end(); ++it)
				textureScanState.names.push_back((*it)->getName());
			textureScanState.started = true;
		}
		uint32 start = g_system->getMillis();
		while (textureScanState.index < textureScanState.names.size()) {
			const Common::String &nm = textureScanState.names[textureScanState.index++];
			Common::String up = nm;
			up.toUppercase();
			if (up.hasPrefix("BUSTUP/")) {
				// Each BUSTUP/*.BIN is a PAC that can hold more than one
				// TMX (e.g. a full portrait plus a small mouth/eye overlay
				// sprite for lip-sync) -- resolve the largest one now, and
				// skip files that turn out to be empty/placeholder stubs
				// (a few real ones, like BUST_DUMMY.BIN, are 0 bytes or
				// too small to contain any valid TMX at all).
				Common::SeekableReadStream *stream = dataCvm->createReadStreamForMember(Common::Path(nm));
				if (!stream)
					continue;
				SMT::Format::Archive::PAC pac(stream); // takes ownership of stream
				Common::ArchiveMemberList inner;
				pac.listMembers(inner);
				Common::String bestName;
				int64 bestArea = 0;
				for (Common::ArchiveMemberList::iterator innerIt = inner.begin(); innerIt != inner.end(); ++innerIt) {
					Common::String innerName = (*innerIt)->getName();
					Common::SeekableReadStream *innerStream = pac.createReadStreamForMember(Common::Path(innerName));
					if (!innerStream)
						continue;
					SMT::TMX tmx(innerStream);
					delete innerStream;
					int64 area = (int64)tmx.width() * tmx.height();
					if (area > bestArea) {
						bestArea = area;
						bestName = innerName;
					}
				}
				if (bestArea > 0) {
					TextureEntry te;
					te.member = nm;
					te.innerMember = bestName;
					te.label = Common::String::format("DATA.CVM/%s", nm.c_str());
					te.isBustup = true;
					textureEntries.push_back(te);
				}
			} else if (up.hasPrefix("CAMP/PERSONA/")) {
				TextureEntry te;
				te.member = nm;
				te.label = Common::String::format("DATA.CVM/%s", nm.c_str());
				te.isBustup = false;
				textureEntries.push_back(te);
			}
			if (g_system->getMillis() - start >= budgetMs)
				break;
		}
		if (textureScanState.index < textureScanState.names.size())
			return false;
		// Group contiguously (bustups, then persona icons) instead of
		// whatever order the archive happened to list them in.
		Common::sort(textureEntries.begin(), textureEntries.end(), [](const TextureEntry &a, const TextureEntry &b) {
			if (a.isBustup != b.isBustup)
				return a.isBustup;
			return a.member < b.member;
		});
		return true;
	};

	// P3P (and any other non-PS2 title without a BGM.CVM) keeps its ADX
	// tracks loose in the game CPK.
	if (bgmEntries.empty() && gameCpk) {
		Common::ArchiveMemberList umdAdx;
		gameCpk->listMembers(umdAdx);
		for (Common::ArchiveMemberList::iterator it = umdAdx.begin(); it != umdAdx.end(); ++it) {
			Common::String nm = (*it)->getName();
			Common::String up = nm;
			up.toUppercase();
			if (!up.hasSuffix(".ADX"))
				continue;
			BgmEntry entry;
			entry.member = nm;
			entry.label = Common::String::format("umd0.cpk/%s", nm.c_str());
			entry.fromCpk = true;
			bgmEntries.push_back(entry);
		}
	}

	// Decode one BGM entry into adxAudio, ready for the mixer to play when
	// the texture+BGM phase starts (or restart it when Up/Down cycles).
	auto loadBgmIndex = [&](int idx) {
		_mixer->stopHandle(adxHandle);
		if (!adxOwnedByMixer)
			delete adxAudio;
		adxAudio = nullptr;
		adxOwnedByMixer = false;
		bgmDurationSecs = 0;
		if (idx < 0 || idx >= (int)bgmEntries.size())
			return;
		const BgmEntry &en = bgmEntries[idx];
		Common::SeekableReadStream *stream = (en.fromCpk && gameCpk)
			? gameCpk->createReadStreamForMember(Common::Path(en.member))
			: bgmCvm.createReadStreamForMember(Common::Path(en.member));
		if (!stream)
			return;
		adxAudio = Common::makeADXStream(stream, DisposeAfterUse::YES);
		if (!adxAudio) {
			warning("SMT: failed to decode %s", en.label.c_str());
			return;
		}
		// Query duration once, now, before this stream is ever handed to the
		// mixer -- once playing, the mixer thread owns its read cursor.
		bgmDurationSecs = adxAudio->getLength().secs();
		bgmIndex = idx;
		debug("SMT: BGM %d/%u selected: %s", idx + 1, (unsigned)bgmEntries.size(), en.label.c_str());
	};
	if (!bgmEntries.empty())
		loadBgmIndex(DebugDemoView::pickRandomIndex(*_rnd, (int)bgmEntries.size()));

	if (openingStream) {
		if (openingSourceDesc.hasSuffixIgnoreCase(".USM")) {
			// TODO: play USM through Video::USMDecoder (VP9 via libvpx) instead of rejecting it
			// by extension. No USM opening is reachable yet: P4G/P5/P5R keep them in a
			// CRI-encrypted data.cpk, so this path has never seen real data. The MPEG-PS
			// decoder below cannot read USM's chunked layout.
			delete openingStream;
			openingStream = nullptr;
			warning("SMT: found a USM opening (%s) but USM playback is not wired up yet", openingSourceDesc.c_str());
		} else if (openingSourceDesc.hasSuffixIgnoreCase(".PMF")) {
			// PMF isn't itself an MPEG-PS stream on disk -- its real 2048-
			// byte PSMF header wraps one. We already unwrapped it via
			// PMF::createMpegStream() (see the P3P branch above), so by the
			// time we get here openingStream already points at the inner
			// MPEG-PS bytes. Fall through to MPEGPSDecoder::loadStream().
			openingLoaded = openingDecoder.loadStream(openingStream);
			if (openingLoaded) {
				debug("SMT: playing opening video %s, %dx%d", openingSourceDesc.c_str(), openingDecoder.getWidth(), openingDecoder.getHeight());
				if (!openingDecoder.setOutputPixelFormat(Graphics::PixelFormat::createFormatRGBA32()))
					warning("SMT: opening video decoder rejected the RGBA32 pixel format; frames will be converted per upload");
				// Same audio-clock sync treatment as the plain SFD path
				// below -- see the comment there.
				openingDecoder.setUseAudioSync(true);
			} else {
				warning("SMT: failed to load opening video %s", openingSourceDesc.c_str());
			}
		} else {
			openingLoaded = openingDecoder.loadStream(openingStream);
			if (openingLoaded) {
				debug("SMT: playing opening video %s, %dx%d", openingSourceDesc.c_str(), openingDecoder.getWidth(), openingDecoder.getHeight());
				if (!openingDecoder.setOutputPixelFormat(Graphics::PixelFormat::createFormatRGBA32()))
					warning("SMT: opening video decoder rejected the RGBA32 pixel format; frames will be converted per upload");
				// Let the decoder-owned audio track drive the clock for all
				// our openings: it keeps A/V in sync and makes the demuxer
				// keep producing audio packets ahead of the mixer (avoids
				// underruns/choppiness). Streams with no audio track fall
				// back to the system clock automatically.
				openingDecoder.setUseAudioSync(true);
			} else {
				warning("SMT: failed to load opening video %s", openingSourceDesc.c_str());
			}
		}
	}

	// Collect every real GMO model we can find so the user can cycle them
	// with Up/Down. Each entry references a CPK member plus a byte range; the
	// model is parsed on demand in loadGmoEntry() below.
	struct GmoEntry {
		Common::String label;
		Common::String member;
		uint32 offset;
		uint32 size;
		bool persona;
	};
	Common::Array<GmoEntry> gmoEntries;
	Common::GMO *gmoModel = nullptr;
	bool gmoIsPersona = false;
	Graphics::GMOBounds gmoFixedBounds;
	int gmoIndex = 0;

	if (gameCpk) {
		Common::ArchiveMemberList modelList;
		gameCpk->listMembers(modelList);
		Common::Array<Common::String> personaPacs;
		Common::Array<Common::String> looseGmos;
		Common::Array<Common::String> fpcNames;
		for (Common::ArchiveMemberList::iterator it = modelList.begin(); it != modelList.end(); ++it) {
			Common::String nm = (*it)->getName();
			Common::String up = nm;
			up.toUppercase();
			if (up.hasPrefix("DATA/MODEL/PACK/PS") && up.hasSuffix(".PAC"))
				personaPacs.push_back(nm);
			else if (up.hasSuffix(".GMO"))
				looseGmos.push_back(nm);
			else if (up.hasSuffix(".FPC"))
				fpcNames.push_back(nm);
			// Real bust portrait directory confirmed for P3P (the one
			// hardcoded file this used to load was
			// data/commu/bustup/i_bust41_b.tmx -- generalized here to every
			// file under that directory instead of just the one).
			if (up.hasPrefix("DATA/COMMU/BUSTUP/")) {
				TextureEntry te;
				te.member = nm;
				te.label = Common::String::format("%s/%s", gameId == "P3P" ? "umd0.cpk" : "data.cpk", nm.c_str());
				te.fromCpk = true;
				textureEntries.push_back(te);
			}
		}

		// Personas first (highly-boned summon models), then field
		// Persona-shadow models, then loose NPC/weapon GMOs. Capped so a
		// huge archive cannot blow up startup/memory.
		const uint kMaxModels = 64;
		Common::sort(personaPacs.begin(), personaPacs.end());
		for (uint i = 0; i < personaPacs.size() && gmoEntries.size() < kMaxModels; i++) {
			Common::SeekableReadStream *s = gameCpk->createReadStreamForMember(Common::Path(personaPacs[i]));
			if (!s)
				continue;
			uint32 sz = (uint32)s->size();
			if (sz <= 0x100) {
				delete s;
				continue;
			}
			byte hdr[0x100];
			if (s->read(hdr, 0x100) != 0x100) {
				delete s;
				continue;
			}
			delete s;
			uint32 gmoSize = hdr[0xFC] | (hdr[0xFD] << 8) | (hdr[0xFE] << 16) | ((uint32)hdr[0xFF] << 24);
			if (gmoSize == 0 || gmoSize > sz - 0x100)
				continue;
			GmoEntry en;
			en.label = personaPacs[i];
			en.member = personaPacs[i];
			en.offset = 0x100;
			en.size = gmoSize;
			en.persona = true;
			gmoEntries.push_back(en);
		}

		for (uint i = 0; i < fpcNames.size() && gmoEntries.size() < kMaxModels; i++) {
			Common::SeekableReadStream *fpcStream = gameCpk->createReadStreamForMember(Common::Path(fpcNames[i]));
			if (!fpcStream)
				continue;
			uint32 fpcSize = (uint32)fpcStream->size();
			if (fpcSize <= 0x100) {
				delete fpcStream;
				continue;
			}
			Common::Array<byte> fpc(fpcSize);
			fpcStream->read(fpc.data(), fpcSize);
			delete fpcStream;
			// `u32 size LE` immediately precedes each `GMO` blob; entries are
			// 0x20-aligned (confirmed on real f024_.fpc).
			uint32 pos = 0x100;
			while (pos + 8 < fpcSize) {
				uint32 blobSize = fpc[pos - 4] | (fpc[pos - 3] << 8) |
				                  (fpc[pos - 2] << 16) | ((uint32)fpc[pos - 1] << 24);
				if (blobSize == 0 || blobSize > fpcSize - pos)
					break;
				if (fpc[pos] == 'O' && fpc[pos + 1] == 'M' && fpc[pos + 2] == 'G' && fpc[pos + 3] == '.') {
					GmoEntry en;
					en.label = Common::String::format("%s#%u", fpcNames[i].c_str(), (unsigned)((pos - 0x100) / 0x20));
					en.member = fpcNames[i];
					en.offset = pos;
					en.size = blobSize;
					en.persona = false;
					gmoEntries.push_back(en);
					if (gmoEntries.size() >= kMaxModels)
						break;
				}
				pos = (pos + blobSize + 0x1F) & ~0x1Fu;
			}
		}

		for (uint i = 0; i < looseGmos.size() && gmoEntries.size() < kMaxModels; i++) {
			Common::SeekableReadStream *probe = gameCpk->createReadStreamForMember(Common::Path(looseGmos[i]));
			uint32 size = probe ? (uint32)probe->size() : 0;
			delete probe;
			if (size < 16)
				continue;
			GmoEntry en;
			en.label = looseGmos[i];
			en.member = looseGmos[i];
			en.offset = 0;
			en.size = size;
			en.persona = false;
			gmoEntries.push_back(en);
		}
	}

	// Parse one entry into the active model and recompute its fixed-camera
	// bounds. The texture-phase list is independent of this (real
	// bust/persona-icon assets, discovered above into textureEntries), so
	// this no longer touches it. The GL renderer caches textures by GMO
	// pointer, so replacing the model automatically reloads its textures.
	auto loadGmoEntry = [&](int idx) {
	#ifdef USE_OPENGL_SHADERS
		Graphics::GMORenderer::invalidate();
	#endif
		delete gmoModel;
		gmoModel = nullptr;
		gmoIsPersona = false;
		gmoFixedBounds = Graphics::GMOBounds();
		if (!gameCpk || idx < 0 || idx >= (int)gmoEntries.size())
			return;
		const GmoEntry &en = gmoEntries[idx];
		Common::SeekableReadStream *s = gameCpk->createReadStreamForMember(Common::Path(en.member));
		if (!s)
			return;
		s->seek(en.offset);
		Common::Array<byte> buf(en.size);
		bool okRead = s->read(buf.data(), en.size) == en.size;
		delete s;
		if (!okRead)
			return;
		Common::MemoryReadStream ms(buf.data(), en.size, DisposeAfterUse::NO);
		gmoModel = new Common::GMO(&ms);
		if (!gmoModel->ok()) {
			delete gmoModel;
			gmoModel = nullptr;
			return;
		}
		gmoIndex = idx;
		gmoIsPersona = en.persona;
		debug("SMT: model %d/%u %s (%u bones, %u parts, %u materials, %u textures)%s",
		      idx + 1, (unsigned)gmoEntries.size(), en.label.c_str(),
		      (unsigned)gmoModel->bones().size(), (unsigned)gmoModel->parts().size(),
		      (unsigned)gmoModel->materials().size(), (unsigned)gmoModel->textures().size(),
		      en.persona ? " [persona]" : "");
#ifdef USE_OPENGL_SHADERS
		Graphics::GMORenderer::computeBounds(*gmoModel, 320, 240, 0.0f, gmoIsPersona, nullptr, nullptr, &gmoFixedBounds);
		debug("SMT: fixed camera bounds X[%.1f,%.1f] Y[%.1f,%.1f] Z[%.1f,%.1f]",
		      gmoFixedBounds.minX, gmoFixedBounds.maxX, gmoFixedBounds.minY, gmoFixedBounds.maxY,
		      gmoFixedBounds.minZ, gmoFixedBounds.maxZ);
#endif
	};

	if (!gmoEntries.empty()) {
		// Random pick scoped to persona-flagged entries -- discovery above
		// already puts them first (a contiguous prefix), so this mirrors
		// the RMD-side persona-first pick.
		int gmoPersonaCount = 0;
		while (gmoPersonaCount < (int)gmoEntries.size() && gmoEntries[gmoPersonaCount].persona)
			++gmoPersonaCount;
		int pickPool = gmoPersonaCount > 0 ? gmoPersonaCount : (int)gmoEntries.size();
		loadGmoEntry(DebugDemoView::pickRandomIndex(*_rnd, pickPool));
	}
	debug("SMT: %u RMD candidate model(s) found", (unsigned)rmdEntries.size());
	debug("SMT: %u candidate model(s) found", (unsigned)gmoEntries.size());

	// Decode one texture entry (bust portrait or persona icon) into
	// textureSurface, ready for the texture+BGM phase. Lazy, same pattern
	// as loadBgmIndex -- decoding all ~800 real candidates eagerly would be
	// wasteful when only one is shown at a time.
	auto loadTextureIndex = [&](int idx) {
		if (textureSurface) {
			textureSurface->free();
			delete textureSurface;
			textureSurface = nullptr;
		}
		if (idx < 0 || idx >= (int)textureEntries.size())
			return;
		const TextureEntry &en = textureEntries[idx];
		Common::SeekableReadStream *stream = (en.fromCpk && gameCpk)
			? gameCpk->createReadStreamForMember(Common::Path(en.member))
			: (dataCvm ? dataCvm->createReadStreamForMember(Common::Path(en.member)) : nullptr);
		if (!stream)
			return;
		// BUSTUP/*.BIN entries are real PAC containers wrapping one or more
		// TMX images (confirmed against real bytes: a 252-byte-name+4-byte-
		// size Version1 PAC header, payload starting at offset 0x100), not
		// raw TMX -- unwrap that layer first, selecting the largest inner
		// TMX already resolved at discovery time (en.innerMember). CAMP/
		// PERSONA/*.TMX and the gameCpk-sourced data/commu/bustup/*.tmx
		// entries are already raw TMX and don't need this.
		SMT::TMX *tmx = nullptr;
		if (en.isBustup) {
			SMT::Format::Archive::PAC pac(stream); // takes ownership of stream
			Common::SeekableReadStream *inner = pac.createReadStreamForMember(Common::Path(en.innerMember));
			if (!inner)
				return;
			tmx = new SMT::TMX(inner);
			delete inner;
		} else {
			tmx = new SMT::TMX(stream);
			delete stream;
		}
		textureSurface = tmx->getSurface();
		delete tmx;
		textureIndex = idx;
		if (textureSurface)
			debug("SMT: texture %d/%u selected: %s (%dx%d)", idx + 1, (unsigned)textureEntries.size(),
			      en.label.c_str(), textureSurface->w, textureSurface->h);
		else
			warning("SMT: failed to decode texture %s", en.label.c_str());
	};
	if (!textureEntries.empty())
		loadTextureIndex(DebugDemoView::pickRandomIndex(*_rnd, (int)textureEntries.size()));

	// GMO rendering is OpenGL-only; there is no software fallback.
	auto modelRendererAvailable = [&]() {
#ifdef USE_OPENGL_SHADERS
		if (rmdModel)
			return Graphics::RMDRenderer::isAvailable();
		return gmoModel && Graphics::GMORenderer::isAvailable();
#else
		return false;
#endif
	};
	bool useGLModel = modelRendererAvailable();
	if ((gmoModel || rmdModel) && !useGLModel)
		warning("SMT: no OpenGL shader context available; the model phase will be skipped");


	//CVMArchive _data("DATA.CVM");
	//CVMArchive _btl("BTL.CVM");

	// You could use backend transactions directly as an alternative,
	// but it isn't recommended, until you want to handle the error values
	// from OSystem::endGFXTransaction yourself.
	// This is just an example template:
	//_system->beginGFXTransaction();
	//	// This setup the graphics mode according to users seetings
	//	initCommonGFX(false);
	//
	//	// Specify dimensions of game graphics window.
	//	// In this example: 320x200
	//	_system->initSize(320, 200);
	//OSystem::kTransactionSizeChangeFailed here
	//_system->endGFXTransaction();

	// Create debugger console. It requires GFX to be initialized
	_console = new Console(this);

	// Additional setup.
	debug("SMTEngine::init");

	Common::Event e;

	g_system->getEventManager()->pollEvent(e);
	g_system->delayMillis(10);

	const Graphics::Font *statusFont = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	const Common::String statusMessage = "Basic file format support only -- not a playable game.";

	// Three-phase demo, each advanced by pressing Space:
	//   1. opening video (PS2: SFD from DATA.CVM; P3P: loose sound/pmsf)
	//   2. a decoded texture + the BGM song playing through the mixer
	//   3. the largest .GMO rotating in 3D
	// Every phase renders full-sale (video / model fill the screen; the
	// texture phase blits the decoded texture centered at native size).
	// Space skips the current phase; the demo ends after the model phase.
	// Phase sequencing/ordering and Space/Up/Down classification live in
	// DebugDemoView (debug_demo_view.h) -- the generic, phase-independent
	// part of this loop; everything else here (loading, rendering, the
	// entry lists) is SMT/RMD/GMO-specific and stays local.
	DebugDemoView demoView;
	bool phaseBgmStarted = false;

	// Model spin: continuous while the model phase runs.
	int32 modelSpinStart = 0;
	// A fixed spin-duration constant, not tied to whether a model happens
	// to be loaded at declaration time (discovery/pick runs later) -- an
	// earlier version gated this on gmoModel/rmdModel, which are always
	// still null here, permanently locking it to 0 and turning the
	// elapsed/modelSpinMs division in prepareModelFrame() into elapsed/0
	// (Inf), which makes cosf/sinf(rotY) NaN and blanks every model's
	// rendered X/Z (confirmed via real vertex-level NaN tracing).
	int32 modelSpinMs = 20000;
	// Animation walker: the model phase plays every real motion in turn.
	int animMotion = 0;
	int32 animMotionStart = 0;
	bool animHadMotion = false;
	int videoFrameCount = 0;
	bool videoStartLogged = false;
	bool userQuit = false;
	bool glModelActive = false;
	// Last decoded video frame, redrawn every iteration through GL.
	const Graphics::Surface *videoFrame = nullptr;

	// Shared model-frame preparation (spin angle + animation pose). Both the
	// CPU and the OpenGL model paths call this so the demo timing is identical.
	Common::GMO::BonePose frameBonePose;
	Common::GMO::MaterialPose frameMatPose;
	const Common::GMO::BonePose *frameBpp = nullptr;
	const Common::GMO::MaterialPose *frameMpp = nullptr;
	Common::RMD::BonePose rmdBonePose;
	const Common::RMD::BonePose *frameRmdPose = nullptr;
	Common::RMD::MaterialPose rmdMaterialPose;
	const Common::RMD::MaterialPose *frameRmdMaterials = nullptr;
	Common::String animLabel;
	// No special starting offset: model selection is random now, and the
	// "start on the front" offset this used to have (M_PI_2) was calibrated
	// for one specific model (N101), which made it arbitrarily wrong for
	// every other model. The model spins continuously regardless, so the
	// front becomes visible during the rotation either way.
	float modelAngle = 0.0f;
	auto prepareModelFrame = [&]() {
		frameBpp = nullptr;
		frameMpp = nullptr;
		frameRmdPose = nullptr;
		frameRmdMaterials = nullptr;
		animLabel.clear();
		int32 elapsed = g_system->getMillis() - modelSpinStart;
		modelAngle = (float)elapsed / (float)modelSpinMs * (float)(M_PI * 2);
		if (!rmdModel && !gmoModel)
			return;

		// Walk through every real MOTION in turn (bone + material tracks),
		// then loop back to the first. Models without motions just keep
		// spinning (weapon/field GMOs).
		if (rmdModel) {
			const Common::Array<Common::RMD::Animation> &animations = rmdModel->animations();
			if (!animations.empty()) {
				if (animMotion >= (int)animations.size())
					animMotion = 0;
				const Common::RMD::Animation &animation = animations[animMotion];
				float duration = animation.duration > 0.0f ? animation.duration : 1.0f;
				float time = (g_system->getMillis() - animMotionStart) / 1000.0f;
				bool looped = false;
				if (time >= duration) {
					animMotion = (animMotion + 1) % (int)animations.size();
					animMotionStart = g_system->getMillis();
					time = 0.0f;
					looped = true;
				}
				if (rmdModel->evaluateAnimation(animMotion, time, rmdBonePose))
					frameRmdPose = &rmdBonePose;
				// UV and material animations live in the same slot as the skeletal clip.
				int32 slot = -1;
				for (uint s = 0; s < rmdModel->animationSlots().size(); ++s)
					if (rmdModel->animationSlots()[s].animation == animMotion) {
						rmdModel->evaluateSlotMaterials(s, time, rmdMaterialPose);
						frameRmdMaterials = &rmdMaterialPose;
						slot = (int32)s;
						break;
					}
				// A slot switch restarts that slot's effects from frame 0.
				// Same slot again: only effects flagged "restart on loop" restart.
				for (uint i = 0; i < rmdEffects.size(); ++i)
					if (rmdEffects[i].slot >= 0 && rmdEffects[i].slot == slot &&
							(slot != rmdEffectSlot || (looped && (rmdEffects[i].flags & 1))))
						rmdEffects[i].effect->restart();
				// Every slot start plays its weapon trail from the beginning; looping doesn't.
				if (slot != rmdEffectSlot) {
					rmdTrail.reset();
					for (uint i = 0; i < rmdModel->trailPacks().size(); ++i)
						if (rmdModel->trailPacks()[i].slot == slot) {
							const Common::Array<byte> &payload = rmdModel->trailPacks()[i].payload;
							rmdTrail.reset(new Graphics::AfPackTrail(payload.data(), payload.size()));
							break;
						}
				}
				rmdEffectSlot = slot;
				animLabel = Common::String::format("Animation %d/%u (time %.1f)",
				                                   animMotion + 1, (unsigned)animations.size(), time);
			} else {
				// No skeletal clip: still play the first slot's UV/material animations.
				for (uint s = 0; s < rmdModel->animationSlots().size(); ++s)
					if (!rmdModel->animationSlots()[s].uvAnimations.empty() || !rmdModel->animationSlots()[s].materialAnimations.empty()) {
						rmdModel->evaluateSlotMaterials(s, elapsed / 1000.0f, rmdMaterialPose);
						frameRmdMaterials = &rmdMaterialPose;
						break;
					}
			}
			// Effects run at the game's 30 ticks per second, in the model's own space.
			const uint32 due = (g_system->getMillis() - rmdEffectStart) * 30 / 1000;
			const Graphics::EffectCamera camera = Graphics::rmdEffectCamera(modelAngle + (float)M_PI);
			DemoHelperContext helpers = {rmdModel, frameRmdPose};
			for (uint32 n = 0; rmdEffectTicks < due && n < 8; ++n, ++rmdEffectTicks) {
				advanceDemoEffects(*rmdModel, rmdEffects, rmdEffectSlot, frameRmdPose, camera);
				if (rmdTrail && rmdTrail->ok() && !rmdTrail->finished())
					rmdTrail->update(demoHelperPosition, &helpers);
			}
			rmdEffectTicks = MAX(rmdEffectTicks, due > 8 ? due - 8 : 0); // don't fall behind after a stall
			return;
		}
		const Common::Array<Common::GMO::Motion> &motions = gmoModel->motions();
		if (!motions.empty()) {
			animHadMotion = true;
			if (animMotion >= (int)motions.size())
				animMotion = 0;
			const Common::GMO::Motion &mo = motions[animMotion];
			float rate = mo.frameRate > 0.0f ? mo.frameRate : 30.0f;
			float dur = mo.frameLoopEnd > mo.frameLoopStart ? mo.frameLoopEnd : 60.0f;
			float frame = (g_system->getMillis() - animMotionStart) / 1000.0f * rate;
			if (frame >= dur) {
				animMotion = (animMotion + 1) % (int)motions.size();
				animMotionStart = g_system->getMillis();
				frame = 0.0f;
			}
			if (gmoModel->evaluateMotion(animMotion, frame, frameBonePose, frameMatPose)) {
				frameBpp = &frameBonePose;
				frameMpp = &frameMatPose;
			}
			animLabel = Common::String::format("Animation %d/%u: %s (frame %.0f)",
			                                   animMotion + 1, (unsigned)motions.size(), mo.name.c_str(), frame);
		}
	};

	while (!shouldQuit()) {
		// Poll input once here so every phase sees the same skip key. Only
		// the net direction is accumulated in this loop -- acting on every
		// queued event individually let a burst of same-frame keydowns
		// (real OS key-repeat, or just events piling up under a slow
		// render loop) fire loadBgmIndex()'s stop/delete/create/play
		// sequence back-to-back with no gap, which crashed (segfault at
		// the `delete adxAudio` inside a second call landing before the
		// first playStream's channel had settled). Applying the summed
		// direction once after the loop, like skipPressed already does,
		// makes at most one BGM/texture/model switch happen per frame.
		bool skipPressed = false;
		int modelCycleDir = 0, textureBgmCycleDir = 0;
		while (g_system->getEventManager()->pollEvent(e)) {
			int dir = 0;
			switch (demoView.classify(e, dir)) {
			case DebugDemoView::kActionQuit:
				// Fall through to the shared cleanup below instead of
				// returning straight out of the loop.
				userQuit = true;
				break;
			case DebugDemoView::kActionSkip:
				skipPressed = true;
				break;
			case DebugDemoView::kActionCycleModel:
				modelCycleDir += dir;
				break;
			case DebugDemoView::kActionCycleTextureBgm:
				textureBgmCycleDir += dir;
				break;
			case DebugDemoView::kActionNone:
			default:
				break;
			}
		}
		if (modelCycleDir && ((rmdModel && rmdEntries.size() > 1) || (!rmdModel && gmoEntries.size() > 1))) {
			// A handful of real entries even within the persona/NPC-only
			// list can still fail to parse (unverified corpus edge cases).
			// Keep stepping in the requested direction, bounded to one
			// full lap, so a single bad entry never leaves the screen on
			// "No model found" -- the user would otherwise need to notice
			// and press the same key again themselves to skip past it.
			int stepDir = modelCycleDir > 0 ? 1 : -1;
			if (rmdModel) {
				const int total = (int)rmdEntries.size();
				for (int attempt = 0; attempt < total; ++attempt) {
					rmdIndex = DebugDemoView::cycleIndex(rmdIndex, attempt == 0 ? modelCycleDir : stepDir, total);
					loadRmdIndex(rmdIndex);
					// A handful of real NPC entries parse fine but carry no
					// geometry at all (e.g. MODEL/NPC/KARA.RMD, confirmed
					// against AmicitiaLibrary's independent parse -- it's a
					// genuine 2-frame skeleton stub with nothing to draw, not
					// a parsing bug) -- skip those too so cycling never lands
					// on a blank screen.
					if (rmdModel && !rmdModel->geometries().empty())
						break;
				}
			} else {
				const int total = (int)gmoEntries.size();
				for (int attempt = 0; attempt < total; ++attempt) {
					gmoIndex = DebugDemoView::cycleIndex(gmoIndex, attempt == 0 ? modelCycleDir : stepDir, total);
					loadGmoEntry(gmoIndex);
					if (gmoModel)
						break;
				}
			}
			useGLModel = modelRendererAvailable();
			modelSpinStart = g_system->getMillis();
			animMotion = 0;
			animMotionStart = modelSpinStart;
			animHadMotion = false;
		}
		if (textureBgmCycleDir && (textureEntries.size() > 1 || bgmEntries.size() > 1)) {
			// Each list wraps on its own size, independent of the other.
			int newTextureIndex = DebugDemoView::cycleIndex(textureIndex, textureBgmCycleDir, (int)textureEntries.size());
			if (newTextureIndex != textureIndex)
				loadTextureIndex(newTextureIndex);
			int newBgmIndex = DebugDemoView::cycleIndex(bgmIndex, textureBgmCycleDir, (int)bgmEntries.size());
			if (newBgmIndex != bgmIndex) {
				loadBgmIndex(newBgmIndex);
				if (adxAudio) {
					_mixer->playStream(Audio::Mixer::kMusicSoundType, &adxHandle, adxAudio, DisposeAfterUse::YES);
					adxOwnedByMixer = true;
				}
			}
		}

		if (userQuit)
			break;

		// Model phase: the backend is already in 3D mode (initGraphics3d at
		// startup), so draw the model and its HUD text directly through GL.
		// This is the last phase, so we never switch back.
#ifdef USE_OPENGL_SHADERS
		// useGLModel is only a startup snapshot (no model is loaded yet when
		// it's first computed) refreshed on model-cycle key presses -- without
		// re-checking here, a run that reaches this phase without ever
		// cycling models stays stuck on the false snapshot and wrongly shows
		// "requires an OpenGL shader context" even when GL is available.
		useGLModel = modelRendererAvailable();
		if (demoView.phase() == DebugDemoView::kPhaseModel && useGLModel && (rmdModel || gmoModel)) {
			if (!glModelActive) {
				glModelActive = true;
				modelSpinStart = g_system->getMillis();
				animMotion = 0;
				animMotionStart = modelSpinStart;
				animHadMotion = false;
				debug("SMT: model phase using OpenGL renderer (%dx%d)", g_system->getWidth(), g_system->getHeight());
			}

			prepareModelFrame();
			if (rmdModel) {
				// RMD's own forward axis faces away from GMO's -- angle 0 is
				// the model's back. Offset the live spin by a half-turn so
				// the model starts front-on instead of showing its back.
				Common::Array<Graphics::EffectBatch> effectBatches;
				const Graphics::EffectCamera camera = Graphics::rmdEffectCamera(modelAngle + (float)M_PI);
				for (uint i = 0; i < rmdEffects.size(); ++i)
					if (rmdEffects[i].slot < 0 || rmdEffects[i].slot == rmdEffectSlot)
						rmdEffects[i].effect->draw(camera, effectBatches);
				if (rmdTrail && rmdTrail->ok())
					rmdTrail->draw(0xFFFFFFFF, effectBatches);
				Graphics::RMDRenderer::render(*rmdModel, screenWidth, screenHeight, modelAngle + (float)M_PI,
				                              frameRmdPose, rmdFixedBounds.valid ? &rmdFixedBounds : nullptr,
				                              &rmdFixedBounds, true /* unlit standalone demo */, frameRmdMaterials,
				                              &effectBatches, effectTextures.get());
			} else {
				Graphics::GMORenderer::render(*gmoModel, screenWidth, screenHeight, modelAngle, gmoIsPersona,
				                      frameBpp, frameMpp, gmoFixedBounds.valid ? &gmoFixedBounds : nullptr,
				                      nullptr, true /* unlit standalone demo */);
			}

			if (statusFont) {
				uint32 white = Graphics::PixelFormat::createFormatRGBA32().RGBToColor(255, 255, 255);
				int modelIndex = rmdModel ? rmdIndex : gmoIndex;
				uint modelCount = rmdModel ? (uint)rmdEntries.size() : (uint)gmoEntries.size();
				bool isPersona = rmdModel ? rmdEntries[rmdIndex].persona : gmoIsPersona;
				Common::String modelLabel = rmdModel ? rmdEntries[rmdIndex].label : gmoEntries[gmoIndex].label;
				Common::String msg = animLabel.empty()
				    ? Common::String::format("Model %d/%u: %s%s", modelIndex + 1, modelCount,
				                             modelLabel.c_str(), isPersona ? " [persona]" : "")
				    : Common::String::format("%s  [%s%s]", animLabel.c_str(),
				                             modelLabel.c_str(), isPersona ? " [persona]" : "");
				renderText(*statusFont, msg, 0, screenHeight - statusFont->getFontHeight() * 2 - 6,
				           screenWidth, white, Graphics::kTextAlignCenter);
				renderText(*statusFont, "SPACE finish | Up/Down cycle model", 0,
				           screenHeight - statusFont->getFontHeight() - 4, screenWidth, white, Graphics::kTextAlignCenter);
			}

			g_system->updateScreen();
			g_system->delayMillis(10);
			if (skipPressed)
				demoView.advance();
			if (demoView.phase() == DebugDemoView::kPhaseDone)
				break;
			continue;
		}
#endif

		switch (demoView.phase()) {
		case DebugDemoView::kPhaseVideo: {
			// Spread the real RMD-tree/texture-bustup scan across many
			// frames of the opening video instead of blocking before it can
			// even start (measured ~4-6s combined on a real P4 corpus).
			// Small per-frame budgets: this runs once per frame at whatever
			// frame rate the video is decoding at, and finishes well before
			// a real opening video ends.
			if (!rmdDiscoveryDone && stepRmdDiscovery(rmdDiscoveryWork, rmdEntries, nestedRmdArchives, 8)) {
				rmdDiscoveryDone = true;
				finishRmdDiscoveryAndPickModel();
			}
			if (!textureScanDone && stepTextureScan(8))
				textureScanDone = true;

			renderClear(0, 0, 0);
			// The decoder owns its audio track (CRI ADX / ATRAC3+), so
			// start() begins audio and video together and stop() ends both.
			if (!openingAudioStarted) {
				openingAudioStarted = true;
				if (openingLoaded)
					openingDecoder.start();
			}

			bool videoStillPlaying = false;
			bool newVideoFrame = false;
			if (!videoStartLogged) {
				videoStartLogged = true;
				debug("SMT: video phase start: openingLoaded=%d endOfVideo=%d needsUpdate=%d", openingLoaded ? 1 : 0, openingDecoder.endOfVideo() ? 1 : 0, openingDecoder.needsUpdate() ? 1 : 0);
			}
			if (openingLoaded && !openingDecoder.endOfVideo()) {
				videoStillPlaying = true;
				if (openingDecoder.needsUpdate()) {
					const Graphics::Surface *frame = openingDecoder.decodeNextFrame();
					if (frame) {
						videoFrame = frame;
						newVideoFrame = true;
						videoFrameCount++;
						if (videoFrameCount == 1 || videoFrameCount % 60 == 0)
							debug("SMT: video frame %d, %ux%u", videoFrameCount, frame->w, frame->h);
					}
				}
			}

			// Letterbox-fit the current frame to the screen, preserving its
			// aspect ratio. The decoder reuses one surface, so only re-upload
			// the texture when a new frame actually arrived -- otherwise just
			// redraw the cached texture.
			if (videoFrame && videoFrame->w > 0 && videoFrame->h > 0) {
				int dstW, dstH, dstX, dstY;
				fitToScreen(videoFrame->w, videoFrame->h, screenWidth, screenHeight, dstW, dstH, dstX, dstY);
				renderSurface(*videoFrame, dstX, dstY, dstW, dstH, newVideoFrame);
			}
			if (statusFont) {
				uint32 white = Graphics::PixelFormat::createFormatRGBA32().RGBToColor(255, 255, 255);
				renderText(*statusFont, "SPACE skip", 0, screenHeight - statusFont->getFontHeight() - 4,
				           screenWidth, white, Graphics::kTextAlignCenter);
			}

			if (skipPressed || !videoStillPlaying) {
				// The next phases need rmdEntries/textureEntries fully
				// populated -- normally both finish well before a real
				// opening video ends, but if the video is skipped early
				// finish whatever's left synchronously here rather than
				// entering the next phase with a partial scan.
				if (!rmdDiscoveryDone) {
					while (!stepRmdDiscovery(rmdDiscoveryWork, rmdEntries, nestedRmdArchives, 1000)) {}
					rmdDiscoveryDone = true;
					finishRmdDiscoveryAndPickModel();
				}
				while (!textureScanDone)
					textureScanDone = stepTextureScan(1000);
				// Stop the opening video and its decoder-owned audio track.
				if (openingLoaded)
					openingDecoder.stop();
				openingAudioStarted = false;
				demoView.advance();
				phaseBgmStarted = false;
				debug("SMT: demo phase 2/3: texture + BGM song");
			}
			break;
		}

		case DebugDemoView::kPhaseTextureSong: {
			renderClear(16, 16, 24);
			// Start the BGM once (already decoded into adxAudio by
			// loadBgmIndex(), called at startup and by Up/Down below).
			if (!phaseBgmStarted) {
				if (adxAudio) {
					_mixer->playStream(Audio::Mixer::kMusicSoundType, &adxHandle, adxAudio, DisposeAfterUse::YES);
					adxOwnedByMixer = true;
					debug("SMT: playing BGM %s", bgmEntries.empty() ? "" : bgmEntries[bgmIndex].label.c_str());
				}
				phaseBgmStarted = true;
			}

			// Draw the currently-selected texture entry (bust portrait or
			// persona icon), scaled to fit and centered.
			Graphics::Surface *t = textureSurface;
			if (t && t->w > 0 && t->h > 0) {
				int dstW, dstH, dstX, dstY;
				fitToScreen(t->w, t->h, screenWidth, screenHeight, dstW, dstH, dstX, dstY);
				renderSurface(*t, dstX, dstY, dstW, dstH, true);
			}
			if (statusFont) {
				uint32 white = Graphics::PixelFormat::createFormatRGBA32().RGBToColor(255, 255, 255);
				Common::String texMsg = textureEntries.empty() ? Common::String("no texture")
				    : Common::String::format("Texture %d/%u: %s", textureIndex + 1,
				          (unsigned)textureEntries.size(), textureEntries[textureIndex].label.c_str());
				Common::String bgmMsg;
				if (!bgmEntries.empty() && adxAudio) {
					bgmMsg = Common::String::format(" | BGM %d/%u: %s (%d:%02d)", bgmIndex + 1,
					    (unsigned)bgmEntries.size(), bgmEntries[bgmIndex].label.c_str(),
					    bgmDurationSecs / 60, bgmDurationSecs % 60);
				} else if (!bgmEntries.empty()) {
					bgmMsg = Common::String::format(" | BGM %d/%u: %s", bgmIndex + 1,
					    (unsigned)bgmEntries.size(), bgmEntries[bgmIndex].label.c_str());
				}
				Common::String msg = Common::String::format("%s%s", texMsg.c_str(), bgmMsg.c_str());
				renderText(*statusFont, msg, 0, screenHeight - statusFont->getFontHeight() * 2 - 6,
				           screenWidth, white, Graphics::kTextAlignCenter);
				renderText(*statusFont, "SPACE next | Up/Down cycle texture+BGM", 0,
				           screenHeight - statusFont->getFontHeight() - 4, screenWidth, white, Graphics::kTextAlignCenter);
			}

			if (skipPressed) {
				_mixer->stopHandle(adxHandle);
				demoView.advance();
				modelSpinStart = g_system->getMillis();
				animMotion = 0;
				animMotionStart = modelSpinStart;
				animHadMotion = false;
				debug("SMT: demo phase 3/3: rotating 3D model");
			}
			break;
		}

		case DebugDemoView::kPhaseModel: {
			renderClear(16, 16, 24);
			// Reached only when no model was found (or, defensively, when the
			// GL context is unavailable); OpenGL itself is guaranteed by the
			// startup check.
			if (statusFont) {
				uint32 white = Graphics::PixelFormat::createFormatRGBA32().RGBToColor(255, 255, 255);
				const char *msg = gmoModel || rmdModel ? "Model phase requires an OpenGL shader context"
				                           : "No model found for the model phase";
				renderText(*statusFont, msg, 0, screenHeight - statusFont->getFontHeight() - 4,
				           screenWidth, white, Graphics::kTextAlignCenter);
			}
			if (skipPressed)
				demoView.advance();
			break;
		}

		case DebugDemoView::kPhaseDone:
		default:
			break;
		}

		g_system->updateScreen();
		g_system->delayMillis(10);

		if (demoView.phase() == DebugDemoView::kPhaseDone)
			break;
	}

	// Stop the texture-phase BGM. Once handed to the mixer (DisposeAfterUse::
	// YES) it owns deletion; only delete here if it never made it that far
	// (e.g. quitting during the video phase, before playStream ever ran).
	_mixer->stopHandle(adxHandle);
	if (!adxOwnedByMixer)
		delete adxAudio;
	adxAudio = nullptr;

	// Release GL objects (blitter shader/VBO/textures and the model renderer)
	// while the context is still alive.
#ifdef USE_OPENGL_SHADERS
	OpenGL::Blitter::shutdown();
	Graphics::GMORenderer::shutdown();
	Graphics::RMDRenderer::shutdown();
#endif

	for (int i = (int)nestedRmdArchives.size() - 1; i >= 0; --i)
		delete nestedRmdArchives[i];
	delete dataCvm;
	delete battleCvm;
	delete gameCpk;

	delete gmoModel;
	clearRmdEffects();
	delete rmdModel;
	if (textureSurface) {
		textureSurface->free();
		delete textureSurface;
	}
	return Common::kNoError;
}

} // End of namespace SMT
