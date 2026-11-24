#include "common/debug_demo_view.h"

namespace Common {

DebugDemoView::Phase DebugDemoView::nextPhase(Phase current) {
	static const Phase kPhaseOrder[] = { kPhaseVideo, kPhaseTextureSong, kPhaseModel, kPhaseDone };
	for (uint i = 0; i + 1 < ARRAYSIZE(kPhaseOrder); ++i)
		if (kPhaseOrder[i] == current)
			return kPhaseOrder[i + 1];
	return kPhaseDone;
}

DebugDemoView::Action DebugDemoView::classify(const Common::Event &e, int &dir) const {
	dir = 0;
	if (e.type == Common::EVENT_QUIT)
		return kActionQuit;
	if (e.type != Common::EVENT_KEYDOWN)
		return kActionNone;
	if (e.kbd.keycode == Common::KEYCODE_SPACE)
		return kActionSkip;
	if (e.kbd.keycode != Common::KEYCODE_UP && e.kbd.keycode != Common::KEYCODE_DOWN)
		return kActionNone;
	dir = (e.kbd.keycode == Common::KEYCODE_DOWN) ? 1 : -1;
	if (_phase == kPhaseModel)
		return kActionCycleModel;
	if (_phase == kPhaseTextureSong)
		return kActionCycleTextureBgm;
	dir = 0;
	return kActionNone;
}

} // End of namespace Common
