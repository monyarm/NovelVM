#ifndef BibleBlack_H
#define BibleBlack_H
 
#include "common/random.h"
#include "engines/engine.h"
#include "common/file.h"
#include "gui/debugger.h"
#include "common/scummsys.h"

#include "common/system.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/error.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/hex.h"

#include "engines/util.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "audio/decoders/wave.h"

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
 typedef Common::HashMap<Common::String, Common::ScopedPtr<Common::Archive>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> GameArchives;

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
	GameArchives _archives;
 
	Audio::SoundHandle _shandle;
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