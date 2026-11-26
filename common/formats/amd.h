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

#ifndef COMMON_FORMATS_AMD_H
#define COMMON_FORMATS_AMD_H

#include "common/array.h"
#include "common/formats/gmo.h"
#include "common/stream.h"

namespace Common {

/** Persona 4 Golden CHNK container carrying a GMO model. */
class AMD {
public:
	AMD(SeekableReadStream *stream);
	~AMD();

	bool ok() const { return _ok; }
	uint32 chunkCount() const { return _chunkCount; }
	GMO *getModel() { return _model; }

private:
	Common::Array<byte> _data;
	GMO *_model = nullptr;
	bool _ok = false;
	uint32 _chunkCount = 0;

	uint32 read32(const byte *p) const;
};

} // namespace Common

#endif
