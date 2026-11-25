#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "graphics/afpack_trail.h"

/**
 * AfPack weapon-trail tests on a SYNTHETIC pack laid out like the P4 ones (v0x65, the corpus'
 * parameter record).
 */
class AfPackTrailTestSuite : public CxxTest::TestSuite {
	static void put16(Common::Array<byte> &d, uint16 v) { d.push_back(v & 0xFF); d.push_back(v >> 8); }
	static void put32(Common::Array<byte> &d, uint32 v) { put16(d, v & 0xFFFF); put16(d, v >> 16); }
	static void putFloat(Common::Array<byte> &d, float v) {
		uint32 bits;
		memcpy(&bits, &v, 4);
		put32(d, bits);
	}

	// One mode-1 section between helpers 100 and 101: on; wait 3; off; end.
	static Common::Array<byte> makePack() {
		Common::Array<byte> d;
		put32(d, 0x65); put32(d, 1); put16(d, 1); put16(d, 1); put16(d, 1); put16(d, 0);
		put16(d, 0); put16(d, 0);
		const float param[] = {0.8f, 1.0f, 0.8f, 1.0f, 0.1f, 1.0f, 0.5f, 0.3f, 60, 0.3f, 15, 1, 0};
		for (float f : param)
			putFloat(d, f);
		put16(d, 0); put16(d, 0); put16(d, 0); put16(d, 0); put32(d, 0);
		const byte colors[16] = {100, 100, 150, 150, 255, 255, 255, 0, 100, 100, 150, 200, 255, 255, 255, 0};
		for (byte b : colors)
			d.push_back(b);
		const uint16 stream[] = {0x0100, 0x0001, 3, 0, 0x0101, 0x0000};
		put16(d, 0); put16(d, 0); put16(d, 0); put16(d, 0);
		put32(d, 100); put32(d, 101);
		put16(d, 6); put16(d, 4); put16(d, 1); put16(d, 0);
		putFloat(d, 10.0f);
		put32(d, sizeof(stream));
		for (uint16 h : stream)
			put16(d, h);
		return d;
	}

	// The blade (edge A at the origin side, B 10 units out) swings about the y axis.
	static bool swing(void *context, uint32 helper, float position[3]) {
		const float angle = *(const int *)context * 0.3f, radius = helper == 100 ? 2.0f : 12.0f;
		position[0] = cosf(angle) * radius;
		position[1] = 0;
		position[2] = sinf(angle) * radius;
		return true;
	}

public:
	void testRibbonFollowsTheStream() {
		Common::Array<byte> d = makePack();
		Graphics::AfPackTrail trail(d.data(), d.size());
		TS_ASSERT(trail.ok());
		int tick = 0;
		Common::Array<Graphics::EffectBatch> batches;
		trail.update(swing, &tick); // run 1: first sample only
		trail.draw(0xFFFFFFFF, batches);
		TS_ASSERT(batches.empty());
		++tick;
		trail.update(swing, &tick); // run 2: two samples, one segment
		trail.draw(0xFFFFFFFF, batches);
		TS_ASSERT_EQUALS(batches.size(), 1u);
		if (batches.size() == 1) {
			TS_ASSERT_EQUALS(batches[0].blendMode, 2);
			TS_ASSERT_EQUALS(batches[0].vertices.size(), 3u * (4 * 4 + 4)); // 4n + 4 triangles
			// The newest row sits on the current blade; the alpha fades to 0 at the oldest end.
			const Graphics::EffectVertex &first = batches[0].vertices[0];
			TS_ASSERT_DELTA(first.x, cosf(0.3f) * 2, 1e-4f);
			TS_ASSERT_DELTA(first.a, 150 / 255.0f, 1e-3f);
			float minAlpha = 1;
			for (uint i = 0; i < batches[0].vertices.size(); ++i)
				minAlpha = MIN(minAlpha, batches[0].vertices[i].a);
			TS_ASSERT_DELTA(minAlpha, 0.0f, 1e-5f);
		}
		TS_ASSERT(!trail.finished());
		++tick;
		trail.update(swing, &tick); // run 3: the wait passes, recording stops, the stream ends
		TS_ASSERT(trail.finished());
		batches.clear();
		trail.draw(0xFFFFFFFF, batches);
		TS_ASSERT(batches.empty());
	}

	void testRejectsUnknownVersion() {
		Common::Array<byte> d = makePack();
		d[0] = 0x66;
		Graphics::AfPackTrail trail(d.data(), d.size());
		TS_ASSERT(!trail.ok());
	}
};
