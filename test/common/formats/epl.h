#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/formats/epl.h"

/**
 * EPL effect tree tests. Fixtures are SYNTHETIC, laid out like the trees in real
 * P3 FES/P4 RMD 0xF0F000E0 chunks: root 0x90, nodes 0xC0, records 0x2C + blocks.
 */
class EPLTestSuite : public CxxTest::TestSuite {
	static void put32(Common::Array<byte> &d, uint32 at, uint32 v) {
		for (int i = 0; i < 4; ++i)
			d[at + i] = (byte)(v >> (8 * i));
	}
	static void put16(Common::Array<byte> &d, uint32 at, uint16 v) {
		d[at] = (byte)v;
		d[at + 1] = (byte)(v >> 8);
	}
	static void putFloat(Common::Array<byte> &d, uint32 at, float v) {
		uint32 bits;
		memcpy(&bits, &v, 4);
		put32(d, at, bits);
	}

	// Three nodes: 0 = kind 1 sub 3 with an 8-byte parameter block and a texture-slot
	// resource; 1 = clone of node 0; 2 = kind 12 sharing node 0's resource.
	static Common::Array<byte> makeTree() {
		const uint32 records = 0x90 + 3 * 0xC0;
		Common::Array<byte> d(records + 0x50 + 0x40, 0);
		put32(d, 0x68, 0x80);
		putFloat(d, 0x74, 2.0f);       // root multiplier
		put32(d, 0x80, 3);             // count
		put32(d, 0x88, 0x90);          // node array
		for (uint32 i = 0; i < 3; ++i) {
			const uint32 n = 0x90 + i * 0xC0;
			putFloat(d, n + 0x40, 0); putFloat(d, n + 0x44, 5.0f + i); putFloat(d, n + 0x48, 0); putFloat(d, n + 0x4C, 1);
			putFloat(d, n + 0x5C, 1);  // orientation w
			putFloat(d, n + 0x60, 1);  // scale
			put32(d, n + 0x64, 0xFF00A6FA);
			put32(d, n + 0x68, i == 2 ? 0xA0 : 0x80);
			put32(d, n + 0x80, i);     // startFrame
			put32(d, n + 0x84, 1);
			memcpy(&d[n + 0x9C], i == 2 ? "R-CRCLE" : "P-SPIRL", 7);
		}
		// node 0 -> record A at `records`
		put32(d, 0x90 + 0x90, records);
		put32(d, records + 0x00, 210);
		put16(d, records + 0x04, 1);
		put16(d, records + 0x0C, 3);
		put32(d, records + 0x10, 0x2C);
		put32(d, records + 0x14, 8);
		put16(d, records + 0x1C, 4);
		put32(d, records + 0x20, 0x40); // resource 0x40 bytes after the record
		put32(d, records + 0x24, 4);
		for (uint32 k = 0; k < 8; ++k)
			d[records + 0x2C + k] = (byte)(0x10 + k);
		put32(d, records + 0x40, 18);   // texture slot
		// node 1 -> clone of node 0
		put32(d, 0x90 + 0xC0 + 0x90, 0);
		put32(d, 0x90 + 0xC0 + 0x98, 1);
		// node 2 -> record B, sharing node 0's resource
		const uint32 b = records + 0x50;
		put32(d, 0x90 + 2 * 0xC0 + 0x90, b);
		put32(d, 0x90 + 2 * 0xC0 + 0x94, 0);
		put32(d, 0x90 + 2 * 0xC0 + 0x98, 2);
		put32(d, b + 0x00, 210);
		put16(d, b + 0x04, 12);
		put16(d, b + 0x0C, 3);
		put32(d, b + 0x10, 0x2C);
		put32(d, b + 0x14, 4);
		put16(d, b + 0x1C, 4);
		return d;
	}

public:
	void testParsesTree() {
		Common::Array<byte> d = makeTree();
		Common::EPL epl(d.data(), d.size());
		TS_ASSERT(epl.ok());
		TS_ASSERT_EQUALS(epl.root().multiplier, 2.0f);
		TS_ASSERT_EQUALS(epl.nodes().size(), 3u);
		if (epl.nodes().size() != 3)
			return;
		const Common::EPL::Node &n0 = epl.nodes()[0];
		TS_ASSERT_EQUALS(n0.name, "P-SPIRL");
		TS_ASSERT_EQUALS(n0.state.offset[1], 5.0f);
		TS_ASSERT_EQUALS(n0.state.color, 0xFF00A6FAu);
		TS_ASSERT_EQUALS(n0.record.kind, (uint16)Common::EPL::kParticle);
		TS_ASSERT_EQUALS(n0.record.sub, 3);
		TS_ASSERT_EQUALS(n0.record.parameters.size(), 8u);
		TS_ASSERT_EQUALS(n0.record.parameters[7], 0x17);
		TS_ASSERT_EQUALS(n0.record.resourceKind, (uint16)Common::EPL::kResourceTextureSlot);
		TS_ASSERT_EQUALS(n0.record.resource.size(), 4u);
		TS_ASSERT_EQUALS(n0.record.resource[0], 18);

		const Common::EPL::Node &n1 = epl.nodes()[1];
		TS_ASSERT_EQUALS(n1.cloneOf, 0);
		TS_ASSERT_EQUALS(n1.startFrame, 1);
		TS_ASSERT_EQUALS(n1.record.kind, (uint16)Common::EPL::kParticle);
		TS_ASSERT_EQUALS(n1.record.parameters.size(), 8u);

		const Common::EPL::Node &n2 = epl.nodes()[2];
		TS_ASSERT_EQUALS(n2.record.kind, (uint16)Common::EPL::kRing);
		TS_ASSERT_EQUALS(n2.state.flags, 0xA0u);
		TS_ASSERT_EQUALS(n2.record.resource.size(), 4u); // shared from node 0
		TS_ASSERT_EQUALS(n2.record.resource[0], 18);
	}

	void testRejectsMalformedTrees() {
		Common::Array<byte> badVersion = makeTree();
		put32(badVersion, 0x90 + 3 * 0xC0, 211);
		TS_ASSERT(!Common::EPL(badVersion.data(), badVersion.size()).ok());

		Common::Array<byte> forwardClone = makeTree();
		put32(forwardClone, 0x90 + 0xC0 + 0x90, 2); // clone of a later node
		TS_ASSERT(!Common::EPL(forwardClone.data(), forwardClone.size()).ok());

		Common::Array<byte> linked = makeTree();
		put32(linked, 0x8C, 0x1234); // runtime (linked) tree, not a file image
		TS_ASSERT(!Common::EPL(linked.data(), linked.size()).ok());

		Common::Array<byte> truncated = makeTree();
		truncated.resize(0x90 + 2 * 0xC0);
		TS_ASSERT(!Common::EPL(truncated.data(), truncated.size()).ok());
	}
};
