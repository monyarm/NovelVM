#include <cxxtest/TestSuite.h>

#include "common/file.h"
#include "common/formats/rmd.h"
#include "common/endian.h"
#include "common/path.h"
#include "../../system/null_osystem.h"

#include <stdlib.h>
#include <cmath>

// forbidden.h is already included by the generated runner before this file.
#undef getenv

/**
 * Real-file RMD checks. No game data is stored in the repository.
 *
 *   NOVELVM_RMD_CORPUS=<.../rmd-corpus>
 *   NOVELVM_RMD_P3_FES_FILE=<specific P3 FES .rmd, optional>
 *   NOVELVM_RMD_P3_OLD_VERSION_FILE=<specific P3 FES RMD using 0x1C02002D, optional>
 *   NOVELVM_RMD_P4_FILE=<specific P4 .rmd, optional>
 *   NOVELVM_RMD_P3_FILE=<specific original P3 .rmd, optional>
 *   NOVELVM_RMD_SKINNED_FILE=<inventory-identified skinned/animated .rmd>
 */
class RMDRealTestSuite : public CxxTest::TestSuite {
private:
	static const char *environment(const char *name) {
		const char *value = getenv(name);
		return value && *value ? value : nullptr;
	}

	static Common::String corpusFile(const char *title, const char *file, const char *overrideName) {
		const char *overridePath = environment(overrideName);
		if (overridePath)
			return overridePath;
		const char *corpus = environment("NOVELVM_RMD_CORPUS");
		return corpus ? Common::String::format("%s/%s/%s", corpus, title, file) : Common::String();
	}

	static bool openRMD(const Common::String &path, Common::File &file, const char *skipMessage) {
		if (path.empty()) {
			TS_WARN(skipMessage);
			return false;
		}
		const Common::Path fullPath(path);
		if (!file.open(Common::FSNode(fullPath))) {
			TS_FAIL("configured RMD path is missing or unreadable");
			return false;
		}
		return true;
	}

	static void assertStaticModel(Common::RMD &rmd, bool requireDecodedTexture) {
		TS_ASSERT(rmd.ok());
		if (!rmd.ok())
			return;
		TS_ASSERT(rmd.clumps().size() > 0);
		TS_ASSERT(rmd.geometries().size() > 0);
		TS_ASSERT(rmd.materials().size() > 0);
		if (requireDecodedTexture) {
			TS_ASSERT(rmd.textures().size() > 0);
		} else {
			TS_ASSERT_EQUALS(rmd.textures().size(), 0u);
		}

		bool decoded = false;
		for (uint32 i = 0; i < rmd.textures().size(); ++i) {
			Graphics::Surface *surface = rmd.getTextureSurface(i);
			if (!surface)
				continue;
			decoded = surface->w > 0 && surface->h > 0 && surface->getPixels() != nullptr;
			surface->free();
			delete surface;
			if (decoded)
				break;
		}
		if (requireDecodedTexture) {
			TS_ASSERT(decoded);
		} else {
			TS_ASSERT(!decoded);
		}
	}

	static void assertOptionalP3(const char *path) {
		if (!path) {
			TS_WARN("SKIP: NOVELVM_RMD_P3_FILE is not set; original P3 RMD smoke is unavailable");
			return;
		}
		Common::File file;
		if (!openRMD(path, file, "NOVELVM_RMD_P3_FILE does not name a readable RMD"))
			return;
		Common::RMD rmd(&file);
		assertStaticModel(rmd, true);
	}

	static void assertPersona52(const char *path, uint32 keyCount, const char *skipMessage) {
		Common::File file;
		if (!openRMD(path ? path : Common::String(), file, skipMessage))
			return;
		Common::RMD rmd(&file);
		TS_ASSERT(rmd.ok());
		if (!rmd.ok())
			return;
		bool found = false;
		for (uint32 i = 0; i < rmd.animations().size(); ++i) {
			const Common::RMD::Animation &animation = rmd.animations()[i];
			if (animation.keyframeCount != keyCount)
				continue;
			found = true;
			TS_ASSERT(animation.supported);
			TS_ASSERT(std::isfinite(animation.duration));
			TS_ASSERT(animation.tracks.size() > 0);
			Common::RMD::BonePose pose;
			TS_ASSERT(rmd.evaluateAnimation(i, 0.0f, pose));
		}
		TS_ASSERT(found);
	}

public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	void testRealP3FESStaticModel() {
		Common::String path = corpusFile("Persona_3_FES", "field-m001_001.rmd", "NOVELVM_RMD_P3_FES_FILE");
		Common::File file;
		if (!openRMD(path, file, "SKIP: set NOVELVM_RMD_CORPUS or NOVELVM_RMD_P3_FES_FILE for P3 FES RMD"))
			return;
		Common::RMD rmd(&file);
		assertStaticModel(rmd, true);
	}

