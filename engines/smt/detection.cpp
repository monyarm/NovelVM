#include "smt/smt.h"

#include "base/plugins.h"

#include "engines/advancedDetector.h"

namespace SMT {
const char *SMTEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform SMTEngine::getPlatform() const { return _gameDescription->platform; }
}

static const PlainGameDescriptor SMTGames[] = {
	{"P3", "Persona 3", "file_formats", "https://megamitensei.fandom.com/wiki/Persona_3"},
	{"P3F", "Persona 3 FES", "file_formats", "https://megamitensei.fandom.com/wiki/Persona_3_FES"},
	{"P4", "Persona 4", "file_formats", "https://megamitensei.fandom.com/wiki/Persona_4"},
	{"P4G", "Persona 4 Golden", "file_formats","https://megamitensei.fandom.com/wiki/Persona_4_Golden"},
	{"P5", "Persona 5", "file_formats","https://megamitensei.fandom.com/wiki/Persona_5"},
	{"P5R", "Persona 5 Royal", "file_formats","https://megamitensei.fandom.com/wiki/Persona_5_Royal"},
	{"P3P", "P3P: Persona 3 Portable", "file_formats","https://megamitensei.fandom.com/wiki/Persona_3_Portable"},
	{0, 0, 0, 0}
};

namespace SMT {

static const ADGameDescription gameDescriptions[] = {
	{
		"P3P",
		0,
		AD_ENTRY1s("umd0.cpk", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPSP,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P3",
		0,
		AD_ENTRY1s("DATA.CVM", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P3F",
		0,
		AD_ENTRY1s("DATA.CVM", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P4",
		0,
		AD_ENTRY1s("DATA.CVM", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P4G",
		0,
		AD_ENTRY1s("data.cpk", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P5",
		0,
		AD_ENTRY1s("data.cpk", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P5R",
		0,
		AD_ENTRY1s("data.cpk", NULL, -1),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace SMT

class SMTMetaEngine : public AdvancedMetaEngine {
public:
	SMTMetaEngine() : AdvancedMetaEngine(SMT::gameDescriptions, sizeof(ADGameDescription), SMTGames) {
	}

	const char *getEngineId() const override {
		return "SMT";
	}

	const char *getName() const override {
		return "Shin Megami Tensei";
	}

	const char *getOriginalCopyright() const override {
		return "Shin Megami Tensei (C) Atlus";
	}

	bool hasFeature(MetaEngineFeature f) const override;
	bool createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
};

bool SMTMetaEngine::hasFeature(MetaEngineFeature f) const {
	return false;
}

bool SMTMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	if (desc)
		*engine = new SMT::SMTEngine(syst, desc);

	return desc != nullptr;
}

#if PLUGIN_ENABLED_DYNAMIC(SMT)
REGISTER_PLUGIN_DYNAMIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#else
REGISTER_PLUGIN_STATIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#endif
