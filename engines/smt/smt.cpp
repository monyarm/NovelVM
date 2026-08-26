#include "audio/mixer.h"
#include "common/list.h"
#include "common/formats/cpk.h"
#include "common/formats/cvm.h"
#include "common/formats/adx.h"
#include "common/formats/dds.h"
#include "common/formats/pmsf.h"
#include "graphics/surface.h"
#include "smt/formats/archive/pac.h"
#include "smt/formats/graphic/tmx.h"
#include "smt/formats/script/bmd.h"

#include "smt/smt.h"
#include "util.h"



namespace {

/**
 * Alpha-blend a 32-bit RGBA source surface onto the destination surface,
 * honoring the destination's actual pixel format. Replaces the removed
 * Graphics::TransparentSurface::blit().
 */
void blitAlphaSurface(const Graphics::Surface *src, Graphics::Surface *dst) {
	if (!src || !dst || !src->getPixels() || !dst->getPixels())
		return;

	const Graphics::PixelFormat &sf = src->format;
	const Graphics::PixelFormat &df = dst->format;

	for (uint16 y = 0; y < src->h && y < dst->h; ++y) {
		const byte *srcRow = (const byte *)src->getBasePtr(0, y);
		byte *dstRow = (byte *)dst->getBasePtr(0, y);

		for (uint16 x = 0; x < src->w && x < dst->w; ++x) {
			uint32 spix = 0;
			memcpy(&spix, srcRow + x * sf.bytesPerPixel, sf.bytesPerPixel);

			uint8 r = ((spix >> sf.rShift) << sf.rLoss) & 0xFF;
			uint8 g = ((spix >> sf.gShift) << sf.gLoss) & 0xFF;
			uint8 b = ((spix >> sf.bShift) << sf.bLoss) & 0xFF;
			uint8 a = sf.aLoss == 8 ? 255 :
			          (((spix >> sf.aShift) << sf.aLoss) & 0xFF);

			byte *dstPix = dstRow + x * df.bytesPerPixel;
			if (a >= 255 || df.bytesPerPixel < 2) {
				uint32 color = df.RGBToColor(r, g, b);
				memcpy(dstPix, &color, df.bytesPerPixel);
				continue;
			}
			if (a == 0)
				continue;

			uint32 dpix = 0;
			memcpy(&dpix, dstPix, df.bytesPerPixel);
			uint8 dr, dg, db;
			df.colorToRGB(dpix, dr, dg, db);

			dr = (r * a + dr * (255 - a)) / 255;
			dg = (g * a + dg * (255 - a)) / 255;
			db = (b * a + db * (255 - a)) / 255;
			uint32 color = df.RGBToColor(dr, dg, db);
			memcpy(dstPix, &color, df.bytesPerPixel);
		}
	}
}

} // anonymous namespace

namespace SMT {


SMTEngine::SMTEngine(OSystem *syst, const ADGameDescription *desc)
    : Engine(syst), _gameDescription(desc), _console(nullptr)//, _gfx(0)
	{
	// Put your engine in a sane state, but do nothing big yet;
	// in particular, do not load data from files; rather, if you
	// need to do such things, do them from run().

	// Do not initialize graphics here
	// Do not initialize audio devices here

	// However this is the place to specify all default directories
	const Common::FSNode gameDataDir(Common::Path(ConfMan.get("path")));
	//SearchMan.addSubDirectoryMatching(gameDataDir, "sound/pmsf");

	// Don't forget to register your random source
	_rnd = new Common::RandomSource("smt");

	debug("SMTEngine::SMTEngine");
}

SMTEngine::~SMTEngine() {
	debug("SMTEngine::~SMTEngine");

	// Dispose your resources here
	delete _rnd;
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

	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	//for (auto &&l : list)
	{
		//debug(l.get()->getName().c_str());
	}
	list = Common::ArchiveMemberList();
	Common::File f;

	/*
	if (!f.open("bgm01.wav", *_archives["STREAM.PAK"].get()))
	{
		error("can't read archive");
	} */

	//CVMArchive _data("DATA.CVM");
	Format::Archive::PAC _data("test/DATMSG.PAK");
	_data.listMembers(list);
	for (auto &&l : list) {
		debug("%s", l.get()->getName().c_str());
	}
	//Common::DumpFile df;
	//df.open("dumps/i_bust_02_61.tmx");
	auto _dfile = _data.createReadStreamForMember("i_bust_02_61.tmx");
	//df.writeStream(_dfile);

	Format::Graphic::TMX _tmx("test/COIN_C10.TMX");
	Format::Script::BMD _bmd("test/field.BMD");
	Common::ADX _adx("test/THEME.ADX");

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

	Graphics::Surface *surfacetmx = _tmx.getSurface();
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
		blitAlphaSurface(surfacetmx, screen);
		g_system->unlockScreen();
		g_system->updateScreen();
		g_system->getEventManager()->pollEvent(e);
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace SMT
