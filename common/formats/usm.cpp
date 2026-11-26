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

#include "common/formats/usm.h"

#include "common/file.h"
#include "common/formats/utf_table.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

// Real chunk sub-header, immediately after the 8-byte tag+size: 1 unknown
// byte, payload_offset (byte, counted from the tag start), padding_size
// (u16BE), stream_index (byte), 2 unknown bytes, payload_type (byte: 0 =
// data, 1 = @UTF metadata), then 16 more bytes (frame_time/frame_rate/
// reserved) unused here. Verified against real mov050.usm chunks -- e.g.
// the file's own CRID chunk has payload_offset=24, landing its @UTF table
// at 8 (tag+size) + 24 = offset 0x20, matching the real hex dump.
const uint32 kChunkSubHeaderSize = 24;

struct ChunkHeader {
	char tag[5] = {0};
	uint32 size = 0;
	byte payloadOffset = 0;
	uint16 paddingSize = 0;
	byte streamIndex = 0;
	byte payloadType = 0;
};

bool readChunkHeader(Common::SeekableReadStream &f, ChunkHeader &out) {
	if (f.read(out.tag, 4) != 4)
		return false;
	out.tag[4] = '\0';
	out.size = f.readUint32BE();
	if (f.eos() || out.size == 0)
		return false;

	int64 dataStart = f.pos();
	out.payloadOffset = 0;
	out.paddingSize = 0;
	out.streamIndex = 0;
	out.payloadType = 0;

	if (!strcmp(out.tag, "@SFV") || !strcmp(out.tag, "@SFA") ||
	    !strcmp(out.tag, "@ALP") || !strcmp(out.tag, "@SBT") ||
	    !strcmp(out.tag, "CRID")) {
		f.readByte(); // unknown
		out.payloadOffset = f.readByte();
		out.paddingSize = f.readUint16BE();
		out.streamIndex = f.readByte();
		f.skip(2); // unknown
		out.payloadType = f.readByte();
	}

	return f.seek(dataStart + out.size);
}

// Reads chunk `header`'s payload (metadata or data) back out, given the
// stream is currently positioned right after the chunk's 8-byte tag+size.
Common::Array<byte> readChunkPayload(Common::SeekableReadStream &f, int64 chunkDataStart, const ChunkHeader &header) {
	uint32 payloadLen = header.size >= (uint32)header.payloadOffset + header.paddingSize
	                         ? header.size - header.payloadOffset - header.paddingSize
	                         : 0;
	Common::Array<byte> result(payloadLen);
	if (payloadLen == 0)
		return result;
	f.seek(chunkDataStart + header.payloadOffset);
	if (f.read(result.data(), payloadLen) != payloadLen)
		result.clear();
	return result;
}

void parseVideoMetadata(const UTFTable &t, USMVideoInfo &info) {
	info.width = (uint16)t.get(0, "width").asUint(0);
	info.height = (uint16)t.get(0, "height").asUint(0);
	info.totalFrames = (uint32)t.get(0, "total_frames").asUint(0);
	info.framerateNum = (uint32)t.get(0, "framerate_n").asUint(0);
	info.framerateDen = (uint32)t.get(0, "framerate_d").asUint(1);
	info.codec = (USMVideoInfo::Codec)t.get(0, "mpeg_codec").asUint(0);
	info.valid = true;
}

void parseAudioMetadata(const UTFTable &t, USMAudioInfo &info) {
	info.sampleRate = (uint32)t.get(0, "sampling_rate").asUint(0);
	info.channels = (byte)t.get(0, "num_channels").asUint(0);
	info.totalSamples = (uint32)t.get(0, "total_samples").asUint(0);
	info.codec = (USMAudioInfo::Codec)t.get(0, "audio_codec").asUint(0);
	info.valid = true;
}

} // namespace

USM::USM(const Common::String &filename) : _filename(filename) {
	load();
}

bool USM::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("USM::USM(): Could not open %s", _filename.c_str());
		return false;
	}

	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "CRID", 4) != 0) {
		warning("USM::USM(): '%s' is not a USM file", _filename.c_str());
		return false;
	}
	f.seek(0);

	bool haveVideoMeta = false, haveAudioMeta = false;

	while (!f.eos()) {
		int64 chunkStart = f.pos();
		ChunkHeader header;
		if (!readChunkHeader(f, header))
			break;
		int64 chunkDataStart = chunkStart + 8;

		bool isVideo = !strcmp(header.tag, "@SFV");
		bool isAudio = !strcmp(header.tag, "@SFA");
		if ((!isVideo && !isAudio) || header.streamIndex != 0)
			continue;

		if (header.payloadType == 1) {
			Common::Array<byte> payload = readChunkPayload(f, chunkDataStart, header);
			UTFTable table;
			if (payload.size() >= 4 && table.parse(payload.data(), payload.size())) {
				if (isVideo && !haveVideoMeta) {
					parseVideoMetadata(table, _videoInfo);
					haveVideoMeta = true;
				} else if (isAudio && !haveAudioMeta) {
					parseAudioMetadata(table, _audioInfo);
					haveAudioMeta = true;
				}
			}
		} else if (isAudio && header.payloadType == 0) {
			// Real on-disk ADX data arrives exactly as a standalone .adx
			// file would be laid out: the first data chunk after the
			// metadata chunk is the ADX header, every later one is raw
			// frame data -- concatenating them all in file order reproduces
			// that file byte-for-byte.
			Common::Array<byte> payload = readChunkPayload(f, chunkDataStart, header);
			_audioData.push_back(payload);
		}

		// readChunkPayload() re-seeks into the chunk just consumed by
		// readChunkHeader() to pull the payload back out; re-sync to the
		// real end of this chunk before the next iteration reads a header.
		f.seek(chunkDataStart + header.size);
	}

	return _videoInfo.valid || _audioInfo.valid;
}

Common::SeekableReadStream *USM::createAudioStream() const {
	if (_audioInfo.codec != USMAudioInfo::kCodecADX || _audioData.empty())
		return nullptr;

	byte *data = new byte[_audioData.size()];
	memcpy(data, _audioData.data(), _audioData.size());
	return new Common::MemoryReadStream(data, _audioData.size(), DisposeAfterUse::YES);
}

} // namespace Common