	void testRealP3FESOlderRenderWareVersionIfConfigured() {
		const char *path = environment("NOVELVM_RMD_P3_OLD_VERSION_FILE");
		if (!path) {
			TS_WARN("SKIP: NOVELVM_RMD_P3_OLD_VERSION_FILE is not set; old-version P3 FES RMD is unavailable");
			return;
		}
		Common::File file;
		if (!openRMD(path, file, "NOVELVM_RMD_P3_OLD_VERSION_FILE does not name a readable RMD"))
			return;
		byte header[12];
		TS_ASSERT_EQUALS(file.read(header, sizeof(header)), sizeof(header));
		if (file.pos() != sizeof(header))
			return;
		TS_ASSERT_EQUALS(READ_LE_UINT32(header), 0x16u);
		TS_ASSERT_EQUALS(READ_LE_UINT32(header + 8), 0x1C02002Du);
		TS_ASSERT(file.seek(0));
		Common::RMD rmd(&file);
		assertStaticModel(rmd, true);
	}

	void testRealP4StaticModel() {
		Common::String path = corpusFile("Persona_4", "field-m001_002.rmd", "NOVELVM_RMD_P4_FILE");
		Common::File file;
		if (!openRMD(path, file, "SKIP: set NOVELVM_RMD_CORPUS or NOVELVM_RMD_P4_FILE for P4 RMD"))
			return;
		Common::RMD rmd(&file);
		assertStaticModel(rmd, false);
		TS_ASSERT(rmd.animations().size() > 0);
		if (rmd.animations().size() > 0) {
			Common::RMD::BonePose pose;
			TS_ASSERT(rmd.evaluateAnimation(0, 0.0f, pose));
		}
	}

	void testRealP3SmokeIfConfigured() {
		assertOptionalP3(environment("NOVELVM_RMD_P3_FILE"));
	}

	void testRealSkinnedOrAnimatedModelIfConfigured() {
		const char *path = environment("NOVELVM_RMD_SKINNED_FILE");
		if (!path) {
			TS_WARN("SKIP: NOVELVM_RMD_SKINNED_FILE is not set; no compatible verified skin/animation sample is configured");
			return;
		}
		Common::File file;
		if (!openRMD(path, file, "NOVELVM_RMD_SKINNED_FILE does not name a readable RMD"))
			return;
		Common::RMD rmd(&file);
		TS_ASSERT(rmd.ok());
		if (!rmd.ok())
			return;
		bool skinned = false;
		for (uint32 i = 0; i < rmd.geometries().size(); ++i) {
			const Common::RMD::Geometry &geometry = rmd.geometries()[i];
			skinned = skinned || geometry.skinned;
			if (geometry.skinned) {
				TS_ASSERT_EQUALS(geometry.skin.influences.size(), geometry.positions.size());
				TS_ASSERT_EQUALS(geometry.skin.inverseBindMatrices.size(), geometry.skin.boneCount);
				TS_ASSERT_EQUALS(geometry.skin.usedBones.size(), geometry.skin.usedBoneCount);
				TS_ASSERT_EQUALS(geometry.skin.rendererBoneFrames.size(), geometry.skin.boneCount);
				for (uint32 palette = 0; palette < geometry.skin.usedBones.size(); ++palette)
					TS_ASSERT_LESS_THAN(geometry.skin.usedBones[palette], rmd.frames().size());
				for (uint32 vertex = 0; vertex < geometry.skin.influences.size(); ++vertex) {
					float weightSum = 0;
					for (uint32 weight = 0; weight < 4; ++weight) {
						TS_ASSERT_LESS_THAN(geometry.skin.influences[vertex].indices[weight], geometry.skin.boneCount);
						TS_ASSERT(std::isfinite(geometry.skin.influences[vertex].weights[weight]));
						TS_ASSERT(geometry.skin.influences[vertex].weights[weight] >= 0.0f);
						weightSum += geometry.skin.influences[vertex].weights[weight];
					}
					TS_ASSERT(std::isfinite(weightSum));
					TS_ASSERT(weightSum > 0.0f);
				}
			}
		}
		if (rmd.animations().size() > 0) {
			Common::RMD::BonePose pose;
			TS_ASSERT(rmd.evaluateAnimation(0, 0.0f, pose));
		}
		TS_ASSERT(skinned || rmd.animations().size() > 0);
		// ponytail: leave title-specific 0x110/0x120 extension payloads opaque; they are metadata, not standard RMD animation nodes.
	}

	void testRealP3FESM001002Persona52IfConfigured() {
		assertPersona52(environment("NOVELVM_RMD_P3_FES_M001_002_FILE"), 446,
			"SKIP: set NOVELVM_RMD_P3_FES_M001_002_FILE for P3 FES M001_002 52-byte animation");
	}

	void testRealP3FESM006022Persona52IfConfigured() {
		assertPersona52(environment("NOVELVM_RMD_P3_FES_M006_022_FILE"), 21,
			"SKIP: set NOVELVM_RMD_P3_FES_M006_022_FILE for P3 FES M006_022 52-byte animation");
	}

	void testRealP4M007038Persona52IfConfigured() {
		assertPersona52(environment("NOVELVM_RMD_P4_M007_038_FILE"), 315,
			"SKIP: set NOVELVM_RMD_P4_M007_038_FILE for P4 M007_038 52-byte animation");
	}
};
