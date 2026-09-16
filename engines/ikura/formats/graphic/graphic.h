#ifndef IKURA_FORMATS_GRAPHIC_GRAPHIC_H
#define IKURA_FORMATS_GRAPHIC_GRAPHIC_H

#include "common/str.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// Picks the right decoder for a resource by its extension - real cabinets
// mix formats within the same archive (e.g. Snow Sakura's GGD cabinet has
// both .GG0-.GG9 GGA-format and .GGD GGD8-format members side by side; see
// engines/ikura/README.md's real-data validation table). Returns nullptr
// if the extension is unrecognized or the decoder rejects the stream.
Graphics::Surface *decodeImage(const Common::String &name, Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
