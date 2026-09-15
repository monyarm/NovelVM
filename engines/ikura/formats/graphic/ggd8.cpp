#include "ikura/formats/graphic/ggd8.h"

#include "common/array.h"

namespace Ikura::Format::Graphic {

namespace {
// Sanity cap, not part of the reference format: real ikura CGs are at
// most 800x600. Blocks a malicious header from requesting a multi-GB
// decompression buffer.
const int kMaxDimension = 8192;
const uint32 kHeaderSize = 16;
const uint32 kPaletteEntries = 256;
const uint32 kUnknownBlockSize = 28;
const uint32 kPaletteSkipSize = 4;
const uint32 kDataStart = kHeaderSize + kUnknownBlockSize + kPaletteEntries * 4 + kPaletteSkipSize;
}

Graphics::Surface *decodeGGD8(Common::SeekableReadStream &stream) {
	byte hdr[kHeaderSize];
	stream.seek(0);
	if (stream.read(hdr, sizeof(hdr)) != sizeof(hdr))
		return nullptr;

	int width = hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | (hdr[11] << 24);
	int32 rawHeight = hdr[12] | (hdr[13] << 8) | (hdr[14] << 16) | (hdr[15] << 24);
	int height = rawHeight < 0 ? -rawHeight : rawHeight;
	if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
		return nullptr;

	uint32 srcLen = stream.size();
	if (srcLen < kDataStart)
		return nullptr;
	Common::Array<byte> srcBuf(srcLen);
	stream.seek(0);
	if (stream.read(srcBuf.data(), srcLen) != srcLen)
		return nullptr;

	uint32 palette[kPaletteEntries];
	uint32 palOff = kHeaderSize + kUnknownBlockSize;
	for (uint32 i = 0; i < kPaletteEntries; i++) {
		uint32 o = palOff + i * 4;
		palette[i] = srcBuf[o] | (srcBuf[o + 1] << 8) | (srcBuf[o + 2] << 16) | (srcBuf[o + 3] << 24);
	}

	// Matches the reference's own over-allocation; the final read-out
	// loop below only ever touches the first width*height bytes.
	uint32 dstLen = (uint32)(width + 1) * (uint32)(height + 1);
	Common::Array<byte> dstBuf(dstLen);

	uint32 ipos = kDataStart;
	uint32 o = 0; // reference tracks this twice, as `p` and `opos`, always in lockstep
	while (ipos < srcLen && o < dstLen) {
		byte ctrl = srcBuf[ipos++];
		for (int bit = 0; bit < 8; bit++) {
			if (ipos >= srcLen)
				break;
			if (ctrl & 1) {
				if (o >= dstLen)
					return nullptr;
				dstBuf[o++] = srcBuf[ipos++];
			} else {
				if (ipos + 1 >= srcLen)
					return nullptr;
				uint32 backword = srcBuf[ipos] | ((srcBuf[ipos + 1] & 0xF0) << 4);
				backword = (o - backword - 19) & 0x0FFF;
				backword++;
				int leng = (srcBuf[ipos + 1] & 0x0F) + 3;
				ipos += 2;

				int32 zero = (int32)backword - (int32)o;
				if (zero > 0) {
					if (leng < zero)
						zero = leng;
					leng -= zero;
					for (int j = 0; j < zero; j++) {
						if (o >= dstLen)
							return nullptr;
						dstBuf[o++] = 0;
					}
				}
				while (leng-- > 0) {
					if (o >= dstLen || backword > o)
						return nullptr;
					dstBuf[o] = dstBuf[o - backword];
					o++;
				}
			}
			ctrl >>= 1;
		}
	}

	Graphics::Surface *result = new Graphics::Surface();
	result->create(width, height, Graphics::PixelFormat::createFormatRGBA32());
	uint32 p = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			// Bound against how much was actually decompressed (o), not
			// the allocated capacity (dstLen) - a truncated stream can
			// exit the loop above with o < width*height.
			if (p >= o) {
				delete result;
				return nullptr;
			}
			uint32 color = palette[dstBuf[p++]];
			byte r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
			*(uint32 *)result->getBasePtr(x, y) = result->format.ARGBToColor(0xFF, r, g, b);
		}
	}
	return result;
}

} // End of namespace Ikura::Format::Graphic
