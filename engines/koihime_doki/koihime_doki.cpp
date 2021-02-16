


#include "koihime_doki/koihime_doki.h"
#include "common/events.h"
#include "koihime_doki/formats/archive/exwlcs.h"

namespace KoihimeDoki
{

KoihimeDokiEngine::KoihimeDokiEngine(OSystem *syst, const ADGameDescription *desc)
	: Engine(syst), _gameDescription(desc), _console(nullptr)
{
	// Put your engine in a sane state, but do nothing big yet;
	// in particular, do not load data from files; rather, if you
	// need to do such things, do them from run().

	// Do not initialize graphics here
	// Do not initialize audio devices here

	// However this is the place to specify all default directories
	const Common::FSNode gameDataDir(ConfMan.get("path"));
	// Here is the right place to set up the engine specific debug channels
	DebugMan.addDebugChannel(kKoihimeDokiDebug, "example", "this is just an example for a engine specific debug channel");
	DebugMan.addDebugChannel(kKoihimeDokiDebug2, "example2", "also an example");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("koihime_doki");

	debug("KoihimeDokiEngine::KoihimeDokiEngine");
}

KoihimeDokiEngine::~KoihimeDokiEngine()
{
	debug("KoihimeDokiEngine::~KoihimeDokiEngine");

	// Dispose your resources here
	delete _rnd;

	// Remove all of our debug levels here
	DebugMan.clearAllDebugChannels();
}

Common::Error KoihimeDokiEngine::run()
{
	// Initialize graphics using following:

	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();

	initGraphics(480, 272, formats);

	Graphics::PixelFormat format = g_system->getScreenFormat();

	Common::Archive *lcsebody1 = makeEXWLCSArchive("lcsebody1");
	_archives["lcsebody1"].reset(lcsebody1);


	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	for (auto &&l : list)
	{
		debug("%s", l.get()->getName().c_str());
	}
	list = Common::ArchiveMemberList();
	Common::File f;

/*
	if (!f.open("bgm01.wav", *_archives["STREAM.PAK"].get()))
	{
		error("can't read archive");
	} */

	_archives["lcsebody1"].get()->listMembers(list);

	for (auto &&l : list)
	{
		debug("%s", l.get()->getName().c_str());
	}
	list = Common::ArchiveMemberList();

	Common::Event e;
	Common::DumpFile df;
	df.open("dumps/d.bmp", true);
	auto rs =_archives["lcsebody1"].get()->createReadStreamForMember("TCG_BUN41AC.bmp");
	df.writeStream(rs,rs->size());
	df.close();
	while (!shouldQuit())
	{
		g_system->getEventManager()->pollEvent(e);
		g_system->delayMillis(10);
	}

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
	debug("KoihimeDokiEngine::init");

	// Your main even loop should be (invoked from) here.
	debug("KoihimeDokiEngine::go: Hello, World!");

	// This test will show up if -d1 and --debugflags=example are specified on the commandline
	debugC(1, kKoihimeDokiDebug, "Example debug call");

	// This test will show up if --debugflags=example or --debugflags=example2 or both of them and -d3 are specified on the commandline
	debugC(3, kKoihimeDokiDebug | kKoihimeDokiDebug2, "Example debug call two");

	return Common::kNoError;
}

} // End of namespace KoihimeDoki
