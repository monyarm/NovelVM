#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/formats/epl.h"
#include "graphics/epl_effect.h"

/**
 * EPL effect runtime tests on SYNTHETIC trees laid out like the real P3 FES/P4 ones.
 */
class EPLEffectTestSuite : public CxxTest::TestSuite {
	struct FixedTextures : public Graphics::EffectTextureSource {
		bool textureSize(const Graphics::EffectTextureRef &, int &w, int &h) override {
			w = h = 64;
			return true;
		}
	};

	static void put32(Common::Array<byte> &d, uint32 at, uint32 v) {
		for (int i = 0; i < 4; ++i)
			d[at + i] = (byte)(v >> (8 * i));
	}
	static void putFloat(Common::Array<byte> &d, uint32 at, float v) {
		uint32 bits;
		memcpy(&bits, &v, 4);
		put32(d, at, bits);
	}

	// One particle node: cone emitter (sub 5) that does not move its particles, 1 per frame,
	// lifetime 10, constant red colour, size 1, additive blend, texture slot 3. The node sits
	// at offset (0, 5, 0).
	// Root + one node + one record of the given kind/sub; parameters at return value + 0x11C.
	static Common::Array<byte> makeTree(uint16 kind, uint16 sub, uint32 paramSize) {
		const uint32 record = 0x90 + 0xC0;
		Common::Array<byte> d(record + 0x2C + paramSize, 0);
		putFloat(d, 0x74, 1.0f);
		put32(d, 0x68, 0x80);
		put32(d, 0x80, 1);
		put32(d, 0x88, 0x90);
		const uint32 n = 0x90;
		putFloat(d, n + 0x4C, 1);
		putFloat(d, n + 0x5C, 1);
		putFloat(d, n + 0x60, 1);
		put32(d, n + 0x64, 0xFFFFFFFF);
		put32(d, n + 0x68, 0x80);
		put32(d, n + 0x84, 1);
		put32(d, n + 0x90, record);
		put32(d, record, 210);
		d[record + 4] = (byte)kind;
		d[record + 0x0C] = (byte)sub;
		put32(d, record + 0x10, 0x2C);
		put32(d, record + 0x14, paramSize);
		return d;
	}

	static Common::Array<byte> makeParticleTree() {
		const uint32 record = 0x90 + 0xC0, params = record + 0x2C, paramSize = 0xE8;
		Common::Array<byte> d(params + paramSize + 0x10 + 4, 0);
		putFloat(d, 0x74, 1.0f);
		put32(d, 0x68, 0x80);
		put32(d, 0x80, 1);
		put32(d, 0x88, 0x90);
		const uint32 n = 0x90;
		putFloat(d, n + 0x44, 5.0f); putFloat(d, n + 0x4C, 1);
		putFloat(d, n + 0x5C, 1);
		putFloat(d, n + 0x60, 1);
		put32(d, n + 0x64, 0xFFFFFFFF);
		put32(d, n + 0x68, 0x80);
		put32(d, n + 0x84, 1);
		put32(d, n + 0x90, record);
		put32(d, record, 210);
		d[record + 4] = 1;          // kind: particle
		d[record + 0x0C] = 5;       // sub: cone
		put32(d, record + 0x10, 0x2C);
		put32(d, record + 0x14, paramSize);
		d[record + 0x1C] = 4;       // texture slot resource
		put32(d, record + 0x20, 0x2C + paramSize + 0x10);
		put32(d, record + 0x24, 4);
		put32(d, params + paramSize + 0x10, 3);
		put32(d, params + 0x24, 1);              // rate
		put32(d, params + 0x30, 0x000000FF);     // colour keys: mode 0, red to red
		put32(d, params + 0x34, 0x000000FF);
		put32(d, params + 0x50, 255);            // alpha
		d[params + 0x54] = 2;                    // blend: additive
		putFloat(d, params + 0x5C, 1.0f);        // fade-out fraction: no fade
		putFloat(d, params + 0x64, 1.0f);        // size keys: 1 -> 1
		putFloat(d, params + 0x68, 1.0f);
		put32(d, params + 0xB8, 10);             // lifetime
		return d;
	}

public:
	void testRandomMatchesGameAlgorithm() {
		Graphics::EffectRandom::seed(0x12345678);
		TS_ASSERT_EQUALS(Graphics::EffectRandom::next(), 0xE925A4B7u);
		TS_ASSERT_EQUALS(Graphics::EffectRandom::next(), 0x06D9B465u);
		TS_ASSERT_EQUALS(Graphics::EffectRandom::next(), 0x4CA72044u);
		TS_ASSERT_EQUALS(Graphics::EffectRandom::next(), 0x2AD4497Cu);
	}

