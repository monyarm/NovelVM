#include "ikura/ikura.h"

#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"
#include "ikura/runtime/loader.h"
#include "ikura/script/interpreter.h"

namespace Ikura {

IkuraEngine::IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc)
	: Engine(syst), _gameDescription(gameDesc), _console(nullptr), _rnd("ikura"), _scriptContext(nullptr) {
}

IkuraEngine::~IkuraEngine() {
	delete _scriptContext;
	delete _console;
}

Common::Error IkuraEngine::run() {
	// All 8 ikura games run at 640x480 except Hitomi (800x600); no
	// per-game resolution table yet.
	initGraphics(640, 480);

	DebugMan.addDebugChannel(kDebugScript, "script", "Ikura/GDL script interpreter");
	DebugMan.addDebugChannel(kDebugGraphics, "graphics", "Ikura/GDL graphics and presentation");
	DebugMan.addDebugChannel(kDebugSound, "sound", "Ikura/GDL audio and voice playback");

	_console = new Console(this);

	// The shared cabinet ships as a bare file named "isf" (no extension);
	// its entry-point member is always "TITLE.ISF" (see loadTitleScript).
	Common::File *isf = new Common::File();
	if (isf->open("isf"))
		_scriptContext = loadTitleScript(isf);
	else
		delete isf;

	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			_input.handleEvent(event);
		}

		if (_scriptContext) {
			// run() always stops on kEnd/kScriptExhausted/kMalformedOpcode/
			// kUnimplementedOpcode - none of the implemented opcodes yet
			// block on input, so a run drains everything runnable in one
			// call.
			byte lastOpcode;
			VM::StepResult result = VM::run(*_scriptContext, lastOpcode);
			if (result == VM::StepResult::kUnimplementedOpcode)
				debugC(kDebugScript, "stopped on unimplemented opcode 0x%02x", lastOpcode);
			else if (result == VM::StepResult::kMalformedOpcode)
				debugC(kDebugScript, "stopped on malformed opcode 0x%02x", lastOpcode);
			delete _scriptContext;
			_scriptContext = nullptr;
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Ikura
