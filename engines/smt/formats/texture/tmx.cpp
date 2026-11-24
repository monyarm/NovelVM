#include "smt/formats/texture/tmx.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace SMT {

TMX::TMX(const byte *data, uint32 size) {
	Common::MemoryReadStream stream(data, size, DisposeAfterUse::NO);
	readFile(&stream);
}

TMX::TMX(Common::SeekableReadStream *stream) {
	if (stream)
		readFile(stream);
}

TMX::TMX(const char *path) {
	Common::File f;
	if (f.open(path))
		readFile(&f);
}

Graphics::Surface *TMX::getSurface() {
	if (!_ok)
		return nullptr;

	int clutCount = Graphics::ps2ClutColorCount(_formatSettings.pixelFmt);
	// The game's TMX loader expands the PS2 half-range palette alpha itself: 0x7F and above
	// become fully opaque, anything lower a * 255 / 128. (RMD textures instead reach the GS as
	// raw half-range alpha, which the shared helper's plain a / 128 scaling models.)
	Common::Array<byte> clut = _clut;
	const bool halfRange = _formatSettings.paletteFmt == Graphics::kPSMCT32;
	if (halfRange)
		for (uint i = 3; i < clut.size(); i += 4)
			clut[i] = clut[i] >= 0x7F ? 255 : (byte)(clut[i] * 255 / 128);
	return Graphics::ps2CreateIndexedSurface(_formatSettings.paletteFmt,
		_formatSettings.numPalettes ? clut.data() : nullptr, clutCount,
		_indices.data(), _formatSettings.pixelFmt, _w, _h,
		clutCount == 256, !halfRange);
}

void TMX::readFile(Common::SeekableReadStream *s) {
	readHeader(s);
	if (!_ok)
		return;
	readClut(s);
	readIndices(s);
}

void TMX::readHeader(Common::SeekableReadStream *s) {
	uint16 tmxID = s->readUint16LE();
	s->readUint16LE(); // userID
	_formatSettings.length = s->readUint32LE();

	char magic[4];
	s->read(magic, 4);
	if (tmxID != 0x0002 || memcmp(magic, "TMX0", 4) != 0) {
		debug("TMX: bad header (id=%04x magic=%.4s)", tmxID, magic);
		return;
	}
	s->readUint32LE(); // pad1

	_formatSettings.numPalettes = s->readByte();
	_formatSettings.paletteFmt = (Graphics::PS2PixelFormat)s->readByte();
	_formatSettings.width = s->readUint16LE();
	_formatSettings.height = s->readUint16LE();
	_formatSettings.pixelFmt = (Graphics::PS2PixelFormat)s->readByte();
	_formatSettings.mipMapCount = s->readByte();
	_formatSettings.mipKL = s->readSint16LE();
	s->readByte(); // reserved
	_formatSettings.wrapModes = s->readByte();
	s->readUint32LE(); // userTextureID
	s->readUint32LE(); // userClutID
	s->skip(28); // userComment

	_w = _formatSettings.width;
	_h = _formatSettings.height;
	_ok = (_w > 0 && _h > 0 && Graphics::ps2IsIndexed(_formatSettings.pixelFmt));
}

void TMX::readClut(Common::SeekableReadStream *s) {
	int clutCount = Graphics::ps2ClutColorCount(_formatSettings.pixelFmt);
	if (_formatSettings.numPalettes == 0 || clutCount == 0)
		return;

	// TODO: the game sizes palettes by their format: 16-bit palettes (formats 2 and 10) use 2
	// bytes per entry (RGB555, entry 0 transparent), and it also loads direct-colour TMX
	// (RGBA32, RGB24, RGB555). Every sampled P3 FES/P4 TMX is indexed with a 32-bit palette,
	// so only that layout is handled: 4 bytes per entry.
	_clut.resize(clutCount * 4);
	s->read(_clut.data(), _clut.size());
	// Multi-palette (palette-swap) TMX: the surface uses palette 0, like Amicitia's default;
	// skip the others so the indices are read from where they really start.
	s->skip((uint32)(_formatSettings.numPalettes - 1) * clutCount * 4);
}

void TMX::readIndices(Common::SeekableReadStream *s) {
	_indices.resize(Graphics::ps2TexelDataSize(_formatSettings.pixelFmt, _w, _h));
	s->read(_indices.data(), _indices.size());

	// Skip any mip levels (data follows the base bitmap, each a halving of
	// the previous level). They are not decoded.
	int mipW = _w / 2, mipH = _h / 2;
	for (int i = 0; i < _formatSettings.mipMapCount && mipW > 0 && mipH > 0; i++) {
		int size = Graphics::ps2TexelDataSize(_formatSettings.pixelFmt, mipW, mipH);
		s->skip(size);
		mipW /= 2;
		mipH /= 2;
	}
}

} // namespace SMT
