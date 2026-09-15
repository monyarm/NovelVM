#include "ikura/ikura.h"
#include "engines/advancedDetector.h"

class IkuraMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override {
		return "ikura";
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
};

Common::Error IkuraMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new Ikura::IkuraEngine(syst, desc);
	return Common::kNoError;
}

bool IkuraMetaEngine::hasFeature(MetaEngineFeature f) const {
	// Save/load support lands in Task 6; nothing to report yet.
	return false;
}

#if PLUGIN_ENABLED_DYNAMIC(IKURA)
REGISTER_PLUGIN_DYNAMIC(IKURA, PLUGIN_TYPE_ENGINE, IkuraMetaEngine);
#else
REGISTER_PLUGIN_STATIC(IKURA, PLUGIN_TYPE_ENGINE, IkuraMetaEngine);
#endif
