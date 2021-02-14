#include "common/scummsys.h"

#include "common/system.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/error.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"

#include "smt/formats/archive/cpk.h"
#include "smt/formats/video/pmsf.h"
#include "smt/formats/image/tmx.h"

#include "engines/util.h"

#include "smt/smt.h"

namespace SMT
{

SMTEngine::SMTEngine(OSystem *syst, const ADGameDescription *desc)
	: Engine(syst), _gameDescription(desc), _console(nullptr)
{
	// Put your engine in a sane state, but do nothing big yet;
	// in particular, do not load data from files; rather, if you
	// need to do such things, do them from run().

	// Do not initialize graphics here
	// Do not initialize audio devices here

	// However this is the place to specify all default directories
	const Common::FSNode gameDataDir(ConfMan.get("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "sound/pmsf");

	// Here is the right place to set up the engine specific debug channels
	DebugMan.addDebugChannel(kSMTDebug, "example", "this is just an example for a engine specific debug channel");
	DebugMan.addDebugChannel(kSMTDebug2, "example2", "also an example");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("smt");


	debug("SMTEngine::SMTEngine");
}

SMTEngine::~SMTEngine()
{
	debug("SMTEngine::~SMTEngine");

	// Dispose your resources here
	delete _rnd;

	// Remove all of our debug levels here
	DebugMan.clearAllDebugChannels();
}


Common::Error SMTEngine::run()
{
	// Initialize graphics using following:

	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();

	initGraphics(480, 272, formats);

	Graphics::PixelFormat format = g_system->getScreenFormat();
	
	//CPKFile _cpk = CPKFile();

	//_cpk.ReadFile("umd0.cpk");


	//PMSFFile _pmsf = PMSFFile();

	//_pmsf.ReadFile("p3opmv_p3p.pmsf");

	TMXFile _tmx = TMXFile();

	_tmx.ReadFile("test.tmx");


	// You could use backend transactions directly as an alternative,
	// but it isn't recommended, until you want to handle the error values
	// from OSystem::endGFXTransaction yourself.
	// This is just an example template:
	//_system->beginGFXTransaction();
	//	// This setup the graphics mode according to users seetings
	//	initCommonGFX(false);
	//
	//	// Specify dimensions of game graphics window.
	//	// In this example: 320x200
	//	_system->initSize(320, 200);
	//FIXME: You really want to handle
	//OSystem::kTransactionSizeChangeFailed here
	//_system->endGFXTransaction();

	// Create debugger console. It requires GFX to be initialized
	_console = new Console(this);

	// Additional setup.
	debug("SMTEngine::init");

	// Your main even loop should be (invoked from) here.
	debug("SMTEngine::go: Hello, World!");

	// This test will show up if -d1 and --debugflags=example are specified on the commandline
	debugC(1, kSMTDebug, "Example debug call");

	// This test will show up if --debugflags=example or --debugflags=example2 or both of them and -d3 are specified on the commandline
	debugC(3, kSMTDebug | kSMTDebug2, "Example debug call two");

	return Common::kNoError;
}

} // End of namespace SMT
