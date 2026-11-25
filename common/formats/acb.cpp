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

#include "common/formats/acb.h"

#include "common/file.h"
#include "common/formats/awb.h"
#include "common/formats/utf_table.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

// Read a nested @UTF table from a vldata column's data segment.
// Returns true on success and fills `table`.
bool readNestedUTF(const UTFTable &parent, const UTFValue &vldata, UTFTable &table) {
	if (!vldata.valid || vldata.dataLength == 0)
		return false;

	const byte *seg = parent.dataSegment();
	uint32 segSize = parent.dataSegmentSize();
	if (vldata.dataOffset > segSize || vldata.dataLength > segSize - vldata.dataOffset)
		return false;

	return table.parse(seg + vldata.dataOffset, vldata.dataLength);
}

} // namespace

ACB::ACB(const Common::String &filename) : _filename(filename), _awb(nullptr) {
	load();
}

ACB::ACB(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeStream,
         Common::SeekableReadStream *awbStream, DisposeAfterUse::Flag disposeAWB)
	: _awb(nullptr) {
	Common::DisposablePtr<Common::SeekableReadStream> streamGuard(stream, disposeStream);
	loadFromStream(*stream, awbStream, disposeAWB);
}

ACB::~ACB() {
}

bool ACB::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("ACB::ACB(): Could not open %s", _filename.c_str());
		return false;
	}
	return loadFromStream(f, nullptr, DisposeAfterUse::NO);
}

bool ACB::loadFromStream(Common::SeekableReadStream &f, Common::SeekableReadStream *awbStream, DisposeAfterUse::Flag disposeAWB) {
	// ACB is a single @UTF table — read the entire file into a buffer.
	int64 fileSize = f.size();
	if (fileSize <= 0 || fileSize > 0x10000000) {
		warning("ACB::loadFromStream(): implausible file size in %s", _filename.c_str());
		return false;
	}

	Common::Array<byte> buf((uint32)fileSize);
	if (f.read(buf.data(), (uint32)fileSize) != (uint32)fileSize) {
		warning("ACB::loadFromStream(): read failed in %s", _filename.c_str());
		return false;
	}

	UTFTable header;
	if (!header.parse(buf.data(), (uint32)fileSize)) {
		warning("ACB::loadFromStream(): failed to parse @UTF header in %s", _filename.c_str());
		return false;
	}

	if (header.numRows() < 1) {
		warning("ACB::loadFromStream(): no rows in header table in %s", _filename.c_str());
		return false;
	}

	// --- Resolve the AWB: explicit stream, embedded AwbFile, or external .awb ---
	if (awbStream)
		_awb.reset(new AWB(awbStream, disposeAWB));
	else {
		// --- AwbFile: embedded AWB data (or empty for external) ---
		UTFValue awbFileVldata = header.get(0, "AwbFile");
		if (awbFileVldata.valid && awbFileVldata.dataLength > 0) {
			const byte *seg = header.dataSegment();
			uint32 segSize = header.dataSegmentSize();
			if (awbFileVldata.dataOffset <= segSize &&
				awbFileVldata.dataLength <= segSize - awbFileVldata.dataOffset) {
				// Copy the embedded AWB out of the header table's buffer:
				// `header` (and thus `seg`) dies when this function returns,
				// so the AWB's stream must own a heap copy to stay valid.
				byte *awbData = new byte[awbFileVldata.dataLength];
				memcpy(awbData, seg + awbFileVldata.dataOffset, awbFileVldata.dataLength);
				_awb.reset(new AWB(new Common::MemoryReadStream(awbData, awbFileVldata.dataLength,
				                                                DisposeAfterUse::YES),
				                   DisposeAfterUse::YES));
			}
		}

		if (!_awb && !_filename.empty()) {
			// --- Derive external AWB filename ---
			Common::String awbName;
			int dotPos = _filename.findLastOf('.');
			if (dotPos >= 0)
				awbName = _filename.substr(0, dotPos) + ".awb";
			else
				awbName = _filename + ".awb";

			Common::File probe;
			if (probe.open(Common::Path(awbName))) {
				probe.close();
				_awb.reset(new AWB(awbName));
			} else {
				warning("ACB::loadFromStream(): no AWB found (embedded or external '%s') in %s",
				         awbName.c_str(), _filename.c_str());
			}
		}
	}

	// --- Parse WaveformTable ---
	UTFValue waveformVldata = header.get(0, "WaveformTable");
	if (waveformVldata.valid && waveformVldata.dataLength > 0) {
		UTFTable waveformTable;
		if (readNestedUTF(header, waveformVldata, waveformTable)) {
			_waveforms.resize(waveformTable.numRows());
			for (int i = 0; i < waveformTable.numRows(); i++) {
				_waveforms[i].id = (uint16)waveformTable.get(i, "Id").asUint(0);
				_waveforms[i].encodeType = (uint8)waveformTable.get(i, "EncodeType").asUint(0);
				_waveforms[i].numChannels = (uint8)waveformTable.get(i, "NumChannels").asUint(1);
				_waveforms[i].numSamples = (uint32)waveformTable.get(i, "NumSamples").asUint(0);
				_waveforms[i].loopFlag = (uint8)waveformTable.get(i, "LoopFlag").asUint(0);
				int64 extIdx = waveformTable.get(i, "ExtensionData").asUint(0xFFFF);
				_waveforms[i].extensionDataIndex = (extIdx == 0xFFFF) ? -1 : (int16)extIdx;
			}
		}
	}

	// --- Parse WaveformExtensionDataTable for loop points ---
	UTFValue extDataVldata = header.get(0, "WaveformExtensionDataTable");
	if (extDataVldata.valid && extDataVldata.dataLength > 0) {
		UTFTable extTable;
		if (readNestedUTF(header, extDataVldata, extTable)) {
			for (int i = 0; i < extTable.numRows(); i++) {
				uint32 loopStart = (uint32)extTable.get(i, "LoopStart").asUint(0);
				uint32 loopEnd = (uint32)extTable.get(i, "LoopEnd").asUint(0);
				// Associate loop points with waveforms that reference this extension data
				for (uint32 w = 0; w < _waveforms.size(); w++) {
					if (_waveforms[w].extensionDataIndex == i) {
						_waveforms[w].hasLoop = true;
						_waveforms[w].loopStart = loopStart;
						_waveforms[w].loopEnd = loopEnd;
					}
				}
			}
		}
	}

	// --- Parse CueNameTable ---
	Common::HashMap<uint16, Common::String> cueNames; // CueIndex → name
	UTFValue cueNameVldata = header.get(0, "CueNameTable");
	if (cueNameVldata.valid && cueNameVldata.dataLength > 0) {
		UTFTable cueNameTable;
		if (readNestedUTF(header, cueNameVldata, cueNameTable)) {
			for (int i = 0; i < cueNameTable.numRows(); i++) {
				uint16 cueIndex = (uint16)cueNameTable.get(i, "CueIndex").asUint(0);
				Common::String name = cueNameTable.get(i, "CueName").asString();
				if (!name.empty())
					cueNames[cueIndex] = name;
			}
		}
	}

	// --- Parse CueTable ---
	UTFValue cueTableVldata = header.get(0, "CueTable");
	if (cueTableVldata.valid && cueTableVldata.dataLength > 0) {
		UTFTable cueTable;
		if (readNestedUTF(header, cueTableVldata, cueTable)) {
			_cues.resize(cueTable.numRows());
			for (int i = 0; i < cueTable.numRows(); i++) {
				_cues[i].waveformIndex = (uint16)cueTable.get(i, "ReferenceIndex").asUint(0);
				// Resolve name from CueNameTable
				if (cueNames.contains(i))
					_cues[i].name = cueNames[i];
				else
					_cues[i].name = Common::String::format("cue_%d", i);
			}
		}
	}

	// --- Copy loop info from waveforms to cues ---
	for (uint32 i = 0; i < _cues.size(); i++) {
		uint16 wIdx = _cues[i].waveformIndex;
		if (wIdx < _waveforms.size() && _waveforms[wIdx].hasLoop) {
			_cues[i].hasLoop = true;
			_cues[i].loopStart = _waveforms[wIdx].loopStart;
			_cues[i].loopEnd = _waveforms[wIdx].loopEnd;
		}
		_cueIndex[_cues[i].name] = i;
	}

	return true;
}

