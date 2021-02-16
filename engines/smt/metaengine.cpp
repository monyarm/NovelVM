#include "smt/smt.h"
#include "engines/advancedDetector.h"

class SMTMetaEngine : public AdvancedMetaEngine {
public:
	const char *getName() const override
	{
		return "Shin Megami Tensei";
	}
    
    Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
};

Common::Error SMTMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new SMT::SMTEngine(syst, desc);
	return Common::kNoError;
}


bool SMTMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return false;
}

#if PLUGIN_ENABLED_DYNAMIC(SMT)
REGISTER_PLUGIN_DYNAMIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#else
REGISTER_PLUGIN_STATIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#endif