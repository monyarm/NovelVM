#ifndef IKURA_FORMATS_GRAPHIC_GGA_H
#define IKURA_FORMATS_GRAPHIC_GGA_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGA: 32-bit per-pixel-alpha CG, used for transparent CGs (VileVN
// reference: CIkura::gga_32b, res/converters/cikura.cpp). A back-reference
// run-length scheme over 4-byte BGRA pixels with a dozen distinct control
// codes. Returns nullptr on any malformed input; caller owns the
// returned Surface.
Graphics::Surface *decodeGGA(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