	void testRotationIsRowVector() {
		// 90 degrees about z: x axis -> y axis under v * R(q).
		const float q[4] = {0, 0, sinf(0.7853982f), cosf(0.7853982f)}, x[3] = {1, 0, 0};
		float r[3];
		Graphics::EffectMath::rotate(q, x, r);
		TS_ASSERT_DELTA(r[0], 0.0f, 1e-5f);
		TS_ASSERT_DELTA(fabsf(r[1]), 1.0f, 1e-5f);
	}

	void testMatrixToPoseRemovesScale() {
		// RW rows right/up/at of a 90 degree turn about y, scaled by 2, translated by (1, 2, 3).
		const float m[16] = {0, 0, -2, 0, 0, 2, 0, 0, 2, 0, 0, 0, 1, 2, 3, 1};
		float position[3], q[4], r[3];
		Graphics::EffectMath::matrixToPose(m, position, q);
		TS_ASSERT_DELTA(position[2], 3.0f, 1e-5f);
		const float x[3] = {1, 0, 0};
		Graphics::EffectMath::rotate(q, x, r); // must give the normalised first row
		TS_ASSERT_DELTA(r[0], 0.0f, 1e-5f);
		TS_ASSERT_DELTA(r[2], -1.0f, 1e-5f);
		TS_ASSERT_DELTA(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0f, 1e-5f);
	}

	void testRingGrowsInLocalPlane() {
		// Ring (sub 1): radius 10 growing 2 per frame, widths 1/1/1, 4 segments, white, opaque.
		Common::Array<byte> d = makeTree(12, 1, 0x6C);
		const uint32 p = 0x90 + 0xC0 + 0x2C;
		put32(d, p + 0x04, 0x00FFFFFF);
		put32(d, p + 0x24, 255);
		d[p + 0x28] = 2;
		putFloat(d, p + 0x30, 1.0f);
		put32(d, p + 0x38, 4);
		putFloat(d, p + 0x40, 10.0f);
		put32(d, p + 0x44, 0xFFFFFFFF); put32(d, p + 0x48, 0xFFFFFFFF); put32(d, p + 0x4C, 0xFFFFFFFF);
		putFloat(d, p + 0x50, 1.0f); putFloat(d, p + 0x54, 1.0f); putFloat(d, p + 0x58, 1.0f);
		putFloat(d, p + 0x60, 2.0f);
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		Graphics::EPLEffect effect(epl, nullptr);
		Graphics::EffectCamera camera;
		for (int i = 0; i < 3; ++i) // ages 0, 1, 2: drawn at age 2, radius 14
			effect.advance(camera);
		Common::Array<Graphics::EffectBatch> batches;
		effect.draw(camera, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() != 1)
			return;
		TS_ASSERT_EQUALS(batches[0].blendMode, 2);
		TS_ASSERT_EQUALS(batches[0].vertices.size(), 4u * 18u);
		float maxX = 0, minR = 1e9f;
		for (uint i = 0; i < batches[0].vertices.size(); ++i) {
			const Graphics::EffectVertex &v = batches[0].vertices[i];
			TS_ASSERT_DELTA(v.y, 0.0f, 1e-4f); // node offset 0: flat in the XZ plane
			maxX = MAX(maxX, v.x);
			minR = MIN(minR, sqrtf(v.x * v.x + v.z * v.z));
		}
		TS_ASSERT_DELTA(minR, 14.0f, 1e-3f);
		TS_ASSERT_DELTA(maxX, 17.0f, 1e-3f);
	}

