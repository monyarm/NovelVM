#include <cxxtest/TestSuite.h>

#include <cstring>

#include "common/formats/rmd.h"
#include "common/memstream.h"

class RMDAnimationTestSuite : public CxxTest::TestSuite {
private:
	static void append32(Common::Array<byte> &out, uint32 value) {
		out.push_back((byte)value); out.push_back((byte)(value >> 8));
		out.push_back((byte)(value >> 16)); out.push_back((byte)(value >> 24));
	}
	static void append16(Common::Array<byte> &out, uint16 value) {
		out.push_back((byte)value); out.push_back((byte)(value >> 8));
	}

	static void appendFloat(Common::Array<byte> &out, float value) {
		uint32 bits;
		std::memcpy(&bits, &value, sizeof(bits));
		append32(out, bits);
	}

	static Common::Array<byte> node(uint32 type, const Common::Array<byte> &payload) {
		Common::Array<byte> out;
		append32(out, type); append32(out, payload.size()); append32(out, 0x1C020037);
		for (uint32 i = 0; i < payload.size(); ++i) out.push_back(payload[i]);
		return out;
	}

	static void appendNode(Common::Array<byte> &out, uint32 type, const Common::Array<byte> &payload) {
		Common::Array<byte> child = node(type, payload);
		for (uint32 i = 0; i < child.size(); ++i) out.push_back(child[i]);
	}

	static Common::RMD parse(const Common::Array<byte> &data) {
		Common::MemoryReadStream stream(data.data(), data.size(), DisposeAfterUse::NO);
		return Common::RMD(&stream);
	}

	static Common::Array<byte> makePersonaAnimation(bool compactHeader = false, bool implicitBone = false) {
		Common::Array<byte> frameStruct;
		append32(frameStruct, 1);
		for (uint32 i = 0; i < 12; ++i)
			appendFloat(frameStruct, i == 0 || i == 5 || i == 10 ? 1.0f : 0.0f);
		append32(frameStruct, 0xFFFFFFFF);
		append32(frameStruct, 0);
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> animation;
		append32(animation, 0x100); // Persona animation format.
		append32(animation, 1); // One bone track.
		append32(animation, 1); // One key.
		append32(animation, 0);
		appendFloat(animation, 2.0f);
		if (!compactHeader) {
			append32(animation, 0); append32(animation, 0); append32(animation, 0x78);
			append32(animation, 0); append32(animation, 0); append32(animation, 0);
		}
		if (implicitBone)
			appendFloat(animation, 1.0f); // Time; one-track variant omits the bone index.
		else {
			append32(animation, 0); // Track/bone index.
			appendFloat(animation, 1.0f); // Time.
		}
		appendFloat(animation, 2.0f); appendFloat(animation, 3.0f); appendFloat(animation, 4.0f);
		appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 1.0f);
		if (implicitBone)
			appendFloat(animation, 0.0f); // One-track records retain a trailing field.

