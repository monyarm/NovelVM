#ifndef BibleBlack_H
#define BibleBlack_H
 
#include "common/random.h"
#include "engines/engine.h"
#include "gui/debugger.h"

struct ADGameDescription;
 
namespace BibleBlack {
 
 
class Console;
 
// our engine debug channels
enum {
	kBibleBlackDebug = 1 << 0,
	kBibleBlackDebug2 = 1 << 1
	// next new channel must be 1 << 2 (4)
	// the current limitation is 32 debug channels (1 << 31 is the last one)
};
 
class BibleBlackEngine : public Engine {
public:
	BibleBlackEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~BibleBlackEngine();
 
	virtual Common::Error run();

	// Detection related functions
	const ADGameDescription *_gameDescription;
	const char *getGameId() const;
	Common::Platform getPlatform() const;
 
private:
	Console *_console;
 
	// We need random numbers
	Common::RandomSource *_rnd;
};
 
// Example console class
class Console : public GUI::Debugger {
public:
	Console(BibleBlackEngine *vm) {}
	virtual ~Console(void) {}
};
 
} // End of namespace BibleBlack
 
#endif