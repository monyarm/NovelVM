#include <cxxtest/TestSuite.h>

#include "common/formats/gmo.h"
#include "common/memstream.h"
#include "common/util.h"

/**
 * Sony PSP GMO = a nested chunk container for 3D models. These tests use a
 * hand-crafted synthetic file (no game data): 16-byte "OMG.00.1PSP" header,
 * a FILE root chunk containing one MODEL whose children are one BONE (with
 * TRANSLATE/SCALE/PARENT_BONE), one PART (MESH with SET_MATERIAL and
 * DRAW_ARRAYS, plus an ARRAYS vertex array), one MATERIAL (DIFFUSE), one
 * TEXTURE (FILE_IMAGE) and one MOTION (FRAME_RATE). The byte layout matches
 * the real format (see common/formats/gmo.h) so the parser exercises the
 * same code paths as real P3P files.
 */
class GMOTestSuite : public CxxTest::TestSuite {
public:
	// ---- builders ----
	static void w8(byte *&p, byte v) { *p++ = v; }
	static void w16(byte *&p, uint16 v) {
		p[0] = v & 0xFF; p[1] = v >> 8; p += 2;
	}
	static void w32(byte *&p, uint32 v) {
		p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24; p += 4;
	}
	static void wf(byte *&p, float v) {
		uint32 u; memcpy(&u, &v, 4); w32(p, u);
	}
	static void wname(byte *&p, const char *s) {
		while (*s) *p++ = (byte)*s++;
	}
	// patch nextOffs at chunkStart+4
	static void patchNext(byte *chunkStart, byte *w) {
		uint32 next = (uint32)(w - chunkStart);
		chunkStart[4] = next & 0xFF;
		chunkStart[5] = (next >> 8) & 0xFF;
		chunkStart[6] = (next >> 16) & 0xFF;
		chunkStart[7] = (next >> 24) & 0xFF;
	}
	// full chunk header: type u16, argsOffs u16, next u32(patch),
	// child u32, data u32 (both = 16, right after the header)
	static byte *fullHeader(byte *&w, uint16 type, const char *name, uint32 &childOff, uint32 &dataOff) {
		byte *start = w;
		w16(w, type);
		uint32 nameLen = 0;
		if (name) nameLen = strlen(name);
		w16(w, (uint16)(16 + nameLen));       // argsOffs (points just past name)
		w32(w, 0);                            // nextOffs patched later
		childOff = 16 + nameLen;              // children right past the name
		w32(w, childOff);
		dataOff = childOff;
		w32(w, dataOff);
		if (name)
			wname(w, name);
		return start;
	}
	static void halfHeader(byte *&w, uint16 type, uint32 dataLen) {
		w16(w, (uint16)(type | 0x8000));
		w16(w, 0);
		w32(w, 8 + dataLen);                  // nextOffs = header + data
	}

