#ifndef IKURA_FORMATS_GRAPHIC_GGD24_H
#define IKURA_FORMATS_GRAPHIC_GGD24_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGD 24-bit opaque CG (VileVN reference: CIkura::ggd_24b,
// res/converters/cikura.cpp). Dispatched on 3 distinct magic 4-byte
// headers (see docs/vilevn-audit/PROVENANCE.md); all three use this same
// body format. A back-reference LZ variant over 3-byte BGR pixel triples.
// Returns nullptr on any malformed input; caller owns the returned Surface.
Graphics::Surface *decodeGGD24(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
