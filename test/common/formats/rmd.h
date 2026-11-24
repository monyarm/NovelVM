#include <cxxtest/TestSuite.h>

#include "common/formats/rmd.h"
#include "common/memstream.h"

class RMDTestSuite : public CxxTest::TestSuite {
private:
	static void write32(byte *&p, uint32 value) {
		p[0] = (byte)value;
		p[1] = (byte)(value >> 8);
		p[2] = (byte)(value >> 16);
		p[3] = (byte)(value >> 24);
		p += 4;
	}

	static void writeNode(byte *&p, uint32 type, uint32 size, uint32 version) {
		write32(p, type);
		write32(p, size);
		write32(p, version);
		for (uint32 i = 0; i < size; ++i)
			*p++ = 0;
	}

	static void append32(Common::Array<byte> &out, uint32 value) {
		out.push_back((byte)value); out.push_back((byte)(value >> 8));
		out.push_back((byte)(value >> 16)); out.push_back((byte)(value >> 24));
	}

	static void append16(Common::Array<byte> &out, uint16 value) {
		out.push_back((byte)value); out.push_back((byte)(value >> 8));
	}

	static Common::Array<byte> node(uint32 type, const Common::Array<byte> &payload, uint32 version = 0x1C020037) {
		Common::Array<byte> out;
		append32(out, type); append32(out, payload.size()); append32(out, version);
		for (uint32 i = 0; i < payload.size(); ++i) out.push_back(payload[i]);
		return out;
	}

	static void appendFloat(Common::Array<byte> &out, float value) {
		uint32 bits;
		memcpy(&bits, &value, 4);
		append32(out, bits);
	}

	static void appendBytes(Common::Array<byte> &out, const char *bytes, uint32 n) {
		for (uint32 i = 0; i < n; ++i)
			out.push_back((byte)bytes[i]);
	}

	static void appendName32(Common::Array<byte> &out, const char *name) {
		for (uint32 i = 0; i < 32; ++i)
			out.push_back(i < strlen(name) ? (byte)name[i] : 0);
	}

	static void appendNode(Common::Array<byte> &out, uint32 type, const Common::Array<byte> &payload, uint32 version = 0x1C020037) {
		Common::Array<byte> child = node(type, payload, version);
		for (uint32 i = 0; i < child.size(); ++i) out.push_back(child[i]);
	}

	static Common::Array<byte> stringNode(const char *value) {
		Common::Array<byte> payload;
		for (const char *p = value; *p; ++p) payload.push_back((byte)*p);
		payload.push_back(0);
		return node(2, payload);
	}

	static Common::RMD parse(const byte *data, uint32 size) {
		Common::MemoryReadStream stream(data, size, DisposeAfterUse::NO);
		return Common::RMD(&stream);
	}

