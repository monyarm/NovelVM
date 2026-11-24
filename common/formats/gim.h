#ifndef COMMON_FORMATS_GIM_H
#define COMMON_FORMATS_GIM_H

#include "common/array.h"
#include "common/stream.h"

#include "graphics/surface.h"

namespace Common {

/** Sony PSP/PS3 Graphic Image Map texture container. */
class GIM {
public:
	GIM(SeekableReadStream *stream);
	~GIM();

	bool ok() const { return _ok; }
	Graphics::Surface *getSurface() { return &_surface; }
	uint16 width() const { return _width; }
	uint16 height() const { return _height; }
	uint16 levelCount() const { return _levelCount; }

private:
	struct Image {
		uint16 format, pixelOrder, width, height, bpp, pitchAlign;
		uint32 nextIndex, frameStart, frameEnd;
		uint16 levelCount, frameCount;
		const byte *data;
		uint32 dataSize;
	};

	Common::Array<byte> _data;
	Graphics::Surface _surface;
	bool _ok = false;
	bool _bigEndian = false;
	uint16 _width = 0, _height = 0, _levelCount = 0;
	Image _image = {};
	Image _palette = {};
	bool _hasImage = false, _hasPalette = false;

	uint16 read16(const byte *p) const;
	uint32 read32(const byte *p) const;
	bool range(uint32 offset, uint32 size) const;
	void parseRegion(uint32 start, uint32 end);
	void parseImage(uint32 blockStart, uint32 blockEnd, bool palette);
	bool decode();
	uint32 pixelValue(uint32 x, uint32 y, uint32 storageWidth) const;
	uint32 paletteValue(uint32 index) const;
	static uint8 expand(uint32 value, uint32 bits);
	static uint32 rgba(uint8 r, uint8 g, uint8 b, uint8 a);
};

} // namespace Common

#endif
