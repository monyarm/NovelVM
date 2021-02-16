#include "koihime_doki/koihime_doki.h"
#include "engines/advancedDetector.h"

class KoihimeDokiMetaEngine : public AdvancedMetaEngine {
public:

	
	const char *getName() const override
	{
		return "Koihime Musou";
	}
    
    Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
};

Common::Error KoihimeDokiMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new KoihimeDoki::KoihimeDokiEngine(syst, desc);
	return Common::kNoError;
}


bool KoihimeDokiMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return false;
}

#if PLUGIN_ENABLED_DYNAMIC(KOIHIME_DOKI)
REGISTER_PLUGIN_DYNAMIC(KOIHIME_DOKI, PLUGIN_TYPE_ENGINE, KoihimeDokiMetaEngine);
#else
REGISTER_PLUGIN_STATIC(KOIHIME_DOKI, PLUGIN_TYPE_ENGINE, KoihimeDokiMetaEngine);
#endif