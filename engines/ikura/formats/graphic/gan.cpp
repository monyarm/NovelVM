#include "ikura/formats/graphic/gan.h"

namespace Ikura::Format::Graphic {

namespace {

const int kWidth = 640;
const int kHeight = 480;
const uint32 kDstLen = (uint32)kWidth * kHeight * 3;
const uint32 kFrameTableOffset = 0x2010;
const uint32 kFrameHeaderSize = 16;
const uint32 kMaxFrames = 10000; // sanity cap against a malicious frame count

// Mirrors the reference's repeated extended-length encoding (used for
// both the RLE run length and the color-repeat count): a byte, where the
// high bit means "read one more byte for a 15-bit value", and a decoded
// value of exactly 0x7FFF in turn means "read a 32-bit value instead".
// Returns false (and leaves pos unmoved past what it could safely read)
// if the buffer runs out partway through.
bool readVarLen(const Common::Array<byte> &buf, uint32 &pos, uint32 len, uint32 &out) {
	if (pos >= len)
		return false;
	uint32 v = buf[pos++];
	if (v & 0x80) {
		if (pos >= len)
			return false;
		v = ((v & 0x7F) << 8) | buf[pos++];
		if (v == 0x7FFF) {
			if (pos + 4 > len)
				return false;
			v = buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16) | (buf[pos + 3] << 24);
			pos += 4;
		}
	}
	out = v;
	return true;
}

// Branch for ref==0: a standalone frame, no dependency on the previous
// one. On any malformed read/write it just stops (rest of dstBuf stays
// whatever it already was, i.e. black - see gan.h for why).
void decodeSolidFrame(const Common::Array<byte> &srcBuf, uint32 srcLen, Common::Array<byte> &dstBuf) {
	uint32 dstLen = dstBuf.size();
	uint32 ipos = 0, opos = 0;
	uint32 last = 0xFFFFFFFF;
	while (ipos < srcLen && opos < dstLen) {
		if (ipos + 3 > srcLen || opos + 3 > dstLen)
			break;
		uint32 color = srcBuf[ipos] | (srcBuf[ipos + 1] << 8) | (srcBuf[ipos + 2] << 16);
		dstBuf[opos] = srcBuf[ipos];
		dstBuf[opos + 1] = srcBuf[ipos + 1];
		dstBuf[opos + 2] = srcBuf[ipos + 2];
		ipos += 3;
		opos += 3;
		if (color == last) {
			uint32 cnt;
			if (!readVarLen(srcBuf, ipos, srcLen, cnt))
				break;
			if (cnt > 2) {
				cnt -= 2;
				while (cnt-- > 0) {
					if (opos + 3 > dstLen || opos < 3)
						break;
					dstBuf[opos] = dstBuf[opos - 3];
					dstBuf[opos + 1] = dstBuf[opos - 2];
					dstBuf[opos + 2] = dstBuf[opos - 1];
					opos += 3;
				}
			}
		}
		last = color;
	}
}

// Branch for ref!=0: an RLE delta against lastFrame (unchanged runs are
// copied straight from it instead of re-encoded).
void decodeDiffFrame(const Common::Array<byte> &srcBuf, uint32 srcLen, Common::Array<byte> &dstBuf, const Common::Array<byte> &lastFrame) {
	uint32 dstLen = dstBuf.size();
	if (srcLen < 4)
		return;
	uint32 hdrlen = srcBuf[0] | (srcBuf[1] << 8) | (srcBuf[2] << 16) | (srcBuf[3] << 24);
	uint32 hpos = 4;
	uint32 ipos = hdrlen;
	uint32 opos = 0;
	uint32 last = 0xFFFFFFFF;
	uint32 cnt = 0; // persists across outer-loop iterations, like the reference

	while (ipos < srcLen && opos < dstLen) {
		uint32 length;
		if (!readVarLen(srcBuf, hpos, srcLen, length))
			break;

		bool truncated = false;
		while (length-- > 0) {
			if (cnt == 0) {
				if (ipos + 3 > srcLen || opos + 3 > dstLen) {
					truncated = true;
					break;
				}
				uint32 color = srcBuf[ipos] | (srcBuf[ipos + 1] << 8) | (srcBuf[ipos + 2] << 16);
				dstBuf[opos] = srcBuf[ipos];
				dstBuf[opos + 1] = srcBuf[ipos + 1];
				dstBuf[opos + 2] = srcBuf[ipos + 2];
				ipos += 3;
				opos += 3;
				if (last == color) {
					if (!readVarLen(srcBuf, ipos, srcLen, cnt)) {
						truncated = true;
						break;
					}
					cnt = (cnt > 2) ? cnt - 2 : 0;
				}
				last = color;
			} else {
				if (opos + 3 > dstLen) {
					truncated = true;
					break;
				}
				cnt--;
				dstBuf[opos] = last & 0xFF;
				dstBuf[opos + 1] = (last >> 8) & 0xFF;
				dstBuf[opos + 2] = (last >> 16) & 0xFF;
				opos += 3;
			}
		}
		if (truncated)
			break;

		uint32 copyLen;
		if (!readVarLen(srcBuf, hpos, srcLen, copyLen))
			break;
		while (copyLen-- > 0) {
			if (opos + 3 > dstLen || opos + 3 > lastFrame.size())
				break;
			dstBuf[opos] = lastFrame[opos];
			dstBuf[opos + 1] = lastFrame[opos + 1];
			dstBuf[opos + 2] = lastFrame[opos + 2];
			opos += 3;
		}
	}
}

Graphics::Surface *finishFrame(const Common::Array<byte> &dstBuf) {
	Graphics::Surface *result = new Graphics::Surface();
	result->create(kWidth, kHeight, Graphics::PixelFormat::createFormatRGBA32());
	uint32 p = 0;
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < kWidth; x++) {
			byte r = dstBuf[p], g = dstBuf[p + 1], b = dstBuf[p + 2];
			p += 3;
			*(uint32 *)result->getBasePtr(x, y) = result->format.ARGBToColor(0xFF, r, g, b);
		}
	}
	return result;
}

} // namespace

