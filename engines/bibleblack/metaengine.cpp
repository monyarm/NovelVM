#include "bibleblack/bibleblack.h"
#include "engines/advancedDetector.h"

class BibleBlackMetaEngine : public AdvancedMetaEngine {
public:

	const char *getName() const override
	{
		return "Bible Black";
	}
    
    Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
};

Common::Error BibleBlackMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new BibleBlack::BibleBlackEngine(syst, desc);
	return Common::kNoError;
}


bool BibleBlackMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return false;
}

#if PLUGIN_ENABLED_DYNAMIC(BIBLEBLACK)
REGISTER_PLUGIN_DYNAMIC(BIBLEBLACK, PLUGIN_TYPE_ENGINE, BibleBlackMetaEngine);
#else
REGISTER_PLUGIN_STATIC(BIBLEBLACK, PLUGIN_TYPE_ENGINE, BibleBlackMetaEngine);
#endif