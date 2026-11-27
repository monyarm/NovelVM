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

#ifndef AI5_DETECTION_H
#define AI5_DETECTION_H

#include "engines/advancedDetector.h"

namespace AI5 {

enum GameId {
	GID_ISAKU,
	GID_YUNO,
	GID_DOUKYUUSEI,
	GID_DOUKYUUSEI2,
	GID_KAKYUUSEI,
	GID_SHUUSAKU,
	GID_AISHIMAI,
	GID_BEYOND,
	GID_SHANGRLIA,
	GID_KAWARAZAKIKE,
	GID_ALLSTARS,
	GID_AI5_FALLBACK
};

struct AI5GameDescription {
	AD_GAME_DESCRIPTION_HELPERS(desc);

	ADGameDescription desc;
	GameId gameId;
};

} // End of namespace AI5

#endif // AI5_DETECTION_H
