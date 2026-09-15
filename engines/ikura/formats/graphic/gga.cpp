#include "ikura/formats/graphic/gga.h"

#include "common/array.h"

namespace Ikura::Format::Graphic {

namespace {
const int kMaxDimension = 8192;
}

Graphics::Surface *decodeGGA(Common::SeekableReadStream &stream) {
	byte hdr[24];
	stream.seek(0);
	if (stream.read(hdr, sizeof(hdr)) != sizeof(hdr))
		return nullptr;
	if (memcmp(hdr, "GGA00000", 8) != 0)
		return nullptr;

	int width = hdr[8] | (hdr[9] << 8);
	int height = hdr[10] | (hdr[11] << 8);
	if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
		return nullptr;

	uint32 headerSize = hdr[16] | (hdr[17] << 8) | (hdr[18] << 16) | (hdr[19] << 24);
	uint32 cryptLength = hdr[20] | (hdr[21] << 8) | (hdr[22] << 16) | (hdr[23] << 24);

	uint32 srcLen = stream.size();
	if (headerSize > srcLen || cryptLength > srcLen - headerSize)
		return nullptr;
	Common::Array<byte> srcBuf(srcLen);
	stream.seek(0);
	if (stream.read(srcBuf.data(), srcLen) != srcLen)
		return nullptr;

	uint32 dstLen = (uint32)width * (uint32)height * 4;
	Common::Array<byte> dstBuf(dstLen);

	auto readPixel = [&](uint32 from, byte *out) -> bool {
		if (from + 4 > dstLen)
			return false;
		out[0] = dstBuf[from];
		out[1] = dstBuf[from + 1];
		out[2] = dstBuf[from + 2];
		out[3] = dstBuf[from + 3];
		return true;
	};
	auto writePixel = [&](uint32 at, const byte *in) -> bool {
		if (at + 4 > dstLen)
			return false;
		dstBuf[at] = in[0];
		dstBuf[at + 1] = in[1];
		dstBuf[at + 2] = in[2];
		dstBuf[at + 3] = in[3];
		return true;
	};

	// opaque_00 in the reference: if every literal pixel's alpha byte was
	// 0x00, the source treated that as "not really transparent, just
	// unset" and forced full opacity instead. It also tracked an
	// opaque_ff flag the same way, but never used it for anything - not
	// replicated here since it provably can't affect output.
	bool opaqueAllZero = true;
	uint32 ipos = headerSize;
	uint32 opos = 0;
	uint32 cryptEnd = headerSize + cryptLength;

	while (ipos < cryptEnd) {
		if (ipos >= srcLen)
			return nullptr;
		byte ctrl = srcBuf[ipos++];
		byte arr[4];

		if (ctrl == 0x00 || ctrl == 0x01) {
			if (opos < 4 || !readPixel(opos - 4, arr))
				return nullptr;
			uint32 len;
			if (ctrl == 0x00) {
				if (ipos >= srcLen)
					return nullptr;
				len = srcBuf[ipos++];
			} else {
				if (ipos + 1 >= srcLen)
					return nullptr;
				byte b0 = srcBuf[ipos++], b1 = srcBuf[ipos++];
				len = b0 | (b1 << 8);
			}
			for (uint32 j = 0; j < len; j++) {
				if (!writePixel(opos, arr))
					return nullptr;
				opos += 4;
			}
		} else if (ctrl == 0x02 || ctrl == 0x03) {
			uint32 dist;
			if (ctrl == 0x02) {
				if (ipos >= srcLen)
					return nullptr;
				dist = srcBuf[ipos++];
			} else {
				if (ipos + 1 >= srcLen)
					return nullptr;
				byte b0 = srcBuf[ipos++], b1 = srcBuf[ipos++];
				dist = b0 | (b1 << 8);
			}
			uint32 back = dist * 4;
			if (back == 0 || back > opos || !readPixel(opos - back, arr) || !writePixel(opos, arr))
				return nullptr;
			opos += 4;
		} else if (ctrl >= 0x04 && ctrl <= 0x07) {
			uint32 dist;
			if (ctrl == 0x04 || ctrl == 0x05) {
				if (ipos >= srcLen)
					return nullptr;
				dist = srcBuf[ipos++];
			} else {
				if (ipos + 1 >= srcLen)
					return nullptr;
				byte b0 = srcBuf[ipos++], b1 = srcBuf[ipos++];
				dist = b0 | (b1 << 8);
			}
			uint32 back = dist * 4;
			if (back == 0 || back > opos)
				return nullptr;
			uint32 len;
			if (ctrl == 0x04 || ctrl == 0x06) {
				if (ipos >= srcLen)
					return nullptr;
				len = srcBuf[ipos++];
			} else {
				if (ipos + 1 >= srcLen)
					return nullptr;
				byte b0 = srcBuf[ipos++], b1 = srcBuf[ipos++];
				len = b0 | (b1 << 8);
			}
			uint32 tpos = opos - back;
			for (uint32 j = 0; j < len; j++) {
				if (!readPixel(tpos, arr) || !writePixel(opos, arr))
					return nullptr;
				tpos += 4;
				opos += 4;
			}
		} else if (ctrl >= 0x08 && ctrl <= 0x0B) {
			uint32 back;
			switch (ctrl) {
			case 0x08:
				back = 4;
				break;
			case 0x09:
				back = (uint32)width * 4;
				break;
			case 0x0A:
				back = (uint32)width * 4 + 4;
				break;
			default: // 0x0B
				back = (uint32)width * 4 - 4;
				break;
			}
			if (back == 0 || back > opos || !readPixel(opos - back, arr) || !writePixel(opos, arr))
				return nullptr;
			opos += 4;
		} else {
			int count = ctrl - 11;
			for (int j = 0; j < count; j++) {
				if (ipos + 4 > srcLen || opos + 4 > dstLen)
					return nullptr;
				for (int k = 0; k < 4; k++)
					dstBuf[opos + k] = srcBuf[ipos + k];
				opaqueAllZero &= (srcBuf[ipos + 3] == 0x00);
				ipos += 4;
				opos += 4;
			}
		}
	}

	Graphics::Surface *result = new Graphics::Surface();
	result->create(width, height, Graphics::PixelFormat::createFormatRGBA32());
	uint32 p = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (p + 4 > opos) {
				delete result;
				return nullptr;
			}
			byte b = dstBuf[p], g = dstBuf[p + 1], r = dstBuf[p + 2], a = dstBuf[p + 3];
			p += 4;
			if (opaqueAllZero)
				a = 0xFF;
			*(uint32 *)result->getBasePtr(x, y) = result->format.ARGBToColor(a, r, g, b);
		}
	}
	return result;
}

} // End of namespace Ikura::Format::Graphic
