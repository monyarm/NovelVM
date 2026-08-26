#include "smt/smt.h"
#include "engines/advancedDetector.h"

namespace SMT {
const char *SMTEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform SMTEngine::getPlatform() const { return _gameDescription->platform; }
}

static const PlainGameDescriptor SMTGames[] = {
	{"P3", "Persona 3", "File Formats", "https://megamitensei.fandom.com/wiki/Persona_3"},
	{"P3F", "Persona 3 FES", "File Formats", "https://megamitensei.fandom.com/wiki/Persona_3_FES"},
	{"P4", "Persona 4", "File Formats", "https://megamitensei.fandom.com/wiki/Persona_4"},
	{"P4G", "Persona 4 Golden", "File Formats","https://megamitensei.fandom.com/wiki/Persona_4_Golden"},
	{"P5", "Persona 5", "File Formats","https://megamitensei.fandom.com/wiki/Persona_5"},
	{"P5R", "Persona 5 Royal", "File Formats","https://megamitensei.fandom.com/wiki/Persona_5_Royal"},
	{"P3P", "P3P: Persona 3 Portable", "File Formats","https://megamitensei.fandom.com/wiki/Persona_3_Portable"},
	{0, 0, 0, 0}
};

namespace SMT {

static const ADGameDescription gameDescriptions[] = {
	{
		"P3P",
		0,
		AD_ENTRY1s("umd0.cpk", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPSP,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P3",
		0,
		AD_ENTRY1s("DATA.CVM", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P3F",
		0,
		AD_ENTRY1s("DATA.CVM", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P4",
		0,
		AD_ENTRY1s("DATA.CVM", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPS2,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P4G",
		0,
		AD_ENTRY1s("data.cpk", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P5",
		0,
		AD_ENTRY1s("data.cpk", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"P5R",
		0,
		AD_ENTRY1s("data.cpk", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformPSVita,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace SMT

class SMTMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	SMTMetaEngineDetection() : AdvancedMetaEngineDetection(SMT::gameDescriptions, SMTGames) {
	}

	const char *getName() const override {
		return "SMT";
	}

	const char *getEngineName() const override {
		return "Shin Megami Tensei";
	}

	const char *getOriginalCopyright() const override {
		return "Shin Megami Tensei (C) Atlus";
	}
};

REGISTER_PLUGIN_STATIC(SMT_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, SMTMetaEngineDetection);
