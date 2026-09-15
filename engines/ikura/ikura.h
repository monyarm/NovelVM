#ifndef IKURA_H
#define IKURA_H

#include "common/error.h"
#include "common/random.h"
#include "common/scummsys.h"
#include "engines/engine.h"
#include "gui/debugger.h"
#include "ikura/runtime/input.h"

struct ADGameDescription;

namespace Ikura {

/**
 * Debug channels registered with DebugMan in IkuraEngine::run(), after
 * graphics init. Extend as Tasks 3/4 add the archive/script layers.
 */
enum IkuraDebugChannels {
	kDebugScript = 1 << 0,
	kDebugGraphics = 1 << 1,
	kDebugSound = 1 << 2
};

class Console;

class IkuraEngine : public Engine {
public:
	IkuraEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~IkuraEngine() override;

	Common::Error run() override;

	const char *getGameId() const;
	Common::Platform getPlatform() const;

private:
	const ADGameDescription *_gameDescription;
	Console *_console;
	Common::RandomSource _rnd;
	Input _input;
};

class Console : public GUI::Debugger {
public:
	Console(IkuraEngine *vm) {}
	~Console() override {}
};

} // End of namespace Ikura

#endif
