#ifndef SMT_H
#define SMT_H
 
#include "common/random.h"
#include "engines/engine.h"
#include "gui/debugger.h"
#include "common/scummsys.h"

#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/palette.h"
#include "graphics/surface.h"
// #include "smt/gfx/gfx.h"

struct ADGameDescription;
 
namespace SMT {
 
 
class Console;
 
class SMTEngine : public Engine {
public:
	SMTEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~SMTEngine();
 
	virtual Common::Error run();

	// Detection related functions
	const ADGameDescription *_gameDescription;
	const char *getGameId() const;
	Common::Platform getPlatform() const;
 
private:
	Console *_console;
	// Renderer *_gfx;
	Audio::SoundHandle _shandle;

	// We need random numbers
	Common::RandomSource *_rnd;
};
 
// Example console class
class Console : public GUI::Debugger {
public:
	Console(SMTEngine *vm) {}
	virtual ~Console(void) {}
};
 
} // End of namespace SMT
 
#endif