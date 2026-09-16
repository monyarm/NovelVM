#include "ikura/runtime/font.h"

#include "common/fs.h"
#include "common/hashmap.h"
#include "graphics/fontman.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Ikura::Runtime {

Font::Mode Font::_mode = Font::Mode::kSystem;

namespace {

#ifdef USE_FREETYPE2
struct SystemFontCandidate {
	const char *path;
	const char *family;
};

// First match wins, cached in triedCandidate below so later calls (a
// different size/style combination) don't re-probe every candidate.
// Genuinely absent on the target machine just falls through to the
// built-in font - never a hard requirement.
const SystemFontCandidate kSystemFontCandidates[] = {
	// Linux
	{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans"},
	{"/usr/share/fonts/dejavu/DejaVuSans.ttf", "DejaVu Sans"},
	{"/usr/share/fonts/TTF/DejaVuSans.ttf", "DejaVu Sans"},
	{"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "Liberation Sans"},
	// Windows
	{"C:\\Windows\\Fonts\\arial.ttf", "Arial"},
	// macOS
	{"/System/Library/Fonts/Helvetica.ttc", "Helvetica"},
	{"/Library/Fonts/Arial.ttf", "Arial"},
};
const int kNoCandidateFound = -2;
const int kNotProbedYet = -1;
int triedCandidate = kNotProbedYet;

int combineKey(int size, bool bold, bool italic) {
	return (size << 2) | (bold ? 2 : 0) | (italic ? 1 : 0);
}

Common::HashMap<int, const Graphics::Font *> ttfCache;

const Graphics::Font *loadSystemTTF(int size, bool bold, bool italic) {
	if (triedCandidate == kNoCandidateFound)
		return nullptr;

	if (triedCandidate == kNotProbedYet) {
		triedCandidate = kNoCandidateFound;
		for (uint i = 0; i < ARRAYSIZE(kSystemFontCandidates); i++) {
			if (Common::FSNode(kSystemFontCandidates[i].path).exists()) {
				triedCandidate = (int)i;
				break;
			}
		}
		if (triedCandidate == kNoCandidateFound)
			return nullptr;
	}

	const SystemFontCandidate &candidate = kSystemFontCandidates[triedCandidate];
	Common::Array<Common::Path> files;
	files.push_back(Common::Path(candidate.path));
	return Graphics::findTTFace(files, Common::U32String(candidate.family), bold, italic, size);
}
#endif

} // namespace

const Graphics::Font *Font::get(int size, bool bold, bool italic) {
	if (size <= 0)
		size = 16;

#ifdef USE_FREETYPE2
	int key = combineKey(size, bold, italic);
	if (!ttfCache.contains(key)) {
		ttfCache[key] = loadSystemTTF(size, bold, italic);
	}
	if (ttfCache[key])
		return ttfCache[key];
#endif

	return FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
}

} // End of namespace Ikura::Runtime
