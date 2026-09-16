#include "ikura/formats/graphic/graphic.h"

#include "ikura/formats/graphic/gga.h"
#include "ikura/formats/graphic/ggd8.h"
#include "ikura/formats/graphic/ggd24.h"
#include "ikura/formats/graphic/ggp.h"

namespace Ikura::Format::Graphic {

namespace {

Common::String extensionOf(const Common::String &name) {
	int dot = (int)name.size() - 1;
	while (dot >= 0 && name[dot] != '.')
		dot--;
	return dot >= 0 ? name.substr(dot) : Common::String();
}

} // namespace

Graphics::Surface *decodeImage(const Common::String &name, Common::SeekableReadStream &stream) {
	Common::String ext = extensionOf(name);
	if (ext.equalsIgnoreCase(".GGP"))
		return decodeGGP(stream);
	if (ext.equalsIgnoreCase(".GGD"))
		return decodeGGD8(stream);
	if (ext.equalsIgnoreCase(".DRG"))
		return decodeGGD24(stream);
	if (ext.size() == 4 && ext.substr(0, 3).equalsIgnoreCase(".GG"))
		return decodeGGA(stream);
	return nullptr;
}

} // End of namespace Ikura::Format::Graphic
