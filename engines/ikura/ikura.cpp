#include "ikura/ikura.h"

#include "common/debug-channels.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"

namespace Ikura {

IkuraEngine::IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc)
	: Engine(syst), _gameDescription(gameDesc), _console(nullptr), _rnd("ikura") {
}

IkuraEngine::~IkuraEngine() {
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

	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			_input.handleEvent(event);
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Ikura
