#ifndef IKURA_FORMATS_GRAPHIC_GGP_H
#define IKURA_FORMATS_GRAPHIC_GGP_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGP (Ikura/GDL reference: CIkura::ggp_32b, res/converters/cikura.cpp):
// an XOR-obfuscated PNG. Returns nullptr on any malformed input; caller
// owns the returned Surface.
Graphics::Surface *decodeGGP(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