		Common::Array<byte> data;
		appendNode(data, 14, frameList);
		appendNode(data, 0x1B, animation);
		return data;
	}

	static Common::Array<byte> makePersonaPackedAnimation() {
		Common::Array<byte> frameStruct;
		append32(frameStruct, 1);
		for (uint32 i = 0; i < 12; ++i)
			appendFloat(frameStruct, i == 0 || i == 5 || i == 10 ? 1.0f : 0.0f);
		append32(frameStruct, 0xFFFFFFFF);
		append32(frameStruct, 0);
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> animation;
		append32(animation, 0x100);
		append32(animation, 2); // RenderWare compressed keyframe type.
		append32(animation, 1);
		append32(animation, 0);
		appendFloat(animation, 2.0f);
		appendFloat(animation, 0.0f); // Time.
		append16(animation, 0); append16(animation, 0); append16(animation, 0); append16(animation, 0x7800); // Rotation.
		append16(animation, 0); append16(animation, 0); append16(animation, 0); // Translation.
		append32(animation, 0xFFFFFFFF); // Ignored for the initial key.
		appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); // Offset.
		appendFloat(animation, 1.0f); appendFloat(animation, 1.0f); appendFloat(animation, 1.0f); // Scalar.

		Common::Array<byte> data;
		appendNode(data, 14, frameList);
		appendNode(data, 0x1B, animation);
		return data;
	}

	static Common::Array<byte> makePersona52Animation(bool unknownMarker = false) {
		Common::Array<byte> frameStruct;
		append32(frameStruct, 2);
		for (uint32 i = 0; i < 2; ++i) {
			for (uint32 j = 0; j < 12; ++j)
				appendFloat(frameStruct, j == 0 || j == 5 || j == 10 ? 1.0f : 0.0f);
			append32(frameStruct, i == 0 ? 0xFFFFFFFF : 0);
			append32(frameStruct, 0);
		}
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> animation;
		append32(animation, 0x100);
		append32(animation, unknownMarker ? 0x44a07196 : 0x44a07195);
		append32(animation, 4);
		append32(animation, 0);
		appendFloat(animation, 2.0f);
		const uint32 root = 0xDEADBEEF;
		for (uint32 key = 0; key < 4; ++key) {
			appendFloat(animation, key < 2 ? 0.0f : (key == 2 ? 1.0f : 2.0f));
			if (key == 1) {
				append32(animation, 0xFFC00000); append32(animation, 0xFFC00000);
				append32(animation, 0x5F1CC471); append32(animation, 0xFFC00000);
			} else {
				appendFloat(animation, 0.0f); appendFloat(animation, 0.0f);
				appendFloat(animation, key == 3 ? 0.70710677f : 0.0f);
				appendFloat(animation, key == 3 ? 0.70710677f : 1.0f);
			}
			appendFloat(animation, key == 1 ? 10.0f : 0.0f);
			appendFloat(animation, key == 2 ? 20.0f : 0.0f);
			appendFloat(animation, key == 3 ? 30.0f : 0.0f);
			appendFloat(animation, 1.0f); appendFloat(animation, 1.0f);
			appendFloat(animation, 1.0f); appendFloat(animation, key < 2 ? 0.0f : 1.0f);
			append32(animation, key < 2 ? root : key == 2 ? 0 : 52);
		}

		Common::Array<byte> data;
		appendNode(data, 14, frameList);
		appendNode(data, 0x1B, animation);
		return data;
	}

	static Common::Array<byte> makeSkinnedAnimation(bool invalidWeight = false, bool malformedSplit = false, bool legacySkin = false, bool remappedMeshes = false, bool reverseHierarchy = false, bool cycle = false, bool malformedLegacy = false, bool invalidRemap = false, bool undercountRLE = false, bool trailingRLE = false, bool invalidAnimation = false, bool wrongPalette = false, uint32 nonFiniteMatrixSlot = 16) {
		Common::Array<byte> frameStruct;
		append32(frameStruct, 2);
		for (uint32 i = 0; i < 2; ++i) {
			for (uint32 j = 0; j < 12; ++j)
				appendFloat(frameStruct, (j == 0 || j == 5 || j == 10) ? 1.0f : (j == 9 && i == 1 ? 1.0f : 0.0f));
			append32(frameStruct, cycle && i == 1 ? 1 : reverseHierarchy ? (i == 0 ? 1 : 0xFFFFFFFF) : i == 0 ? 0xFFFFFFFF : 0);
			append32(frameStruct, 0);
		}
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> skin;
		skin.push_back(2); skin.push_back(legacySkin ? 0 : (wrongPalette ? 1 : 2)); skin.push_back(4); skin.push_back(0);
		if (legacySkin) {
			append32(skin, 0xDEADDEAD); append32(skin, malformedLegacy ? 0 : 0xDEADDEAD);
		} else {
			skin.push_back(0);
			if (!wrongPalette)
				skin.push_back(1);
		}
		skin.push_back(0); skin.push_back(1); skin.push_back(0); skin.push_back(0);
		skin.push_back(1); skin.push_back(0); skin.push_back(1); skin.push_back(0);
		append32(skin, invalidWeight ? 0x7FC00000 : 0x3E800000);
		appendFloat(skin, 0.25f); appendFloat(skin, 0.25f); appendFloat(skin, 0.25f);
		appendFloat(skin, 0.1f); appendFloat(skin, 0.2f); appendFloat(skin, 0.3f); appendFloat(skin, 0.4f);
		for (uint32 bone = 0; bone < 2; ++bone)
			for (uint32 row = 0; row < 4; ++row)
				for (uint32 column = 0; column < 4; ++column) {
				const uint32 i = row * 4 + column;
				if (i == nonFiniteMatrixSlot)
					append32(skin, 0xFFFFFFFF);
				else
					appendFloat(skin, row == 3 && column == 0 && bone == 0 ? 2.0f :
					            row == 3 && column == 1 && bone == 1 ? 3.0f :
					            row < 3 && row == column ? 1.0f : 0.0f);
			}
		if (!legacySkin || remappedMeshes) {
			const uint32 rleSize = undercountRLE ? 1 : trailingRLE ? 3 : remappedMeshes ? 2 : 0;
			append32(skin, 2); append32(skin, malformedSplit ? 1 : (remappedMeshes ? 2 : 0)); append32(skin, rleSize);
			if (remappedMeshes) {
				skin.push_back(invalidRemap ? 2 : 1); skin.push_back(0xFF);
				skin.push_back(0); skin.push_back(1);
				skin.push_back(1); skin.push_back(1);
				skin.push_back(0); skin.push_back(1);
				skin.push_back(1); skin.push_back(1);
				if (trailingRLE)
					skin.push_back(2);
			} else if (undercountRLE) {
				append16(skin, 0);
			}
		}

		Common::Array<byte> geometryStruct;
		append32(geometryStruct, 0x2); append32(geometryStruct, 0); append32(geometryStruct, 2); append32(geometryStruct, 1);
		for (uint32 i = 0; i < 24; ++i) appendFloat(geometryStruct, 0.0f);
		Common::Array<byte> geometry;
		appendNode(geometry, 1, geometryStruct);
		Common::Array<byte> extension;
		appendNode(extension, 0x116, skin);
		appendNode(geometry, 3, extension);

		Common::Array<byte> geometryList;
		appendNode(geometryList, 1, Common::Array<byte>());
		appendNode(geometryList, 15, geometry);
		Common::Array<byte> clump;
		appendNode(clump, 1, Common::Array<byte>());
		appendNode(clump, 14, frameList);
		appendNode(clump, 26, geometryList);

		Common::Array<byte> animation;
		append32(animation, 1); append32(animation, 2); appendFloat(animation, 10.0f);
		append32(animation, 0); append32(animation, 2);
		appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 1.0f);
		appendFloat(animation, 10.0f); appendFloat(animation, 2.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 0.0f); appendFloat(animation, 1.0f);

		Common::Array<byte> data;
		appendNode(data, 16, clump);
		appendNode(data, 0x108, animation);
		Common::Array<byte> compressed;
		append32(compressed, 1); append32(compressed, 2); appendFloat(compressed, 10.0f);
		append32(compressed, 0); append32(compressed, 2);
		for (uint32 key = 0; key < 2; ++key) {
			appendFloat(compressed, key ? 10.0f : 0.0f);
			append16(compressed, key ? 32767 : 0); append16(compressed, 0); append16(compressed, 0);
			append16(compressed, 0); append16(compressed, 0); append16(compressed, 0); append16(compressed, 32767); append16(compressed, 0);
		}
		appendNode(data, 0x108, compressed);
		Common::Array<byte> standard;
		append32(standard, 1); append32(standard, 4); append32(standard, 0); appendFloat(standard, 10.0f);
		for (uint32 key = 0; key < 4; ++key) {
			if (invalidAnimation && key == 1)
				append32(standard, 0x7FC00000);
			else
				appendFloat(standard, key >= 2 ? 10.0f : 0.0f);
			appendFloat(standard, 0.0f); appendFloat(standard, 0.0f); appendFloat(standard, 0.0f); appendFloat(standard, 1.0f);
			appendFloat(standard, key == 2 ? 2.0f : 0.0f); appendFloat(standard, key == 3 ? 3.0f : 0.0f); appendFloat(standard, 0.0f);
			append32(standard, key == 0 || key == 1 ? 0xFFFFFFFF : (key == 2 ? 0 : 40));
			append32(standard, 0);
		}
		appendNode(data, 0x108, standard);
		return data;
	}

