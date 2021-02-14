#include "koihime_doki/koihime_doki.h"

#include "base/plugins.h"

#include "engines/advancedDetector.h"

namespace KoihimeDoki
{
const char *KoihimeDokiEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform KoihimeDokiEngine::getPlatform() const { return _gameDescription->platform; }
} // namespace KoihimeDoki

static const PlainGameDescriptor KoihimeDokiGames[] = {
	{"KM", "Koihime † Musou ~Doki ☆ Otome Darake no Sangokushi Engi~", "File Formats", "https://vndb.org/v369"},
	{0, 0, 0, 0}};

namespace KoihimeDoki
{

static const ADGameDescription gameDescriptions[] = {
	{"KM",
	 0,
	 AD_ENTRY1s("koihime.ico", NULL, -1),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},

	AD_TABLE_END_MARKER};

} // End of namespace KoihimeDoki

class KoihimeDokiMetaEngine : public AdvancedMetaEngine
{
public:
	KoihimeDokiMetaEngine() : AdvancedMetaEngine(KoihimeDoki::gameDescriptions, sizeof(ADGameDescription), KoihimeDokiGames)
	{
	}

	const char *getEngineId() const override
	{
		return "KoihimeDoki";
	}

	const char *getName() const override
	{
		return "Koihime Musou";
	}

	const char *getOriginalCopyright() const override
	{
		return "";
	}

	bool hasFeature(MetaEngineFeature f) const override;
	bool createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
};

bool KoihimeDokiMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return false;
}

bool KoihimeDokiMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const
{
	if (desc)
		*engine = new KoihimeDoki::KoihimeDokiEngine(syst, desc);

	return desc != nullptr;
}

#if PLUGIN_ENABLED_DYNAMIC(KOIHIME_DOKI)
REGISTER_PLUGIN_DYNAMIC(KOIHIME_DOKI, PLUGIN_TYPE_ENGINE, KoihimeDokiMetaEngine);
#else
REGISTER_PLUGIN_STATIC(KOIHIME_DOKI, PLUGIN_TYPE_ENGINE, KoihimeDokiMetaEngine);
#endif