	void testBoardIsSizedByTexture() {
		// Board: size key 1.5 x multiplier 2 on a 64x64 texture -> half extent 2 * 3 = 6 in XZ.
		Common::Array<byte> d = makeTree(7, 1, 0x98 + 4);
		const uint32 r = 0x90 + 0xC0, p = r + 0x2C;
		d[r + 0x1C] = 4; // global texture slot
		put32(d, r + 0x20, 0x2C + 0x98);
		put32(d, r + 0x24, 4);
		put32(d, p + 0x98, 5);
		put32(d, p + 0x04, 0x00FFFFFF);
		put32(d, p + 0x24, 255);
		putFloat(d, p + 0x30, 1.0f);
		putFloat(d, p + 0x38, 1.5f);
		putFloat(d, p + 0x90, 2.0f);
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		FixedTextures textures;
		Graphics::EPLEffect effect(epl, &textures);
		Graphics::EffectCamera camera;
		effect.advance(camera);
		Common::Array<Graphics::EffectBatch> batches;
		effect.draw(camera, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() != 1)
			return;
		TS_ASSERT_EQUALS(batches[0].texture.globalSlot, 5);
		TS_ASSERT_EQUALS(batches[0].vertices.size(), 6u);
		float maxX = -1e9f, maxZ = -1e9f;
		for (uint i = 0; i < batches[0].vertices.size(); ++i) {
			maxX = MAX(maxX, batches[0].vertices[i].x);
			maxZ = MAX(maxZ, batches[0].vertices[i].z);
		}
		TS_ASSERT_DELTA(maxX, 6.0f, 1e-4f);
		TS_ASSERT_DELTA(maxZ, 6.0f, 1e-4f);
	}

	void testBounceParticlesStayAboveFloor() {
		// Bounce model (sub 8): falling particles never end a tick below the floor (y = 2).
		Common::Array<byte> d = makeTree(1, 8, 0xF8 + 4);
		const uint32 r = 0x90 + 0xC0, p = r + 0x2C;
		d[r + 0x1C] = 4;
		put32(d, r + 0x20, 0x2C + 0xF8);
		put32(d, r + 0x24, 4);
		put32(d, p + 0x24, 1);              // rate
		put32(d, p + 0x50, 255);
		putFloat(d, p + 0x5C, 1.0f);
		putFloat(d, p + 0x64, 1.0f);
		putFloat(d, p + 0x68, 1.0f);
		put32(d, p + 0xB8, 60);             // lifetime
		putFloat(d, p + 0xD0, 0.3f);        // cone
		putFloat(d, p + 0xD8, 2.0f);        // floor
		putFloat(d, p + 0xDC, 1.0f);        // speed
		putFloat(d, p + 0xE8, 0.5f);        // gravity
		putFloat(d, p + 0xEC, 0.5f);        // bounce
		putFloat(d, p + 0xF4, 0.8f);        // bounce decay
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		FixedTextures textures;
		Graphics::EPLEffect effect(epl, &textures);
		const float position[3] = {0, 20, 0}, identity[4] = {0, 0, 0, 1};
		effect.setPose(position, identity);
		Graphics::EffectCamera camera;
		float minY = 1e9f;
		for (int i = 0; i < 40; ++i) {
			effect.advance(camera);
			Common::Array<Graphics::EffectBatch> batches;
			effect.draw(camera, batches);
			for (uint b = 0; b < batches.size(); ++b)
				for (uint v = 0; v < batches[b].vertices.size(); v += 6) // first corner: centre - 1 in y
					minY = MIN(minY, batches[b].vertices[v].y + 1.0f);
		}
		TS_ASSERT_LESS_THAN(minY, 4.0f);              // they did reach the floor ...
		TS_ASSERT_LESS_THAN_EQUALS(2.0f - 1e-3f, minY); // ... and never sank through it
	}

