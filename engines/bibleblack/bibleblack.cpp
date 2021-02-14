

#include "bibleblack/formats/archive/pak.h"


#include "bibleblack/bibleblack.h"


namespace BibleBlack
{

BibleBlackEngine::BibleBlackEngine(OSystem *syst, const ADGameDescription *desc)
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
	DebugMan.addDebugChannel(kBibleBlackDebug, "example", "this is just an example for a engine specific debug channel");
	DebugMan.addDebugChannel(kBibleBlackDebug2, "example2", "also an example");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("bibleblack");

	debug("BibleBlackEngine::BibleBlackEngine");
}

BibleBlackEngine::~BibleBlackEngine()
{
	debug("BibleBlackEngine::~BibleBlackEngine");

	// Dispose your resources here
	delete _rnd;

	// Remove all of our debug levels here
	DebugMan.clearAllDebugChannels();
}

Common::Error BibleBlackEngine::run()
{
	// Initialize graphics using following:

	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();

	initGraphics(480, 272, formats);

	Graphics::PixelFormat format = g_system->getScreenFormat();


	Common::Archive *A98FAST_PAK = makePAKArchive("A98FAST.PAK");
	_archives["A98FAST.PAK"].reset(A98FAST_PAK);

	Common::Archive *A98SYS_PAK = makePAKArchive("A98SYS.PAK");
	_archives["A98SYS.PAK"].reset(A98SYS_PAK);

	Common::Archive *STREAM_PAK = makePAKArchive("STREAM.PAK");
	_archives["STREAM.PAK"].reset(STREAM_PAK);

	/*
	Common::Archive *VOICE_PAK = makePAKArchive("VOICE.PAK");
	Common::Archive *VOICE2_PAK = makePAKArchive("VOICE2.PAK");
	
	*/

	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	Common::File f;

	
	if (!f.open("BGM01.WAV", *_archives["STREAM.PAK"].get()))
	{
		debug("can't read archive");
	}
	
	Audio::SeekableAudioStream *str;
	str = Audio::makeWAVStream(f.readStream(f.size()),DisposeAfterUse::YES);
	_mixer->playStream(Audio::Mixer::SoundType::kMusicSoundType, _shandle, str);

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
	debug("BibleBlackEngine::init");

	// Your main even loop should be (invoked from) here.
	debug("BibleBlackEngine::go: Hello, World!");

	// This test will show up if -d1 and --debugflags=example are specified on the commandline
	debugC(1, kBibleBlackDebug, "Example debug call");

	// This test will show up if --debugflags=example or --debugflags=example2 or both of them and -d3 are specified on the commandline
	debugC(3, kBibleBlackDebug | kBibleBlackDebug2, "Example debug call two");

	return Common::kNoError;
}

} // End of namespace BibleBlack
