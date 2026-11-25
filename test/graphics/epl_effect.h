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
