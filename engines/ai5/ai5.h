/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
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

#ifndef AI5_AI5_H
#define AI5_AI5_H

#include "common/random.h"
#include "engines/engine.h"

#include "ai5/detection.h"

namespace AI5 {

class AI5Engine : public Engine {
public:
	AI5Engine(OSystem *syst, const AI5GameDescription *gameDesc);

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	Common::RandomSource &getRandom() { return _rnd; }

private:
	const AI5GameDescription *_gameDescription;
	Common::RandomSource _rnd;
};

extern AI5Engine *g_ai5;

} // End of namespace AI5

#endif // AI5_AI5_H
