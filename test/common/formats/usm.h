#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "common/rational.h"
#include "common/system.h"
#include "graphics/pixelformat.h"
#include "video/usm_decoder.h"

#include "../../system/null_osystem.h"


/**
 * Synthetic USM (Sofdec2) fixtures, no game data. The file is built in the
 * same chunk layout the decoder reads: a "CRID" marker, then per-stream
 * @UTF metadata chunks whose 8-byte sub-header carries payload_offset=8,
 * padding=0, the stream index and payload_type=1 (metadata). All metadata
 * columns are constant-storage numerics, which common/formats/utf_table.cpp
 * reads inline from the schema.
 *
 * These exercise the decoder's leading-metadata scan only; no VP9 frame is
 * ever decoded (there is no real sample and no encoder here).
 */
class USMDecoderTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// Regression: the video track used to default to CLUT8 (1 byte/pixel)
	// while decodeFrame() writes a 2/4-byte RGB surface, overflowing the
	// heap if a caller never called setOutputPixelFormat() first. It must
	// now default to a real YUV-convertible RGB format.
	void testDefaultPixelFormatIsConvertible() {
#ifdef USE_VPX
		Common::Array<byte> file = buildUSM(true, true);
		Video::USMDecoder decoder;
		TS_ASSERT(decoder.loadStream(makeStream(file)));
		TS_ASSERT_EQUALS(decoder.getWidth(), 320);
		TS_ASSERT_EQUALS(decoder.getHeight(), 240);

		Graphics::PixelFormat fmt = decoder.getPixelFormat();
		TS_ASSERT(fmt.bytesPerPixel == 2 || fmt.bytesPerPixel == 4);
		TS_ASSERT(fmt.bytesPerPixel != 1);
#endif
	}

	// @ALP/@SBT metadata is recorded during the same leading scan that
	// reads @SFV/@SFA; the alpha @UTF width/height/framerate fields are
	// kept, and absence is reported as false.
	void testAlphaAndSubtitlePresence() {
#ifdef USE_VPX
		Common::Array<byte> withStreams = buildUSM(true, true);
		Video::USMDecoder with;
		TS_ASSERT(with.loadStream(makeStream(withStreams)));
		TS_ASSERT(with.hasAlphaStream());
		TS_ASSERT(with.hasSubtitleStream());
		TS_ASSERT_EQUALS(with.getAlphaWidth(), 320);
		TS_ASSERT_EQUALS(with.getAlphaHeight(), 240);
		TS_ASSERT(with.getAlphaFrameRate() == Common::Rational(30, 1));

		Common::Array<byte> withoutStreams = buildUSM(false, false);
		Video::USMDecoder without;
		TS_ASSERT(without.loadStream(makeStream(withoutStreams)));
		TS_ASSERT(!without.hasAlphaStream());
		TS_ASSERT(!without.hasSubtitleStream());
#endif
	}

	// mpeg_codec 5 (H.264) must be accepted rather than rejected on the
	// codec value alone. The synthetic file carries metadata only, so no
	// @SFV payload is decoded and no frame is produced.
	void testAcceptsH264Codec() {
#ifdef USE_VPX
#ifdef USE_OPENH264
		Common::Array<byte> file = buildUSM(true, true, 5);
		Video::USMDecoder decoder;
		TS_ASSERT(decoder.loadStream(makeStream(file)));
		TS_ASSERT_EQUALS(decoder.getWidth(), 320);
		TS_ASSERT_EQUALS(decoder.getHeight(), 240);
#endif
#endif
	}

	// mpeg_codec 1 (MPEG-1) must likewise be accepted.
	void testAcceptsMPEG1Codec() {
#ifdef USE_VPX
#ifdef USE_MPEG2
		Common::Array<byte> file = buildUSM(true, true, 1);
		Video::USMDecoder decoder;
		TS_ASSERT(decoder.loadStream(makeStream(file)));
		TS_ASSERT_EQUALS(decoder.getWidth(), 320);
		TS_ASSERT_EQUALS(decoder.getHeight(), 240);
#endif
#endif
	}

	// An unknown mpeg_codec is still rejected.
	void testRejectsUnknownCodec() {
#ifdef USE_VPX
		Common::Array<byte> file = buildUSM(true, true, 42);
		Video::USMDecoder decoder;
		TS_ASSERT(!decoder.loadStream(makeStream(file)));
#endif
	}