	template<uint32 depth>
	static Common::RMD parseNestedChain() {
		byte data[12 * depth] = {0};
		for (uint32 i = 0; i < depth; ++i) {
			byte *p = data + i * 12;
			write32(p, 1);
			write32(p, (depth - i - 1) * 12);
			write32(p, 0x1C020037);
		}
		return parse(data, sizeof(data));
	}

public:
	void testRejectsEmptyInput() {
		Common::RMD rmd = parse(nullptr, 0);
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 0u);
	}

	void testRejectsTruncatedHeader() {
		byte data[11] = {0};
		Common::RMD rmd = parse(data, sizeof(data));
		TS_ASSERT(!rmd.ok());
	}

	void testRejectsPayloadPastEnd() {
		byte data[12] = {0};
		byte *p = data;
		write32(p, 1);
		write32(p, 1);
		write32(p, 0x1C020037);
		Common::RMD rmd = parse(data, sizeof(data));
		TS_ASSERT(!rmd.ok());
	}

	void testAcceptsObservedOlderRenderWareVersion() {
		Common::Array<byte> data = node(0x16, Common::Array<byte>(), 0x1C02002D);
		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 1u);
	}

	void testRejectsUnknownRenderWareVersion() {
		Common::Array<byte> data = node(0x16, Common::Array<byte>(), 0x1C02002E);
		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(!rmd.ok());
	}

	void testRejectsOverflowedPayload() {
		byte data[12] = {0};
		byte *p = data;
		write32(p, 1);
		write32(p, 0xFFFFFFFF);
		write32(p, 0x1C020037);
		Common::RMD rmd = parse(data, sizeof(data));
		TS_ASSERT(!rmd.ok());
	}

	void testRejectsOversizedInputBeforeAllocation() {
		Common::MemoryReadStream stream(nullptr, 0xFFFFFFFF, DisposeAfterUse::NO);
		Common::RMD rmd(&stream);
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 0u);
	}

	void testRejectsNestedChildPastParent() {
		byte data[24] = {0};
		byte *p = data;
		write32(p, 0x00000010);
		write32(p, 12);
		write32(p, 0x1C020037);
		write32(p, 0x00000001);
		write32(p, 1);
		write32(p, 0x1C020037);

		Common::RMD rmd = parse(data, sizeof(data));
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 0u);
	}

	void testAcceptsExactNestedDepthLimit() {
		Common::RMD rmd = parseNestedChain<65>(); // root + 64 nested levels
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 1u);
	}

	void testRejectsOneBeyondNestedDepthLimit() {
		Common::RMD rmd = parseNestedChain<66>(); // root + 65 nested levels
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 0u);
	}

	void testRejectsMalformedClumpPayload() {
		byte data[64];
		byte *p = data;
		writeNode(p, 0xF0F000F0, 2, 0x40000000);
		writeNode(p, 0x00000010, 3, 0x1C020037);
		writeNode(p, 0xF0F00004, 0, 0x40000000);

		Common::RMD rmd = parse(data, (uint32)(p - data));
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.nodeCount(), 0u);
	}

	void testParsesSyntheticSceneGeometryAndTextures() {
		Common::Array<byte> frameStruct;
		append32(frameStruct, 1);
		for (uint32 i = 0; i < 14; ++i) append32(frameStruct, i == 12 ? 0xFFFFFFFF : 0);
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> geometryStruct;
		append16(geometryStruct, 0x009F); geometryStruct.push_back(2); geometryStruct.push_back(0); // flags, numTexCoord, native flag
		append32(geometryStruct, 1); append32(geometryStruct, 3); append32(geometryStruct, 1);
		for (uint32 i = 0; i < 3; ++i) append32(geometryStruct, 0xFFFFFFFF);
		append32(geometryStruct, 0x3F800000); append32(geometryStruct, 0x40000000);
		append32(geometryStruct, 0x40400000); append32(geometryStruct, 0x40800000);
		append32(geometryStruct, 0x40A00000); append32(geometryStruct, 0x40C00000);
		append32(geometryStruct, 0x40E00000); append32(geometryStruct, 0x41000000);
		append32(geometryStruct, 0x41100000); append32(geometryStruct, 0x41200000);
		append32(geometryStruct, 0x41300000); append32(geometryStruct, 0x41400000);
		append16(geometryStruct, 0); append16(geometryStruct, 1); append16(geometryStruct, 0); append16(geometryStruct, 2);
		append32(geometryStruct, 0); append32(geometryStruct, 0); append32(geometryStruct, 0); append32(geometryStruct, 1); append32(geometryStruct, 1); append32(geometryStruct, 1);
		append32(geometryStruct, 0); append32(geometryStruct, 0); append32(geometryStruct, 0);
		append32(geometryStruct, 0x3F800000); append32(geometryStruct, 0x40000000); append32(geometryStruct, 0x40400000);
		append32(geometryStruct, 0xBF800000); append32(geometryStruct, 0xC0000000); append32(geometryStruct, 0xC0400000);
		for (uint32 i = 0; i < 3; ++i) { append32(geometryStruct, 0); append32(geometryStruct, 0); append32(geometryStruct, 0x3F800000); }
		Common::Array<byte> geometry;
		appendNode(geometry, 1, geometryStruct);
		Common::Array<byte> materialList; Common::Array<byte> materialListStruct; append32(materialListStruct, 2); append32(materialListStruct, 1); append32(materialListStruct, 0);
		appendNode(materialList, 1, materialListStruct);
		for (uint32 i = 0; i < 2; ++i) {
			Common::Array<byte> materialStruct; append32(materialStruct, 0); append32(materialStruct, 0xFF3366CC); append32(materialStruct, 0); append32(materialStruct, 1); append32(materialStruct, 0x3F800000); append32(materialStruct, 0x3F000000); append32(materialStruct, 0x3E800000);
			Common::Array<byte> material;
			appendNode(material, 1, materialStruct);
			if (i == 0) {
				Common::Array<byte> reference; Common::Array<byte> referenceStruct; append32(referenceStruct, 0x00010002); appendNode(reference, 1, referenceStruct);
				Common::Array<byte> textureName = stringNode("diffuse"); Common::Array<byte> maskName = stringNode("mask");
				for (uint32 j = 0; j < textureName.size(); ++j) reference.push_back(textureName[j]);
				for (uint32 j = 0; j < maskName.size(); ++j) reference.push_back(maskName[j]);
				appendNode(material, 6, reference);
				// UV anim plugin (0x135): STRUCT { mask = 1, name[32] } inside the material extension.
				Common::Array<byte> uvStruct; append32(uvStruct, 1);
				appendName32(uvStruct, "09 - Default");
				Common::Array<byte> uvPlugin; appendNode(uvPlugin, 1, uvStruct);
				Common::Array<byte> extension; appendNode(extension, 0x135, uvPlugin);
				// RW user data (0x11F): one string array "name" = "hasi", as on real P3 FES materials.
				Common::Array<byte> userData; append32(userData, 1);
				append32(userData, 5); appendBytes(userData, "name", 5); append32(userData, 3); append32(userData, 1);
				append32(userData, 5); appendBytes(userData, "hasi", 5);
				appendNode(extension, 0x11F, userData);
				appendNode(material, 3, extension);
			}
			appendNode(materialList, 7, material);
		}
		appendNode(geometry, 8, materialList);
		Common::Array<byte> strip; append32(strip, 0xDEADBEEF); append32(strip, 1); append32(strip, 2); append32(strip, 4); append32(strip, 1);
		append32(strip, 0); append32(strip, 1); append32(strip, 2); append32(strip, 0); appendNode(geometry, 0x50E, strip);

		Common::Array<byte> geometryListStruct; append32(geometryListStruct, 1);
		Common::Array<byte> geometryList; appendNode(geometryList, 1, geometryListStruct); appendNode(geometryList, 15, geometry);
		Common::Array<byte> clumpStruct; append32(clumpStruct, 1); append32(clumpStruct, 0); append32(clumpStruct, 0);
		Common::Array<byte> clump; appendNode(clump, 1, clumpStruct); appendNode(clump, 14, frameList); appendNode(clump, 26, geometryList);
		Common::Array<byte> atomicStruct; append32(atomicStruct, 0); append32(atomicStruct, 0); append32(atomicStruct, 0);
		Common::Array<byte> atomic; appendNode(atomic, 1, atomicStruct); appendNode(clump, 20, atomic);

		Common::Array<byte> textureStruct; append32(textureStruct, 1);
		Common::Array<byte> textureNative; Common::Array<byte> textureName = stringNode("diffuse"); Common::Array<byte> textureMask = stringNode("mask");
		for (uint32 i = 0; i < textureName.size(); ++i) textureNative.push_back(textureName[i]);
		for (uint32 i = 0; i < textureMask.size(); ++i) textureNative.push_back(textureMask[i]);
		Common::Array<byte> nativeBytes; nativeBytes.push_back(0xA5); nativeBytes.push_back(0x5A); appendNode(textureNative, 1, nativeBytes);
		Common::Array<byte> textureDict; appendNode(textureDict, 1, textureStruct); appendNode(textureDict, 21, textureNative);
		Common::Array<byte> data; appendNode(data, 22, textureDict, 0x1C020037); appendNode(data, 16, clump);
		// One animation slot holding a UV animation dictionary, laid out like the real P3 FES
		// FOBJ000.RMD one: a param (0x1C1) animation scrolling y from 0 to 1 over 1.6667s.
		Common::Array<byte> uvAnim;
		append32(uvAnim, 0x100); append32(uvAnim, 0x1C1); append32(uvAnim, 2); append32(uvAnim, 0); appendFloat(uvAnim, 1.6667f);
		append32(uvAnim, 0); appendName32(uvAnim, "09 - Default");
		for (uint32 i = 0; i < 8; ++i) append32(uvAnim, 0); // nodeToUVChannel: base set
		const float keys[2][7] = {{0, 0, 1, 1, 0, 0, 0}, {1.6667f, 0, 1, 1, 0, 0, 1}};
		for (uint32 k = 0; k < 2; ++k) {
			for (uint32 j = 0; j < 7; ++j) appendFloat(uvAnim, keys[k][j]);
			append32(uvAnim, k == 0 ? 0xFEC4A9F3 : 0); // prev: junk for the first frame, as in the file
		}
		Common::Array<byte> uvDictStruct; append32(uvDictStruct, 1);
		Common::Array<byte> uvDict; appendNode(uvDict, 1, uvDictStruct); appendNode(uvDict, 0x1B, uvAnim);
		appendNode(data, 0x2B, uvDict);
		// Visibility animation (0xF0F00080) fading material "hasi" from 1 to 0 over 1s.
		Common::Array<byte> vis;
		append32(vis, 2); appendFloat(vis, 1.0f); append32(vis, 8); append32(vis, 4); append32(vis, 0x12345678);
		appendBytes(vis, "hasi", 4);
		appendFloat(vis, 0.0f); appendFloat(vis, 1.0f); appendFloat(vis, 1.0f); appendFloat(vis, 0.0f);
		appendNode(data, 0xF0F00080, vis);
		appendNode(data, 0xF0F00004, Common::Array<byte>());
		Common::MemoryReadStream stream(data.data(), data.size(), DisposeAfterUse::NO);
		Common::RMD rmd(&stream);
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.materials()[0].uvAnimationName, "09 - Default");
		TS_ASSERT_EQUALS(rmd.uvAnimations().size(), 1u);
		TS_ASSERT_EQUALS(rmd.materials()[0].name, "hasi");
		TS_ASSERT_EQUALS(rmd.visibilityAnimations().size(), 1u);
		Common::RMD::MaterialPose materialPose;
		rmd.evaluateSlotMaterials(0, 1.6667f / 2, materialPose);
		const Common::Array<float> &uv = materialPose.uvTransforms;
		TS_ASSERT_EQUALS(uv.size(), 12u);
		if (uv.size() == 12) {
			TS_ASSERT_DELTA(uv[0], 1.0f, 1e-5f); TS_ASSERT_DELTA(uv[3], 1.0f, 1e-5f);
			TS_ASSERT_DELTA(uv[4], 0.0f, 1e-5f); TS_ASSERT_DELTA(uv[5], 0.5f, 1e-4f); // half a V scroll
			TS_ASSERT_DELTA(uv[6 + 5], 0.0f, 1e-5f); // material 1: no UV animation, identity
		}
		rmd.evaluateSlotMaterials(0, 0.5f, materialPose);
		TS_ASSERT_EQUALS(materialPose.opacity.size(), 2u);
		if (materialPose.opacity.size() == 2) {
			TS_ASSERT_DELTA(materialPose.opacity[0], 0.5f, 1e-5f); // halfway through the fade
			TS_ASSERT_DELTA(materialPose.opacity[1], 1.0f, 1e-5f);
		}
		TS_ASSERT_EQUALS(rmd.frames().size(), 1u);
		TS_ASSERT_EQUALS(rmd.geometries().size(), 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices.size(), 6u);
		TS_ASSERT(rmd.geometries()[0].triangleStrip);
		TS_ASSERT_EQUALS(rmd.geometries()[0].positions.size(), 3u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].normals.size(), 3u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].colors.size(), 3u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].uvChannels.size(), 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].uvChannels[0][1].u, 3.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].uvChannels[1][2].v, 12.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[0], 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[1], 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[2], 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[3], 2u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[4], 1u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].indices[5], 0u);
		TS_ASSERT_EQUALS(rmd.geometries()[0].boundsMin.x, -1.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].boundsMin.y, -2.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].boundsMax.z, 3.0f);
		TS_ASSERT_EQUALS(rmd.geometries()[0].materialIndices[0], 0u);
		TS_ASSERT_EQUALS(rmd.materials().size(), 2u);
		TS_ASSERT_EQUALS(rmd.materials()[0].color.r, 0x33u);
		TS_ASSERT_EQUALS(rmd.materials()[0].color.g, 0x66u);
		TS_ASSERT_EQUALS(rmd.materials()[0].color.b, 0xCCu);
		TS_ASSERT_EQUALS(rmd.materials()[0].color.a, 0xFFu);
		TS_ASSERT_EQUALS(rmd.materials()[0].ambient, 1.0f);
		TS_ASSERT_EQUALS(rmd.materials()[0].specular, 0.5f);
		TS_ASSERT_EQUALS(rmd.materials()[0].diffuse, 0.25f);
		TS_ASSERT_EQUALS(rmd.materials()[0].textureName, "diffuse");
		TS_ASSERT_EQUALS(rmd.materials()[0].maskName, "mask");
		TS_ASSERT_EQUALS(rmd.materials()[0].textureFlags, 0x00010002u);
		TS_ASSERT_EQUALS(rmd.textures().size(), 1u);
		TS_ASSERT_EQUALS(rmd.textures()[0].name, "diffuse");
		TS_ASSERT_EQUALS(rmd.textures()[0].maskName, "mask");
		TS_ASSERT(rmd.textures()[0].nativeData.size() > 0);
		TS_ASSERT_EQUALS(rmd.atomics().size(), 1u);
		TS_ASSERT_EQUALS(rmd.atomics()[0].frame, 0);
		TS_ASSERT_EQUALS(rmd.atomics()[0].geometry, 0);
		TS_ASSERT_EQUALS(rmd.clumps().size(), 1u);
		TS_ASSERT_EQUALS(rmd.clumps()[0].frames[0], 0u);
		TS_ASSERT_EQUALS(rmd.clumps()[0].geometries[0], 0u);
		TS_ASSERT_EQUALS(rmd.clumps()[0].atomics[0], 0u);
	}

	void testRejectsMalformedGeometryCountsWithoutAllocating() {
		Common::Array<byte> geometryStruct;
		append32(geometryStruct, 0x2); append32(geometryStruct, 0); append32(geometryStruct, 0xFFFFFFFF); append32(geometryStruct, 1);
		for (uint32 i = 0; i < 8; ++i) append32(geometryStruct, 0);
		Common::Array<byte> geometry; appendNode(geometry, 1, geometryStruct);
		Common::Array<byte> data; appendNode(data, 15, geometry);
		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.geometries().size(), 0u);
	}

	void testIgnoresHAnimExtensionWithoutFrame() {
		Common::Array<byte> hAnimStruct; append32(hAnimStruct, 1); append32(hAnimStruct, 42);
		Common::Array<byte> hAnim; appendNode(hAnim, 1, hAnimStruct); appendNode(hAnim, 2, stringNode("orphan"));
		Common::Array<byte> data; appendNode(data, 0x11E, hAnim);
		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(!rmd.ok());
		TS_ASSERT_EQUALS(rmd.frames().size(), 0u);
	}

	void testParsesHierarchyWithZeroNameIdAndNineNodes() {
		// Real P3F/P4 field RMDs (e.g. P4 FIELD/RMD/M001_002.RMD) start the HAnim payload with
		// 0x100, nameId 0, nodeCount 9 -- bytes that also look like a {0x100, size 0, version 9}
		// chunk header. They must still parse as a hierarchy.
		Common::Array<byte> frameStruct;
		append32(frameStruct, 1);
		for (uint32 i = 0; i < 14; ++i) append32(frameStruct, i == 12 ? 0xFFFFFFFF : 0);
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);

		Common::Array<byte> hAnim;
		append32(hAnim, 0x100); append32(hAnim, 0); append32(hAnim, 9); append32(hAnim, 0); append32(hAnim, 36);
		for (uint32 i = 0; i < 9; ++i) {
			append32(hAnim, i); append32(hAnim, (i + 3) % 9); append32(hAnim, 0);
		}
		Common::Array<byte> data;
		appendNode(data, 14, frameList);
		appendNode(data, 0x11E, hAnim);

		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
		if (rmd.ok()) {
			TS_ASSERT_EQUALS(rmd.frames()[0].nameId, 0u);
			// Hierarchy index 3 is node 0, i.e. frame 0; without the hierarchy it would be frame 3 (absent).
			TS_ASSERT_EQUALS(rmd.frameForHierarchy(3), 0);
		}
	}

	void testHierarchyIgnoresFramesWithoutHAnim() {
		// Real P4 weapons (MODEL/WEAPON/WP000.RMD): the root frame has no HAnim extension
		// (so its nameId is a default 0) and frame 1 carries HAnim node 0. Node 0 is frame 1.
		Common::Array<byte> frameStruct;
		append32(frameStruct, 2);
		for (uint32 f = 0; f < 2; ++f)
			for (uint32 i = 0; i < 14; ++i) append32(frameStruct, i == 12 ? (f == 0 ? 0xFFFFFFFF : 0) : 0);
		Common::Array<byte> hAnim;
		append32(hAnim, 0x100); append32(hAnim, 0); append32(hAnim, 1); append32(hAnim, 0); append32(hAnim, 36);
		append32(hAnim, 0); append32(hAnim, 0); append32(hAnim, 0);
		Common::Array<byte> hAnimExtension;
		appendNode(hAnimExtension, 0x11E, hAnim);
		Common::Array<byte> frameList;
		appendNode(frameList, 1, frameStruct);
		appendNode(frameList, 3, Common::Array<byte>());
		appendNode(frameList, 3, hAnimExtension);
		Common::Array<byte> data;
		appendNode(data, 14, frameList);

		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
		if (rmd.ok()) {
			TS_ASSERT_EQUALS(rmd.frames().size(), 2u);
			TS_ASSERT_EQUALS(rmd.frameForHierarchy(0), 1);
		}
	}

	void testAnimationSlotsFollowTerminatorsAndInstances() {
		// Slots as in real RMDs (and Amicitia's RmdScene): each runs up to a 0xF0F00004
		// terminator. Slot 0 is a placeholder, slot 1 an instance of slot 2, slot 2 holds a
		// transform override (64-byte matrix) and nothing skeletal.
		Common::Array<byte> instance;
		instance.push_back(2); instance.push_back(0);
		// Transform override: an RwMatrix scale (1.1, 1.05, 1.1) as on real P4 NPC variants;
		// the pad after each row is garbage and must not leak into the matrix.
		Common::Array<byte> override;
		const float rows[16] = {1.1f, 0, 0, 7, 0, 1.05f, 0, 7, 0, 0, 1.1f, 7, 0, 0, 0, 0};
		for (uint32 i = 0; i < 16; ++i) {
			uint32 bits;
			memcpy(&bits, &rows[i], 4);
			append32(override, bits);
		}
		Common::Array<byte> data;
		appendNode(data, 0xF0F00001, Common::Array<byte>());
		appendNode(data, 0xF0F00004, Common::Array<byte>());
		appendNode(data, 0xF0F00003, instance);
		appendNode(data, 0xF0F00004, Common::Array<byte>());
		appendNode(data, 0xF0F00005, override);
		appendNode(data, 0xF0F00004, Common::Array<byte>());

		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.animationSlots().size(), 3u);
		if (rmd.animationSlots().size() != 3)
			return;
		TS_ASSERT_EQUALS(rmd.animationSlots()[0].animation, -1);
		TS_ASSERT_EQUALS(rmd.animationSlots()[0].extras.size(), 0u);
		TS_ASSERT_EQUALS(rmd.animationSlots()[1].instanceOf, 2);
		TS_ASSERT_EQUALS(rmd.animationSlots()[2].extras.size(), 1u);
		TS_ASSERT_EQUALS(rmd.opaqueNodes()[rmd.animationSlots()[2].extras[0]].type, 0xF0F00005u);
		TS_ASSERT(rmd.animationSlots()[2].hasTransformOverride);
		TS_ASSERT_DELTA(rmd.animationSlots()[2].transformOverride.m[5], 1.05f, 1e-6f);
		TS_ASSERT_EQUALS(rmd.animationSlots()[2].transformOverride.m[3], 0.0f);
		TS_ASSERT_EQUALS(rmd.animationSlots()[2].transformOverride.m[15], 1.0f);
		TS_ASSERT(!rmd.animationSlots()[0].hasTransformOverride);
		TS_ASSERT_EQUALS(rmd.animationForSlot(1), -1);
		TS_ASSERT_EQUALS(rmd.animationForSlot(7), -1);
	}

	void testPreservesPersonaParticleListTrailer() {
		Common::Array<byte> particle;
		append32(particle, 0x12345678);
		particle.push_back(0xA5);
		particle.push_back(0x5A);
		Common::Array<byte> data;
		appendNode(data, 0xF0F000E0, particle, 0x40000000);
		data.push_back(0);
		data.push_back(0);
		appendNode(data, 2, Common::Array<byte>());

		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
		TS_ASSERT_EQUALS(rmd.opaqueNodes().size(), 1u);
		if (rmd.opaqueNodes().size() == 1) {
			TS_ASSERT_EQUALS(rmd.opaqueNodes()[0].type, 0xF0F000E0u);
			TS_ASSERT_EQUALS(rmd.opaqueNodes()[0].payload.size(), particle.size());
			TS_ASSERT_EQUALS(rmd.opaqueNodes()[0].payload[4], 0xA5u);
		}
	}

	void testRejectsNonZeroParticleAlignment() {
		Common::Array<byte> particle;
		append32(particle, 0x12345678);
		Common::Array<byte> data;
		appendNode(data, 0xF0F000E0, particle, 0x40000000);
		data.push_back(0xA5);
		appendNode(data, 2, Common::Array<byte>());

		TS_ASSERT(!parse(data.data(), data.size()).ok());
	}

	void testAcceptsObservedParticleVariantAlignment() {
		Common::Array<byte> particle;
		append32(particle, 0x12345678);
		Common::Array<byte> data;
		appendNode(data, 0xF0F000E0, particle, 0x40000000);
		data.push_back(0xBD);
		data.push_back(0);
		appendNode(data, 2, Common::Array<byte>());

		TS_ASSERT(parse(data.data(), data.size()).ok());
	}

	void testRecoversParticleAfterObservedFesMarker() {
		Common::Array<byte> particle;
		append32(particle, 0x12345678);
		Common::Array<byte> data;
		appendNode(data, 0xF0F000E0, particle, 0x40000000);
		data.push_back(0xAF); data.push_back(0x9F); data.push_back(0x10); data.push_back(0x00);
		data.push_back(0x00); data.push_back(0x00);
		data.push_back(0xA5); data.push_back(0x01); data.push_back(0x00); data.push_back(0x00);
		append32(data, 0x1C020037);
		appendNode(data, 2, Common::Array<byte>());
		Common::RMD rmd = parse(data.data(), data.size());
		TS_ASSERT(rmd.ok());
	}

	void testRecoversParticleAfterObservedP4Marker() {
		Common::Array<byte> particle;
		append32(particle, 0x12345678);
		Common::Array<byte> data;
		appendNode(data, 0xF0F000E0, particle, 0x40000000);
		data.push_back(0x54); data.push_back(0x3C);
		appendNode(data, 2, Common::Array<byte>());
		TS_ASSERT(parse(data.data(), data.size()).ok());
	}

	void testRecoversFillBeforeTransformOverride() {
		Common::Array<byte> data;
		appendNode(data, 2, Common::Array<byte>());
		for (uint32 i = 0; i < 12; ++i)
			data.push_back(0);
		Common::Array<byte> transform(64, 0);
		appendNode(data, 0xF0F00005, transform, 0x40000000);
		appendNode(data, 2, Common::Array<byte>());
		TS_ASSERT(parse(data.data(), data.size()).ok());
	}

	void testRecoversWeaponTrailerSuffix() {
		Common::Array<byte> data;
		appendNode(data, 2, Common::Array<byte>());
		data.push_back(0); data.push_back(0); data.push_back(0); data.push_back(0);
		data.push_back(0); data.push_back(0); data.push_back(0); data.push_back(0x40);
		data.push_back(0); data.push_back(0); data.push_back(0);
		Common::Array<byte> transform(64, 0);
		appendNode(data, 0xF0F00005, transform, 0x40000000);
		appendNode(data, 2, Common::Array<byte>());
		TS_ASSERT(parse(data.data(), data.size()).ok());
	}

	void testDoesNotRecoverArbitraryBytesBeforeTransform() {
		Common::Array<byte> data;
		appendNode(data, 2, Common::Array<byte>());
		for (uint32 i = 0; i < 12; ++i)
			data.push_back((byte)(i + 1));
		Common::Array<byte> transform(64, 0);
		appendNode(data, 0xF0F00005, transform, 0x40000000);
		TS_ASSERT(!parse(data.data(), data.size()).ok());
	}

	void testTreatsOpaquePluginPayloadAsOpaque() {
		Common::Array<byte> pluginPayload;
		append32(pluginPayload, 0x00010000);
		append32(pluginPayload, 0x00060000);
		append32(pluginPayload, 0x1C020037);
		Common::Array<byte> extension;
		appendNode(extension, 0x110, pluginPayload);
		Common::Array<byte> data;
		appendNode(data, 3, extension);
		TS_ASSERT(parse(data.data(), data.size()).ok());
	}
};
