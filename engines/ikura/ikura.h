#ifndef IKURA_H
#define IKURA_H

#include "common/error.h"
#include "common/random.h"
#include "common/scummsys.h"
#include "engines/engine.h"
#include "gui/debugger.h"
#include "ikura/runtime/input.h"

struct ADGameDescription;

namespace Common {
class Archive;
}

namespace Ikura {

namespace VM {
class Context;
}

namespace Runtime {
class Presentation;
class Audio;
}

/**
 * Debug channels registered with DebugMan in IkuraEngine::run().
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
	VM::Context *_scriptContext;
	Common::Archive *_scriptCabinet;
	Common::Archive *_graphicsCabinet;
	Runtime::Presentation *_presentation;
	Common::Archive *_musicCabinet;
	Common::Archive *_seCabinet;
	Common::Archive *_voiceCabinet;
	Runtime::Audio *_audio;

	// Which of StepResult's pause kinds (if any) the interpreter is
	// currently sitting in - see script/interpreter.h's StepResult
	// comments for what each caller-side resume policy needs to check.
	enum class PauseState { kNone, kAdvance, kTimer };
	PauseState _pauseState = PauseState::kNone;
};

class Console : public GUI::Debugger {
public:
	Console(IkuraEngine *vm) {}
	~Console() override {}
};

} // End of namespace Ikura

#endif
