/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

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