private:
	struct Col {
		const char *name;
		byte type; // 2 = uint16, 4 = uint32 (see UTFTable)
		uint32 value;
	};

	static Col col(const char *name, byte type, uint32 value) {
		Col c;
		c.name = name;
		c.type = type;
		c.value = value;
		return c;
	}

	static uint32 typeSize(byte type) {
		switch (type) {
		case 2: return 2;
		case 4: return 4;
		default: return 0;
		}
	}

	static void put16be(byte *p, uint16 v) {
		p[0] = (byte)(v >> 8);
		p[1] = (byte)v;
	}

	static void put32be(byte *p, uint32 v) {
		p[0] = (byte)(v >> 24);
		p[1] = (byte)(v >> 16);
		p[2] = (byte)(v >> 8);
		p[3] = (byte)v;
	}

	static void appendChunk(Common::Array<byte> &out, const char *tag, byte payloadType, const Common::Array<byte> &payload) {
		for (int i = 0; i < 4; i++)
			out.push_back((byte)tag[i]);

		byte sizeBuf[4];
		put32be(sizeBuf, 8 + (uint32)payload.size()); // 8-byte sub-header + payload
		for (int i = 0; i < 4; i++)
			out.push_back(sizeBuf[i]);

		out.push_back(0); // unknown
		out.push_back(8); // payload offset, from right after tag+size
		out.push_back(0); // padding size (u16BE)
		out.push_back(0);
		out.push_back(0); // stream index
		out.push_back(0); // unknown
		out.push_back(0); // unknown
		out.push_back(payloadType);

		for (uint32 i = 0; i < payload.size(); i++)
			out.push_back(payload[i]);
	}

	// One-row @UTF table of constant-storage numeric columns. rowLength is
	// zero and no row/data segments are emitted; UTFTable::get() reads
	// constant columns straight from the schema.
	static Common::Array<byte> buildUTF(const Common::Array<Col> &cols) {
		Common::Array<uint32> nameOff(cols.size());
		Common::Array<byte> nameStrings;
		for (uint32 i = 0; i < cols.size(); i++) {
			nameOff[i] = nameStrings.size();
			for (const char *p = cols[i].name; *p; p++)
				nameStrings.push_back((byte)*p);
			nameStrings.push_back(0);
		}

		uint32 schemaSize = 0;
		for (uint32 i = 0; i < cols.size(); i++)
			schemaSize += 1 + 4 + typeSize(cols[i].type);

		uint32 stringsAbs = 32 + schemaSize;
		uint32 dataAbs = stringsAbs + nameStrings.size();
		Common::Array<byte> out(dataAbs, (byte)0);

		memcpy(out.data(), "@UTF", 4);
		put32be(out.data() + 4, dataAbs);
		put32be(out.data() + 8, stringsAbs - 8);  // unique/rows offset
		put32be(out.data() + 12, stringsAbs - 8); // strings offset
		put32be(out.data() + 16, dataAbs - 8);    // data offset
		put32be(out.data() + 20, 0);              // table name
		put16be(out.data() + 24, (uint16)cols.size());
		put16be(out.data() + 26, 0); // row length
		put32be(out.data() + 28, 1); // one row

		uint32 off = 32;
		for (uint32 i = 0; i < cols.size(); i++) {
			out[off++] = (byte)(0x30 | cols[i].type); // constant storage
			put32be(out.data() + off, nameOff[i]);
			off += 4;
			if (cols[i].type == 2) {
				put16be(out.data() + off, (uint16)cols[i].value);
				off += 2;
			} else {
				put32be(out.data() + off, cols[i].value);
				off += 4;
			}
		}

		memcpy(out.data() + stringsAbs, nameStrings.data(), nameStrings.size());
		return out;
	}

	static Common::Array<byte> buildUSM(bool withAlpha, bool withSubtitle, uint32 videoCodec = 9) {
		Common::Array<byte> out;
		appendChunk(out, "CRID", 0, Common::Array<byte>());

		Common::Array<Col> video;
		video.push_back(col("width", 2, 320));
		video.push_back(col("height", 2, 240));
		video.push_back(col("mpeg_codec", 4, videoCodec)); // 9 = VP9, 5 = H264, 1 = MPEG1
		video.push_back(col("framerate_n", 4, 30));
		video.push_back(col("framerate_d", 4, 1));
		appendChunk(out, "@SFV", 1, buildUTF(video));

		if (withAlpha) {
			Common::Array<Col> alpha;
			alpha.push_back(col("width", 2, 320));
			alpha.push_back(col("height", 2, 240));
			alpha.push_back(col("framerate_n", 4, 30));
			alpha.push_back(col("framerate_d", 4, 1));
			appendChunk(out, "@ALP", 1, buildUTF(alpha));
		}

		if (withSubtitle)
			appendChunk(out, "@SBT", 1, buildUTF(Common::Array<Col>()));

		return out;
	}

	// loadStream() takes ownership of the stream.
	static Common::SeekableReadStream *makeStream(const Common::Array<byte> &file) {
		byte *copy = new byte[file.size()];
		memcpy(copy, file.data(), file.size());
		return new Common::MemoryReadStream(copy, file.size(), DisposeAfterUse::YES);
	}
};

