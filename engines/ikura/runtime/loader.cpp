#include "ikura/runtime/loader.h"

#include "common/path.h"

namespace Ikura {

VM::Context *loadTitleScript(Common::Archive *cabinet) {
	if (!cabinet)
		return nullptr;

	Common::SeekableReadStream *scriptStream = cabinet->createReadStreamForMember(Common::Path("TITLE.ISF"));
	if (!scriptStream)
		return nullptr;

	Format::Script::Script *script = Format::Script::Script::load(*scriptStream);
	delete scriptStream;
	if (!script)
		return nullptr;

	VM::Context *ctx = new VM::Context(script);
	ctx->setScriptCabinet(cabinet);
	return ctx;
}

} // End of namespace Ikura