bool ACB::hasFile(const Common::Path &name) const {
	return _cueIndex.contains(name.toString());
}

int ACB::listMembers(Common::ArchiveMemberList &list) const {
	for (uint32 i = 0; i < _cues.size(); i++) {
		list.push_back(Common::ArchiveMemberList::value_type(
			new Common::GenericArchiveMember(Common::Path(_cues[i].name), *this)));
	}
	return (int)_cues.size();
}

const Common::ArchiveMemberPtr ACB::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *ACB::createReadStreamForMember(const Common::Path &name) const {
	if (!_awb)
		return nullptr;

	Common::String nameStr = name.toString();
	if (!_cueIndex.contains(nameStr))
		return nullptr;

	const CueInfo &cue = _cues[_cueIndex[nameStr]];
	if (cue.waveformIndex >= _waveforms.size())
		return nullptr;

	const WaveformInfo &wf = _waveforms[cue.waveformIndex];

	// Resolve the waveform by its numeric ID; AWB stores/names entries by
	// that ID but not necessarily as the bare "id_%04u" stem.
	return _awb->createReadStreamForId(wf.id);
}

ACB::LoopInfo ACB::getLoopInfo(const Common::String &cueName) const {
	LoopInfo info;
	if (!_cueIndex.contains(cueName))
		return info;

	const CueInfo &cue = _cues[_cueIndex[cueName]];
	if (cue.hasLoop) {
		info.valid = true;
		info.start = cue.loopStart;
		info.end = cue.loopEnd;
	}
	return info;
}

ACB *makeACBArchive(const Common::String &name) {
	return new ACB(name);
}

} // namespace Common