Common::Array<Graphics::Surface *> decodeGAN(Common::SeekableReadStream &stream) {
	Common::Array<Graphics::Surface *> frames;

	byte countHdr[4];
	stream.seek(12);
	if (stream.read(countHdr, 4) != 4)
		return frames;
	uint32 frameCount = countHdr[0] | (countHdr[1] << 8) | (countHdr[2] << 16) | (countHdr[3] << 24);
	if (frameCount == 0 || frameCount > kMaxFrames)
		return frames;

	uint32 fileSize = stream.size();
	Common::Array<byte> lastFrame;

	for (uint32 frame = 0; frame < frameCount; frame++) {
		uint64 tableOffset = (uint64)kFrameTableOffset + (uint64)frame * kFrameHeaderSize;
		if (tableOffset + kFrameHeaderSize > fileSize) {
			for (auto *s : frames) {
				s->free();
				delete s;
			}
			return {};
		}
		byte fhdr[kFrameHeaderSize];
		stream.seek(tableOffset);
		if (stream.read(fhdr, kFrameHeaderSize) != kFrameHeaderSize) {
			for (auto *s : frames) {
				s->free();
				delete s;
			}
			return {};
		}
		uint32 ref = fhdr[4] | (fhdr[5] << 8) | (fhdr[6] << 16) | (fhdr[7] << 24);
		uint32 offset = fhdr[8] | (fhdr[9] << 8) | (fhdr[10] << 16) | (fhdr[11] << 24);
		uint32 frameSrcLen = fhdr[12] | (fhdr[13] << 8) | (fhdr[14] << 16) | (fhdr[15] << 24);

		if (offset > fileSize || frameSrcLen > fileSize - offset) {
			for (auto *s : frames) {
				s->free();
				delete s;
			}
			return {};
		}
		Common::Array<byte> srcBuf(frameSrcLen);
		stream.seek(offset);
		if (stream.read(srcBuf.data(), frameSrcLen) != frameSrcLen) {
			for (auto *s : frames) {
				s->free();
				delete s;
			}
			return {};
		}

		Common::Array<byte> dstBuf(kDstLen, (byte)0);
		if (ref != 0)
			decodeDiffFrame(srcBuf, frameSrcLen, dstBuf, lastFrame);
		else
			decodeSolidFrame(srcBuf, frameSrcLen, dstBuf);

		frames.push_back(finishFrame(dstBuf));
		lastFrame = dstBuf;
	}

	return frames;
}

} // End of namespace Ikura::Format::Graphic
