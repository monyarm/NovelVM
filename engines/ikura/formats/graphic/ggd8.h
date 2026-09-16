#ifndef IKURA_FORMATS_GRAPHIC_GGD8_H
#define IKURA_FORMATS_GRAPHIC_GGD8_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGD 8-bit paletted hitmap/opaque CG (VileVN reference: CIkura::ggd_8b,
// res/converters/cikura.cpp). A classic LZSS variant (12-bit ring-buffer
// style back-references, 8 literal/back-reference flags per control
// byte) over a 256-color palette. Returns nullptr on any malformed
// input; caller owns the returned Surface.
Graphics::Surface *decodeGGD8(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
