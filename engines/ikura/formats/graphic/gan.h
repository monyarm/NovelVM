#ifndef IKURA_FORMATS_GRAPHIC_GAN_H
#define IKURA_FORMATS_GRAPHIC_GAN_H

#include "common/array.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GAN animation (VileVN reference: CIkura::gan, res/converters/cikura.cpp).
// A sequence of 24-bit frames, always 640x480 regardless of the game's
// actual resolution (a property of the reference format, not a
// limitation of this port - see gan.cpp). Each frame is either a
// standalone RLE-compressed frame or an RLE delta against the previous
// one. Returns an empty array if the file is too malformed to identify
// any frames at all; a truncated individual frame stops decoding that
// frame early (rest left black) rather than discarding the whole
// animation, since VileVN itself has no error handling here and a
// partial frame is still useful for a cosmetic animation. Caller owns
// every returned Surface.
Common::Array<Graphics::Surface *> decodeGAN(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
