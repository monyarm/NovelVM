#include "ikura/runtime/loader.h"

#include "common/path.h"
#include "ikura/formats/archive/cabinet.h"

namespace Ikura {

VM::Context *loadTitleScript(Common::SeekableReadStream *cabinetStream) {
	Format::Archive::Cabinet *cabinet = Format::Archive::Cabinet::open(cabinetStream);
	if (!cabinet)
		return nullptr;

	Common::SeekableReadStream *scriptStream = cabinet->createReadStreamForMember(Common::Path("TITLE.ISF"));
	if (!scriptStream) {
		delete cabinet;
		return nullptr;
	}

	Format::Script::Script *script = Format::Script::Script::load(*scriptStream);
	delete scriptStream;
	delete cabinet;
	if (!script)
		return nullptr;

	return new VM::Context(script);
}

} // End of namespace Ikura