public:
	void testParsesPersonaUncompressedAnimationNode() {
		Common::RMD rmd = parse(makePersonaAnimation());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		if (rmd.animations().size() == 1) {
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks.size(), 1u);
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks[0].bone, 0u);
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks[0].keys.size(), 1u);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[0].time, 1.0f, 0.0001f);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[0].translation.x, 2.0f, 0.0001f);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[0].translation.y, 3.0f, 0.0001f);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[0].translation.z, 4.0f, 0.0001f);
		}
	}

	void testParsesCompactPersonaUncompressedAnimationNode() {
		Common::RMD rmd = parse(makePersonaAnimation(true));
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		if (rmd.animations().size() == 1)
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks[0].keys.size(), 1u);
	}

	void testParsesImplicitBonePersonaAnimationNode() {
		Common::RMD rmd = parse(makePersonaAnimation(true, true));
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		if (rmd.animations().size() == 1) {
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks.size(), 1u);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[0].time, 1.0f, 0.0001f);
		}
	}

	void testEvaluatesPersonaPackedAnimationNode() {
		Common::RMD rmd = parse(makePersonaPackedAnimation());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		if (rmd.animations().size() == 1) {
			TS_ASSERT(rmd.animations()[0].compressed);
			Common::RMD::BonePose pose;
			TS_ASSERT(rmd.evaluateAnimation(0, 0.0f, pose));
			TS_ASSERT_DELTA(pose.localTransforms[0].m[0], 1.0f, 0.0001f);
			TS_ASSERT_DELTA(pose.localTransforms[0].m[12], 0.0f, 0.0001f);
		}
	}

	void testParsesPersona52ByteAnimationNode() {
		Common::RMD rmd = parse(makePersona52Animation());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		if (rmd.animations().size() == 1) {
			TS_ASSERT_EQUALS(rmd.animations()[0].keyframeCount, 4u);
			TS_ASSERT_DELTA(rmd.animations()[0].duration, 2.0f, 0.0001f);
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks.size(), 2u);
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks[0].keys.size(), 2u);
			TS_ASSERT_EQUALS(rmd.animations()[0].tracks[1].keys.size(), 2u);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[1].time, 1.0f, 0.0001f);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[0].keys[1].translation.y, 20.0f, 0.0001f);
			TS_ASSERT_DELTA(rmd.animations()[0].tracks[1].keys[1].rotation.z, 0.70710677f, 0.0001f);
		}
	}

	void testRejectsUnknownPersona52ByteVariant() {
		Common::RMD rmd = parse(makePersona52Animation(true));
			TS_ASSERT(!rmd.ok());
	}

	void testParsesPersonaAnimationSetFraming() {
		Common::Array<byte> regular = makePersonaAnimation();
		Common::Array<byte> data;
		append32(data, 0xF0F000F0); append32(data, 1); append32(data, 0x40000000);
		for (uint32 i = 0; i < regular.size(); ++i)
			data.push_back(regular[i]);
		appendNode(data, 0xF0F00004, Common::Array<byte>());
		Common::Array<byte> particle;
		append32(particle, 0xCAFEBABE);
		appendNode(data, 0xF0F000E0, particle);

		Common::RMD rmd = parse(data);
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 1u);
		TS_ASSERT_EQUALS(rmd.opaqueNodes().size(), 1u);
	}

	void testParsesSkinInfluencesAndMatrices() {
		Common::RMD rmd = parse(makeSkinnedAnimation());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.geometries().size(), 1u);
		TS_ASSERT(rmd.geometries()[0].skinned);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.boneCount, 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences.size(), 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[0].indices[0], 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[0].indices[1], 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[0].indices[2], 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[0].indices[3], 0u);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[0].weights[0], 0.25f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[0].weights[1], 0.25f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[0].weights[2], 0.25f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[0].weights[3], 0.25f, 0.0001f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[1].indices[0], 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[1].indices[1], 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[1].indices[2], 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.influences[1].indices[3], 0u);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[1].weights[0], 0.1f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[1].weights[1], 0.2f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[1].weights[2], 0.3f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.influences[1].weights[3], 0.4f, 0.0001f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.inverseBindMatrices.size(), 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.meshBoneRemap.size(), 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.inverseBindMatrices[0].m[0], 1.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.inverseBindMatrices[1].m[15], 1.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.rendererBoneFrames.size(), 2u);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.inverseBindMatrices[0].m[12], 2.0f, 0.0001f);
		TS_ASSERT_DELTA(rmd.geometries()[0].skin.inverseBindMatrices[1].m[13], 3.0f, 0.0001f);
	}

	void testEvaluatesAnimationWithHierarchyAndClampsTime() {
		Common::RMD rmd = parse(makeSkinnedAnimation());
		TS_ASSERT_EQUALS(rmd.animations().size(), 3u);
		Common::RMD::BonePose pose;
		TS_ASSERT(rmd.evaluateAnimation(0, 5.0f, pose));
		TS_ASSERT_EQUALS(pose.worldTransforms.size(), 2u);
		TS_ASSERT_DELTA(pose.worldTransforms[1].m[12], 2.0f, 0.0001f);
		TS_ASSERT(rmd.evaluateAnimation(0, -1.0f, pose));
		TS_ASSERT_DELTA(pose.localTransforms[0].m[12], 0.0f, 0.0001f);
		TS_ASSERT(rmd.evaluateAnimation(0, 20.0f, pose));
		TS_ASSERT_DELTA(pose.localTransforms[0].m[12], 2.0f, 0.0001f);
		TS_ASSERT(rmd.evaluateAnimation(1, 5.0f, pose));
		TS_ASSERT(rmd.animations()[1].compressed);
		TS_ASSERT_DELTA(pose.localTransforms[0].m[12], 0.5f, 0.0001f);
	}

	void testEvaluatesStandardHAnimTracksPerBone() {
		Common::RMD rmd = parse(makeSkinnedAnimation());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animations().size(), 3u);
		TS_ASSERT_EQUALS(rmd.animations()[2].tracks.size(), 2u);
		Common::RMD::BonePose pose;
		TS_ASSERT(rmd.evaluateAnimation(2, 5.0f, pose));
		TS_ASSERT_DELTA(pose.localTransforms[0].m[12], 1.0f, 0.0001f);
		TS_ASSERT_DELTA(pose.localTransforms[1].m[13], 1.5f, 0.0001f);
	}

	void testRejectsNonFiniteSkinWeightAndSum() {
		TS_ASSERT(!parse(makeSkinnedAnimation(true)).ok());
	}

	void testNormalizesKnownNonFiniteSkinMatrixPadding() {
		Common::RMD m7 = parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, false, false, 7));
		Common::RMD m11 = parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, false, false, 11));
		Common::RMD m15 = parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, false, false, 15));
		TS_ASSERT(m7.ok());
		TS_ASSERT(m11.ok());
		TS_ASSERT(m15.ok());
		if (m7.ok())
			TS_ASSERT_EQUALS(m7.geometries()[0].skin.inverseBindMatrices[0].m[7], 0.0f);
		if (m11.ok())
			TS_ASSERT_EQUALS(m11.geometries()[0].skin.inverseBindMatrices[0].m[11], 0.0f);
		if (m15.ok())
			TS_ASSERT_EQUALS(m15.geometries()[0].skin.inverseBindMatrices[0].m[15], 1.0f);
	}

	void testRejectsNonFiniteSkinMatrixNonPadding() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, false, false, 0)).ok());
	}

	void testAcceptsFullBoneSkinIndexWithCompactUsedList() {
		TS_ASSERT(parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, false, true)).ok());
	}

	void testRejectsTruncatedAnimationFrame() {
		Common::Array<byte> data = makeSkinnedAnimation();
		data.remove_at(data.size() - 1);
		TS_ASSERT(!parse(data).ok());
	}

	void testRejectsMalformedSplitRLE() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, true)).ok());
	}

	void testParsesLegacySkinWithoutModernTrailer() {
		Common::RMD rmd = parse(makeSkinnedAnimation(false, false, true));
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.usedBoneCount, 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.meshCount, 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].skin.rendererBoneFrames[1], 1u);
	}

	void testRejectsMalformedLegacySkinPadding() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, true, false, false, false, true)).ok());
	}

	void testKeepsNonIdentityMeshRemapOutOfInverseBindMapping() {
		Common::RMD rmd = parse(makeSkinnedAnimation(false, false, false, true));
		TS_ASSERT(rmd.ok());
		const Common::RMD::Skin &skin = rmd.geometries()[0].skin;
		TS_ASSERT_EQUALS(skin.meshCount, 2u);
		TS_ASSERT_EQUALS(skin.meshBoneRemap[0], 1u);
		TS_ASSERT_EQUALS(skin.meshBoneRemap[1], 0xFFu);
		TS_ASSERT_EQUALS(skin.meshRLECounts[0], 1u);
		TS_ASSERT_EQUALS(skin.meshRLECounts[1], 0x0101u);
		TS_ASSERT_EQUALS(skin.rle[0], 1u);
		TS_ASSERT_EQUALS(skin.rle[1], 0x0101u);
		TS_ASSERT_EQUALS(skin.rendererBoneFrames[0], 0u);
		TS_ASSERT_EQUALS(skin.rendererBoneFrames[1], 1u);
	}

	void testRejectsInvalidNonIdentityMeshRemap() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, true, false, false, false, true)).ok());
	}

	void testRejectsUndercountedRLETable() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, true, false, false, false, false, true)).ok());
	}

	void testRejectsTrailingRLETableEntries() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, true, false, false, false, false, false, true)).ok());
	}

	void testRejectsNonFiniteAnimationKeyValues() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, false, false, false, false, false, false, false, true)).ok());
	}

	void testRejectsHierarchyCycle() {
		TS_ASSERT(!parse(makeSkinnedAnimation(false, false, false, false, false, true)).ok());
	}

	void testEvaluatesHierarchyWhenChildPrecedesParent() {
		Common::RMD rmd = parse(makeSkinnedAnimation(false, false, false, false, true));
		Common::RMD::BonePose pose;
		TS_ASSERT(rmd.ok());
		TS_ASSERT(rmd.evaluateAnimation(0, 0.0f, pose));
		TS_ASSERT_DELTA(pose.worldTransforms[0].m[12], 1.0f, 0.0001f);
	}
};
