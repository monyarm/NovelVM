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

#include "common/formats/amd.h"

#include "common/memstream.h"

namespace Common {

AMD::AMD(SeekableReadStream *stream) {
	if (!stream)
		return;
	uint32 size = (uint32)(stream->size() - stream->pos());
	if (size < 0x20)
		return;
	_data.resize(size);
	stream->read(_data.data(), size);
	if (memcmp(_data.data(), "CHNK", 4) != 0)
		return;

	_chunkCount = read32(_data.data() + 4);
	uint32 pos = 8;
	for (uint32 i = 0; i < _chunkCount && pos + 24 <= size; ++i) {
		const byte *header = _data.data() + pos;
		uint32 chunkSize = read32(header + 20);
		pos += 24;
		if (chunkSize > size - pos)
			return;
		if (memcmp(header, "MODEL_DATA", 10) == 0 && !_model) {
			MemoryReadStream modelStream(_data.data() + pos, chunkSize);
			_model = new GMO(&modelStream);
			if (!_model->ok())
				return;
		}
		pos += chunkSize;
	}
	_ok = _model != nullptr;
}

AMD::~AMD() {
	delete _model;
}

uint32 AMD::read32(const byte *p) const {
	return p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

} // namespace Common
