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

#ifndef COMMON_OFFSETTO
#define COMMON_OFFSETTO

#include "scummsys.h"
namespace Common {
template<typename T>
class OffsetTo {
public:
	uint32 Offset;
	T Value;

	OffsetTo() {
		Offset = 0;
	}

	OffsetTo(uint32 offset) {
		Offset = offset;
	}

	OffsetTo(uint32 address, T value) {
		Offset = address;
		Value = value;
	}
};

} // namespace Common

#endif
