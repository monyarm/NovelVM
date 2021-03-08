#include "common/list.h"
#include "formats/archive/cpk.h"
#include "formats/archive/cvm.h"
#include "formats/audio/adx.h"
#include "formats/graphic/dds.h"
#include "formats/video/pmsf.h"
#include "graphics/transform_struct.h"
#include "graphics/transparent_surface.h"
#include "smt/formats/archive/pac.h"
#include "smt/formats/graphic/tmx.h"
#include "smt/formats/script/bmd.h"

#include "smt/smt.h"
#include "util.h"

namespace SMT {

SMTEngine::SMTEngine(OSystem *syst, const ADGameDescription *desc)
    : Engine(syst), _gameDescription(desc), _console(nullptr), _gfx(0) {
	// Put your engine in a sane state, but do nothing big yet;
	// in particular, do not load data from files; rather, if you
	// need to do such things, do them from run().

	// Do not initialize graphics here
	// Do not initialize audio devices here

	// However this is the place to specify all default directories
	const Common::FSNode gameDataDir(ConfMan.get("path"));
	//SearchMan.addSubDirectoryMatching(gameDataDir, "sound/pmsf");

	// Here is the right place to set up the engine specific debug channels
	DebugMan.addDebugChannel(kSMTDebug, "example", "this is just an example for a engine specific debug channel");
	DebugMan.addDebugChannel(kSMTDebug2, "example2", "also an example");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("smt");

	debug("SMTEngine::SMTEngine");
}

SMTEngine::~SMTEngine() {
	debug("SMTEngine::~SMTEngine");

	// Dispose your resources here
	delete _rnd;

	// Remove all of our debug levels here
	DebugMan.clearAllDebugChannels();
}

Common::Error SMTEngine::run() {
	// Initialize graphics using following:

	Common::List<Graphics::PixelFormat> formats = Common::List<Graphics::PixelFormat>();
	formats.push_back(Graphics::PixelFormat(4, 8, 8, 8, 8, 24,16,8,0));//0, 8, 16, 24));
	// _gfx = createRenderer(_system/*, getGameId()*/);
	// _gfx->init();
	// _gfx->clear();

	initGraphics(640, 480, formats);

	//_frameLimiter = new FrameLimiter(_system, ConfMan.getInt("engine_speed"));
	//CPK::CPKFile _cpk = CPK::CPKFile("umd0.cpk");

	//PMSFFile _pmsf = PMSFFile();

	//_pmsf.ReadFile("p3opmv_p3p.pmsf");

	//Format::Graphic::DDSFile _dds("test/DXT5.dds");
	//ADXFile _adx("test/THEME.ADX");

	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	//for (auto &&l : list)
	{
		//debug(l.get()->getName().c_str());
	}
	list = Common::ArchiveMemberList();
	Common::File f;

	//CVMArchive _data("DATA.CVM");
	PAC _data("test/DATMSG.PAK");
	_data.listMembers(list);
	for (auto &&l : list) {
		debug("%s", l.get()->getName().c_str());
	}
	//Common::DumpFile df;
	//df.writeStream(_dfile);

	TMX _tmx("test/COIN_C10.TMX");
	Format::Script::BMD _bmd("test/field.BMD");
	//TMX _tmx("test/PSMT8.tmx");


	//CVMArchive _data("DATA.CVM");
	//CVMArchive _btl("BTL.CVM");

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
	//OSystem::kTransactionSizeChangeFailed here
	//_system->endGFXTransaction();

	// Create debugger console. It requires GFX to be initialized
	_console = new Console(this);

	// Additional setup.
	debug("SMTEngine::init");

	Common::Event e;

	g_system->getEventManager()->pollEvent(e);
	g_system->delayMillis(10);

	Graphics::TransparentSurface *surfacetmx = _tmx.getSurface();
	Common::Rect tmxRect = Common::Rect(surfacetmx->w, surfacetmx->h);

	//Graphics::TransparentSurface *surfacedds = _dds.getSurface();
	//Common::Rect ddsRect = Common::Rect(0 - surfacedds->w, 0, surfacedds->w, surfacedds->h);
	// auto texturetmx = _gfx->createTexture(surfacetmx);
	//auto texturedds = _gfx->createTexture(surfacedds);

	// debug("%i %i", _gfx->viewport().width(), _gfx->viewport().height());
	// _gfx->selectTargetWindow(nullptr, false, false);
	// _gfx->computeScreenViewport();

	while (!shouldQuit()) {
		// _gfx->clear();
		// _gfx->drawRect2D(Common::Rect(1920, 1080), 0xFF0000);
		// _gfx->drawTexturedRect2D(_gfx->viewport(), tmxRect, texturetmx, -0.5, false);
		// _gfx->drawTexturedRect2D(_gfx->viewport(), ddsRect, texturedds, -.5, false);
		// _gfx->flipBuffer();
		Graphics::Surface *screen = g_system->lockScreen();
		surfacetmx->blit(*screen);
		g_system->unlockScreen();
		g_system->updateScreen();
		g_system->getEventManager()->pollEvent(e);
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace SMT
