#include "ikura/ikura.h"

#include "common/debug-channels.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"

namespace Ikura {

IkuraEngine::IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc)
	: Engine(syst), _gameDescription(gameDesc), _console(nullptr), _rnd("ikura") {
	// Keep the constructor free of file I/O and device initialization;
	// resource loading belongs in run(), once graphics/audio are up.
}

IkuraEngine::~IkuraEngine() {
	delete _console;
}

Common::Error IkuraEngine::run() {
	// The 8 ikura games run at 640x480 except Hitomi (800x600); the
	// per-game resolution table lands with the game profiles in Task 7.
	// Fixed default is fine for the Task 2 skeleton.
	initGraphics(640, 480);

	DebugMan.addDebugChannel(kDebugScript, "script", "Ikura/GDL script interpreter");
	DebugMan.addDebugChannel(kDebugGraphics, "graphics", "Ikura/GDL graphics and presentation");
	DebugMan.addDebugChannel(kDebugSound, "sound", "Ikura/GDL audio and voice playback");

	_console = new Console(this);

	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace Ikura