	void testScreenEchoZoomsPreviousFrame() {
		// Kind 4 sub 1: zoom key 100 -> zoom 2, so the screen corners sample half a frame outside.
		Common::Array<byte> d = makeTree(4, 1, 0xE8);
		const uint32 p = 0x90 + 0xC0 + 0x2C;
		put32(d, p + 0x04, 0x00FFFFFF);
		put32(d, p + 0x24, 255);
		d[p + 0x28] = 2;
		putFloat(d, p + 0x30, 1.0f);
		putFloat(d, p + 0x38, 100.0f);
		put32(d, p + 0xB8, 10);
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		Graphics::EPLEffect effect(epl, nullptr);
		Graphics::EffectCamera camera;
		effect.advance(camera);
		Common::Array<Graphics::EffectBatch> batches;
		effect.draw(camera, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() != 1)
			return;
		const Graphics::EffectBatch &batch = batches[0];
		TS_ASSERT(batch.overlay && batch.screenSpace && batch.texture.previousFrame);
		TS_ASSERT_EQUALS(batch.vertices.size(), 6u);
		TS_ASSERT_DELTA(batch.vertices[0].x, 0.0f, 1e-6f); // top left corner ...
		TS_ASSERT_DELTA(batch.vertices[0].u, -0.5f, 1e-5f); // ... samples (-0.5, -0.5)
		TS_ASSERT_DELTA(batch.vertices[0].v, -0.5f, 1e-5f);
		for (int i = 0; i < 11; ++i) // past the duration nothing is drawn
			effect.advance(camera);
		batches.clear();
		effect.draw(camera, batches);
		TS_ASSERT(batches.empty());
	}

	void testParticlesSpawnAtNodeAndBillboard() {
		Common::Array<byte> d = makeParticleTree();
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		FixedTextures textures;
		Graphics::EPLEffect effect(epl, &textures);
		const float position[3] = {10, 0, 0}, identity[4] = {0, 0, 0, 1};
		effect.setPose(position, identity);
		Graphics::EffectCamera camera;
		camera.position[2] = -100;
		for (int i = 0; i < 3; ++i)
			effect.advance(camera);
		Common::Array<Graphics::EffectBatch> batches;
		effect.draw(camera, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() != 1)
			return;
		const Graphics::EffectBatch &batch = batches[0];
		TS_ASSERT_EQUALS(batch.blendMode, 2);
		TS_ASSERT_EQUALS(batch.texture.globalSlot, 3);
		TS_ASSERT_EQUALS(batch.vertices.size(), 18u); // one particle born per frame, three quads
		// 64x64 texture at size 1 -> 2x2 units, centred on pose + node offset = (10, 5, 0).
		float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
		for (uint i = 0; i < 6; ++i) {
			minX = MIN(minX, batch.vertices[i].x); maxX = MAX(maxX, batch.vertices[i].x);
			minY = MIN(minY, batch.vertices[i].y); maxY = MAX(maxY, batch.vertices[i].y);
		}
		TS_ASSERT_DELTA(minX, 9.0f, 1e-4f); TS_ASSERT_DELTA(maxX, 11.0f, 1e-4f);
		TS_ASSERT_DELTA(minY, 4.0f, 1e-4f); TS_ASSERT_DELTA(maxY, 6.0f, 1e-4f);
		TS_ASSERT_DELTA(batch.vertices[0].r, 1.0f, 1e-5f);
		TS_ASSERT_DELTA(batch.vertices[0].b, 0.0f, 1e-5f);
		TS_ASSERT_DELTA(batch.vertices[0].a, 1.0f, 1e-5f);

		// Restarting wipes all particles: one tick later only one is alive.
		effect.restart();
		effect.advance(camera);
		batches.clear();
		effect.draw(camera, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() == 1)
			TS_ASSERT_EQUALS(batches[0].vertices.size(), 6u);
	}
};
