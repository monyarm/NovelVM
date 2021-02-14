#include "bibleblack/bibleblack.h"

#include "base/plugins.h"

#include "engines/advancedDetector.h"

namespace BibleBlack
{
const char *BibleBlackEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform BibleBlackEngine::getPlatform() const { return _gameDescription->platform; }
} // namespace BibleBlack

static const PlainGameDescriptor BibleBlackGames[] = {
	{"BBLNdW", "Bible Black -La Noche de Walpurgis-", "File Formats", "https://vndb.org/v9"},
	{"BBTI", "Bible Black -The Infection-", "File Formats", "https://vndb.org/v1030"},
	{0, 0, 0, 0}};

namespace BibleBlack
{

static const ADGameDescription gameDescriptions[] = {
	{"BBLNdW",
	 0,
	 AD_ENTRY1s("A98SYS.PAK", NULL, -1),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},
	{"BBTI",
	 0,
	 AD_ENTRY1s("A98SYS.PAK", NULL, -1),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},

	AD_TABLE_END_MARKER};

} // End of namespace BibleBlack

class BibleBlackMetaEngine : public AdvancedMetaEngine
{
public:
	BibleBlackMetaEngine() : AdvancedMetaEngine(BibleBlack::gameDescriptions, sizeof(ADGameDescription), BibleBlackGames)
	{
	}

	const char *getEngineId() const override
	{
		return "BibleBlack";
	}

	const char *getName() const override
	{
		return "Bible Black";
	}

	const char *getOriginalCopyright() const override
	{
		return "Shin Megami Tensei (C) Atlus";
	}

	bool hasFeature(MetaEngineFeature f) const override;
	bool createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
};

bool BibleBlackMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return false;
}

bool BibleBlackMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const
{
	if (desc)
		*engine = new BibleBlack::BibleBlackEngine(syst, desc);

	return desc != nullptr;
}

#if PLUGIN_ENABLED_DYNAMIC(BIBLEBLACK)
REGISTER_PLUGIN_DYNAMIC(BIBLEBLACK, PLUGIN_TYPE_ENGINE, BibleBlackMetaEngine);
#else
REGISTER_PLUGIN_STATIC(BIBLEBLACK, PLUGIN_TYPE_ENGINE, BibleBlackMetaEngine);
#endif