	void testParseSynthetic() {
		byte arena[2048];
		byte *w = arena;

		// ---- 16-byte GMO file header ----
		w8(w, 'O'); w8(w, 'M'); w8(w, 'G'); w8(w, '.');
		w8(w, '0'); w8(w, '0'); w8(w, '.'); w8(w, '1');
		w8(w, 'P'); w8(w, 'S'); w8(w, 'P'); w8(w, 0);
		w8(w, 0); w8(w, 0); w8(w, 0); w8(w, 0);

		// ---- FILE root ----
		uint32 fChild, fData;
		byte *fileStart = fullHeader(w, 0x0002, nullptr, fChild, fData);

		// ---- MODEL ----
		uint32 mChild, mData;
		byte *modelStart = fullHeader(w, 0x0003, "m", mChild, mData);

		// BONE
		uint32 bChild, bData;
		byte *boneStart = fullHeader(w, 0x0004, "b", bChild, bData);
		halfHeader(w, 0x0048, 12); wf(w, 1.0f); wf(w, 2.0f); wf(w, 3.0f);   // TRANSLATE
		halfHeader(w, 0x004C, 12); wf(w, 2.0f); wf(w, 2.0f); wf(w, 2.0f);   // SCALE
		halfHeader(w, 0x0041, 4); w8(w, 2); w8(w, 0); w8(w, 0x04); w8(w, 0);// PARENT_BONE idx 2
		// BLIND_DATA "per3Helper": 12-byte name + u32 id + 9 floats
		halfHeader(w, 0x00F1, 12 + 4 + 36);
		wname(w, "per3Helper"); w8(w, 0); w8(w, 0);
		w32(w, 7);
		for (int i = 0; i < 9; i++) wf(w, 0.5f + i);
		patchNext(boneStart, w);

		// MODEL-level ARRAY_OFFSET: vertex variant (format = VERTEX_FLOAT)
		halfHeader(w, 0x0015, 4 + 24);
		w32(w, (3 << 7));
		wf(w, 10.0f); wf(w, 20.0f); wf(w, 30.0f);   // offset
		wf(w, 2.0f);  wf(w, 3.0f);  wf(w, 4.0f);    // scale
		// MODEL-level ARRAY_OFFSET: texture variant (format = TEXTURE_FLOAT)
		halfHeader(w, 0x0015, 4 + 16);
		w32(w, (3 << 0));
		wf(w, 0.25f); wf(w, 0.125f);                // offset
		wf(w, 0.5f);  wf(w, 0.5f);                  // scale

		// PART
		uint32 pChild, pData;
		byte *partStart = fullHeader(w, 0x0005, "p", pChild, pData);
		// MESH
		uint32 sChild, sData;
		byte *meshStart = fullHeader(w, 0x0006, "s", sChild, sData);
		// SET_MATERIAL -> material[0]
		halfHeader(w, 0x0061, 4); w8(w, 0); w8(w, 0x10); w8(w, 0x08); w8(w, 0);
		// DRAW_ARRAYS -> arrays[0], mode 0x104, 3 verts, 1 prim
		halfHeader(w, 0x0066, 20);
		w8(w, 0); w8(w, 0x10); w8(w, 0x07); w8(w, 0);
		w32(w, 0x104); w32(w, 3); w32(w, 1); w32(w, 0);
		patchNext(meshStart, w);
		// ARRAYS "a": float pos/normal/uv, 3 vertices
		uint32 aChild, aData;
		byte *arrStart = fullHeader(w, 0x0007, "a", aChild, aData);
		w32(w, (3 << 7) | (3 << 5) | (3 << 0)); // vertex/normal/tex = float
		w32(w, 3); w32(w, 0); w32(w, 0);
		for (int i = 0; i < 3; i++) {
			wf(w, 0.5f + i); wf(w, 0.25f);     // uv
			wf(w, 0.0f); wf(w, 0.0f); wf(w, 1.0f); // normal
			wf(w, (float)i); wf(w, 0.0f); wf(w, 0.0f); // position
		}
		patchNext(arrStart, w);
		patchNext(partStart, w);

		// MATERIAL "mat" with DIFFUSE, SPECULAR(+power), REFLECTION, BUMP
		// and a LAYER carrying the render-state chunks.
		uint32 tChild, tData;
		byte *matStart = fullHeader(w, 0x0008, "mat", tChild, tData);
		halfHeader(w, 0x0082, 16); wf(w, 0.9f); wf(w, 0.5f); wf(w, 0.2f); wf(w, 1.0f);
		halfHeader(w, 0x0083, 20); wf(w, 1.0f); wf(w, 1.0f); wf(w, 1.0f); wf(w, 1.0f); wf(w, 8.0f);
		halfHeader(w, 0x0086, 4); wf(w, 0.75f);  // REFLECTION
		halfHeader(w, 0x0088, 4); wf(w, 0.4f);   // BUMP
		{
			uint32 lChild, lData;
			byte *layerStart = fullHeader(w, 0x0009, "layer", lChild, lData);
			halfHeader(w, 0x0091, 4); w8(w, 0); w8(w, 0x10); w8(w, 0x0A); w8(w, 0); // SET_TEXTURE
			halfHeader(w, 0x0092, 4); w32(w, 3);        // MAP_TYPE
			halfHeader(w, 0x0093, 4); wf(w, 0.5f);      // MAP_FACTOR
			halfHeader(w, 0x0094, 12); w32(w, 1); w32(w, 2); w32(w, 3); // BLEND_FUNC
			halfHeader(w, 0x0095, 8);  w32(w, 4); w32(w, 5);            // TEX_FUNC
			halfHeader(w, 0x0096, 8);  w32(w, 6); w32(w, 7);            // TEX_FILTER
			patchNext(layerStart, w);
		}
		patchNext(matStart, w);

		// TEXTURE "tex" with FILE_IMAGE
		uint32 qChild, qData;
		byte *texStart = fullHeader(w, 0x000A, "tex", qChild, qData);
		halfHeader(w, 0x0013, 8); w32(w, 4); w32(w, 0x11223344); // u32 size + 4 bytes
		patchNext(texStart, w);

		// MOTION "mot" with FRAME_RATE + a 1-frame FCURVE
		uint32 oChild, oData;
		byte *motStart = fullHeader(w, 0x000B, "mot", oChild, oData);
		halfHeader(w, 0x00B2, 4); wf(w, 30.0f); // FRAME_RATE
		// ANIMATE: bone[0] TRANSLATE driven by fcurve[0].
		halfHeader(w, 0x00B3, 16);
		w8(w, 0); w8(w, 0x10); w8(w, 0x04); w8(w, 0); // block = BONE index 0
		w32(w, 0x48);  // TRANSLATE
		w32(w, 0);     // sub-index
		w8(w, 0); w8(w, 0); w8(w, 0x0C); w8(w, 0); // fcurve = FCURVE index 0
		uint32 fch, fda;
		byte *fcStart = fullHeader(w, 0x000C, "f", fch, fda);
		w32(w, 1);       // type: LINEAR (low nibble = interpolation mode)
		w32(w, 3);       // valueCount
		w32(w, 2);       // frameCount
		w32(w, 0);       // reserved
		wf(w, 0.0f); wf(w, 1.0f); wf(w, 2.0f); wf(w, 3.0f); // frame0: t + 3 vals
		wf(w, 1.0f); wf(w, 4.0f); wf(w, 5.0f); wf(w, 6.0f); // frame1
		patchNext(fcStart, w);
		patchNext(motStart, w);

		patchNext(modelStart, w);
		patchNext(fileStart, w);

		uint32 total = (uint32)(w - arena);
		TS_ASSERT(total < 2048);

		Common::MemoryReadStream s(arena, total, DisposeAfterUse::NO);
		Common::GMO g(&s);
		TS_ASSERT(g.ok());

		// bones
		TS_ASSERT_EQUALS(g.bones().size(), 1u);
		TS_ASSERT_EQUALS(g.bones()[0].name, "b");
		TS_ASSERT(g.bones()[0].hasTranslate);
		TS_ASSERT_DELTA(g.bones()[0].translate[0], 1.0f, 0.0001f);
		TS_ASSERT(g.bones()[0].hasScale);
		TS_ASSERT_DELTA(g.bones()[0].scale[0], 2.0f, 0.0001f);
		TS_ASSERT_EQUALS(g.bones()[0].parentIndex, 2);
		// BLIND_DATA "per3Helper" (name is NUL-padded to 12 bytes)
		TS_ASSERT(g.bones()[0].blindName.hasPrefix("per3Helper"));
		TS_ASSERT_EQUALS(g.bones()[0].blindId, 7u);
		TS_ASSERT_DELTA(g.bones()[0].blindData[8], 8.5f, 0.0001f);

		// model ARRAY_OFFSET (vertex + texture variants)
		float off[3], scale[3], toff[2], tscale[2];
		TS_ASSERT(g.getVertexOffset(off, scale));
		TS_ASSERT_DELTA(off[0], 10.0f, 0.0001f);
		TS_ASSERT_DELTA(scale[2], 4.0f, 0.0001f);
		TS_ASSERT(g.getTextureOffset(toff, tscale));
		TS_ASSERT_DELTA(toff[1], 0.125f, 0.0001f);
		TS_ASSERT_DELTA(tscale[0], 0.5f, 0.0001f);

		// parts / meshes / arrays
		TS_ASSERT_EQUALS(g.parts().size(), 1u);
		TS_ASSERT_EQUALS(g.parts()[0].name, "p");
		TS_ASSERT_EQUALS(g.parts()[0].meshes.size(), 1u);
		TS_ASSERT_EQUALS(g.parts()[0].meshes[0].name, "s");
		TS_ASSERT_EQUALS(g.parts()[0].meshes[0].material.index, 0);
		TS_ASSERT_EQUALS(g.parts()[0].meshes[0].draws.size(), 1u);
		TS_ASSERT_EQUALS(g.parts()[0].meshes[0].draws[0].vertexCount, 3u);
		TS_ASSERT_EQUALS(g.parts()[0].arrays.size(), 1u);
		TS_ASSERT_EQUALS(g.parts()[0].arrays[0].vertexCount, 3u);
		TS_ASSERT_EQUALS(g.parts()[0].arrays[0].positions.size(), 9u);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].positions[0], 0.0f, 0.0001f);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].positions[3], 1.0f, 0.0001f);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].positions[6], 2.0f, 0.0001f);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].texcoords[0], 0.5f, 0.0001f);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].texcoords[2], 1.5f, 0.0001f);
		TS_ASSERT_DELTA(g.parts()[0].arrays[0].normals[2], 1.0f, 0.0001f);

		// materials
		TS_ASSERT_EQUALS(g.materials().size(), 1u);
		TS_ASSERT_EQUALS(g.materials()[0].name, "mat");
		TS_ASSERT(g.materials()[0].hasDiffuse);
		TS_ASSERT_DELTA(g.materials()[0].diffuse[0], 0.9f, 0.0001f);
		TS_ASSERT(g.materials()[0].hasSpecular);
		TS_ASSERT_DELTA(g.materials()[0].specularPower, 8.0f, 0.0001f);
		TS_ASSERT_DELTA(g.materials()[0].reflection, 0.75f, 0.0001f);
		TS_ASSERT_DELTA(g.materials()[0].bump, 0.4f, 0.0001f);
		TS_ASSERT_EQUALS(g.materials()[0].layers.size(), 1u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].texture.index, 0);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].mapType, 3u);
		TS_ASSERT_DELTA(g.materials()[0].layers[0].mapFactor, 0.5f, 0.0001f);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].blendSrc, 2u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].blendDst, 3u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].texFunc, 4u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].texComp, 5u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].texFilterMag, 6u);
		TS_ASSERT_EQUALS(g.materials()[0].layers[0].texFilterMin, 7u);

		// textures
		TS_ASSERT_EQUALS(g.textures().size(), 1u);
		TS_ASSERT_EQUALS(g.textures()[0].name, "tex");
		TS_ASSERT_EQUALS(g.textures()[0].image.size(), 4u);

		// motions
		TS_ASSERT_EQUALS(g.motions().size(), 1u);
		TS_ASSERT_DELTA(g.motions()[0].frameRate, 30.0f, 0.0001f);
		TS_ASSERT_EQUALS(g.motions()[0].fcurves.size(), 1u);
		TS_ASSERT_EQUALS(g.motions()[0].fcurves[0].frameCount, 2u);
		TS_ASSERT_EQUALS(g.motions()[0].fcurves[0].times.size(), 2u);
		TS_ASSERT_EQUALS(g.motions()[0].fcurves[0].values.size(), 6u);
		TS_ASSERT_DELTA(g.motions()[0].fcurves[0].values[1], 2.0f, 0.0001f);

		// ANIMATE ref decoding + motion sampling (bone[0] TRANSLATE).
		TS_ASSERT_EQUALS(g.motions()[0].animates.size(), 1u);
		TS_ASSERT_EQUALS(g.motions()[0].animates[0].block.type, (uint16)Common::GMO::kBone);
		TS_ASSERT_EQUALS(g.motions()[0].animates[0].block.index, 0);
		TS_ASSERT_EQUALS(g.motions()[0].animates[0].type, (uint32)Common::GMO::kTranslate);
		TS_ASSERT_EQUALS(g.motions()[0].animates[0].fcurve.index, 0);
		{
			Common::GMO::BonePose bp;
			Common::GMO::MaterialPose mp;
			TS_ASSERT(g.evaluateMotion(0, 0.5f, bp, mp));
			TS_ASSERT(bp.hasTranslate[0]);
			TS_ASSERT_DELTA(bp.translate[0], 2.5f, 0.0001f); // lerp(1,4)
			TS_ASSERT_DELTA(bp.translate[1], 3.5f, 0.0001f); // lerp(2,5)
			TS_ASSERT_DELTA(bp.translate[2], 4.5f, 0.0001f); // lerp(3,6)
		}
	}

	// Vertex components follow the PSP GE layout: each aligned to its own size, the vertex padded
	// to the largest alignment. 8-bit UV (2) + pad + RGBA8888 colour (4) + 8-bit normal (3) +
	// pad + 16-bit position (6) + pad = 20 bytes. Real P3P field objects use the colour field
	// (format 0x0c0010ba), which an earlier 2-bit colour mask skipped, shifting every vertex.
	void testVertexComponentsAreAlignedLikeTheGE() {
		byte arena[512];
		byte *w = arena;
		w8(w, 'O'); w8(w, 'M'); w8(w, 'G'); w8(w, '.');
		w8(w, '0'); w8(w, '0'); w8(w, '.'); w8(w, '1');
		w8(w, 'P'); w8(w, 'S'); w8(w, 'P'); w8(w, 0);
		w8(w, 0); w8(w, 0); w8(w, 0); w8(w, 0);
		uint32 c, d;
		byte *fileStart = fullHeader(w, 0x0002, nullptr, c, d);
		byte *modelStart = fullHeader(w, 0x0003, "m", c, d);
		byte *partStart = fullHeader(w, 0x0005, "p", c, d);
		byte *arrStart = fullHeader(w, 0x0007, "a", c, d);
		w32(w, (1 << 0) | (7 << 2) | (1 << 5) | (2 << 7));
		w32(w, 2); w32(w, 0); w32(w, 0);
		for (int v = 0; v < 2; v++) {
			w8(w, 64); w8(w, 32);                                   // uv 0.5, 0.25
			w8(w, 0); w8(w, 0);                                     // pad to 4
			w8(w, 0x10 * (v + 1)); w8(w, 0x20); w8(w, 0x30); w8(w, 0xFF); // colour
			w8(w, 0); w8(w, 0x7F); w8(w, 0);                        // normal
			w8(w, 0);                                               // pad to 2
			w16(w, (uint16)(0x4000 * v)); w16(w, 0xC000); w16(w, 0x2000); // position
			w8(w, 0); w8(w, 0);                                     // pad stride to 4
		}
		patchNext(arrStart, w);
		patchNext(partStart, w);
		patchNext(modelStart, w);
		patchNext(fileStart, w);

		Common::MemoryReadStream stream(arena, (uint32)(w - arena), DisposeAfterUse::NO);
		Common::GMO gmo(&stream);
		TS_ASSERT_EQUALS(gmo.parts().size(), 1u);
		if (gmo.parts().size() != 1 || gmo.parts()[0].arrays.size() != 1)
			return;
		const Common::GMO::Arrays &arr = gmo.parts()[0].arrays[0];
		TS_ASSERT_EQUALS(arr.vertexCount, 2u);
		TS_ASSERT_EQUALS(arr.colors.size(), 8u);
		TS_ASSERT_EQUALS(arr.positions.size(), 6u);
		if (arr.colors.size() != 8 || arr.positions.size() != 6)
			return;
		TS_ASSERT_DELTA(arr.texcoords[2], 0.5f, 1e-6f);
		TS_ASSERT_DELTA(arr.colors[4], 0x20 / 255.0f, 1e-6f); // second vertex red
		TS_ASSERT_DELTA(arr.normals[4], 127 / 128.0f, 1e-6f);
		TS_ASSERT_DELTA(arr.positions[3], 0.5f, 1e-6f);       // second vertex x
		TS_ASSERT_DELTA(arr.positions[4], -0.5f, 1e-6f);
		TS_ASSERT_DELTA(arr.positions[5], 0.25f, 1e-6f);
	}

	void testRejectsBadMagic() {
		byte buf[32] = {0};
		buf[0] = 'X'; buf[1] = 'M'; buf[2] = 'G'; buf[3] = '.';
		Common::MemoryReadStream s(buf, sizeof(buf), DisposeAfterUse::NO);
		Common::GMO g(&s);
		TS_ASSERT(!g.ok());
	}

	void testRejectsTruncated() {
		byte buf[8] = {0};
		Common::MemoryReadStream s(buf, sizeof(buf), DisposeAfterUse::NO);
		Common::GMO g(&s);
		TS_ASSERT(!g.ok());
	}
};
