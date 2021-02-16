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

class BibleBlackMetaEngineDetection : public AdvancedMetaEngineDetection
{
public:
	BibleBlackMetaEngineDetection() : AdvancedMetaEngineDetection(BibleBlack::gameDescriptions, sizeof(ADGameDescription), BibleBlackGames)
	{
	}


	const char *getOriginalCopyright() const override
	{
		return "";
	}
	const char *getEngineId() const override
	{
		return "BibleBlack";
	}

	const char *getName() const override
	{
		return "Bible Black";
	}
};

REGISTER_PLUGIN_STATIC(BIBLEBLACK_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, BibleBlackMetaEngineDetection);
