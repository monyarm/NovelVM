#include "common/formats/pmf.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/substream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

const uint32 kMpegOffsetOffset = 0x08;
const uint32 kMpegSizeOffset = 0x0C;
const uint32 kFirstTimestampOffset = 0x54; // 6 bytes; must be 90000 (1s @ 90kHz) for a valid file
const uint32 kNumStreamsOffset = 0x80;
const uint32 kStreamTableOffset = 0x82;
const uint32 kStreamEntrySize = 16;

const byte kVideoStreamId = 0xE0; // 0xE0-0xEF range
const byte kAudioStreamId = 0xBD; // private_stream_1

uint64 readUint48BE(Common::SeekableReadStream &s) {
	uint64 v = 0;
	for (int i = 0; i < 6; i++)
		v = (v << 8) | s.readByte();
	return v;
}

} // namespace

PMF::PMF(const Common::String &filename) : _filename(filename) {
	load();
}

bool PMF::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("PMF::PMF(): Could not open %s", _filename.c_str());
		return false;
	}

	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "PSMF", 4) != 0) {
		warning("PMF::PMF(): '%s' is not a PSMF/PMF file", _filename.c_str());
		return false;
	}

	f.seek(kMpegOffsetOffset);
	_mpegOffset = f.readUint32BE();
	f.seek(kMpegSizeOffset);
	_mpegSize = f.readUint32BE();

	f.seek(kFirstTimestampOffset);
	uint64 firstTimestamp = readUint48BE(f);
	if (firstTimestamp != 90000) {
		warning("PMF::PMF(): %s has an implausible first timestamp %llu (expected 90000)", _filename.c_str(), (unsigned long long)firstTimestamp);
		return false;
	}

	f.seek(kNumStreamsOffset);
	uint16 numStreams = f.readUint16BE();
	if (f.err() || f.eos() || numStreams == 0 || numStreams > 32) {
		warning("PMF::PMF(): implausible stream count in %s", _filename.c_str());
		return false;
	}

	for (uint16 i = 0; i < numStreams; i++) {
		f.seek(kStreamTableOffset + i * kStreamEntrySize);
		PMFStream stream;
		stream.streamId = f.readByte();
		stream.subStreamId = f.readByte();
		byte typeFlag1 = f.readByte();
		stream.codecFlag = f.readByte(); // per-type codec byte

		if ((stream.streamId & 0xF0) == kVideoStreamId) {
			stream.isVideo = true;
			stream.epMapOffset = f.readUint32BE();
			stream.epMapCount = f.readUint32BE();
			stream.width = (uint16)f.readByte() * 16;
			stream.height = (uint16)f.readByte() * 16;
			if (_width == 0) {
				_width = stream.width;
				_height = stream.height;
			}
		} else if (stream.streamId == kAudioStreamId) {
			stream.isAudio = true;
			f.seek(kStreamTableOffset + i * kStreamEntrySize + 14);
			stream.channelConfig = f.readByte();
			stream.sampleFreqCode = f.readByte();
		}

		_streams.push_back(stream);
	}

	// TODO: the EP map is parsed but not used for seeking, and multi-video-stream PMFs are
	// unverified (every real sample has one video stream).
	// Real EP map for the first video stream, if any (JPCSP/PPSSPP: 10
	// bytes/entry -- 1 index + 1 pic offset + 4 PTS + 4 relative offset).
	for (uint32 i = 0; i < _streams.size(); i++) {
		if (!_streams[i].isVideo || _streams[i].epMapCount == 0)
			continue;
		f.seek(_streams[i].epMapOffset);
		for (uint32 j = 0; j < _streams[i].epMapCount; j++) {
			PMFEntryPoint ep;
			ep.index = f.readByte();
			ep.picOffset = f.readByte();
			ep.pts = f.readUint32BE();
			ep.offset = f.readUint32BE();
			_videoEntryPoints.push_back(ep);
		}
		break;
	}

	_valid = true;
	return true;
}

Common::SeekableReadStream *PMF::createMpegStream() const {
	if (!_valid)
		return nullptr;

	Common::File *f = new Common::File();
	if (!f->open(Common::Path(_filename))) {
		delete f;
		return nullptr;
	}
	f->seek(_mpegOffset);
	return new Common::SeekableSubReadStream(f, _mpegOffset, _mpegOffset + _mpegSize, DisposeAfterUse::YES);
}

PMF::ATRAC3Audio PMF::extractATRAC3Audio() const {
	ATRAC3Audio result;
	if (!_valid)
		return result;

	// Audio parameters come straight from the PSMF stream table.
	bool hasAudio = false;
	for (uint i = 0; i < _streams.size(); i++) {
		if (!_streams[i].isAudio)
			continue;
		hasAudio = true;
		result.channels = (_streams[i].channelConfig == 1) ? 1 : 2;
		switch (_streams[i].sampleFreqCode) {
		case 0: result.sampleRate = 48000; break;
		case 1: result.sampleRate = 32000; break;
		case 2: result.sampleRate = 44100; break;
		case 3: result.sampleRate = 22050; break;
		case 4: result.sampleRate = 24000; break;
		case 5: result.sampleRate = 11025; break;
		case 6: result.sampleRate = 12000; break;
		default: result.sampleRate = 44100; break;
		}
		break;
	}
	if (!hasAudio)
		return result;

	Common::SeekableReadStream *psFile = createMpegStream();
	if (!psFile)
		return result;
	// The scan below walks every byte of the whole PS stream (tens of MB for
	// a real opening movie) one readByte() call at a time -- on the file
	// stream that's tens of millions of fread(1) calls through several
	// virtual layers, measured at ~20s on a real P3P opening. Bulk-reading
	// into memory first makes each of those a plain array index instead.
	Common::SeekableReadStream *ps = psFile->readStream(psFile->size());
	delete psFile;

	// Concatenate the ATRAC3+ bytes (PSP sub-header stripped) from every
	// private_stream_1 PES payload. Frames may span PES boundaries, so the
	// bytes are collected first and split into frames afterwards.
	Common::Array<byte> es;
	// Array::resize() reallocates to exactly the requested size every call
	// (no growth headroom, see Array::reserve()), so growing `es` by one PES
	// payload at a time below is quadratic in the number of payloads without
	// this upfront reserve. The audio payload can't exceed the whole PS
	// stream, so ps->size() is a safe upper bound.
	es.reserve(ps->size());
	uint32 startCode = 0;
	while (!ps->eos()) {
		int b = ps->readByte();
		if (b < 0)
			break;
		startCode = (startCode << 8) | (byte)b;
		if ((startCode & 0xFFFFFF00) != 0x00000100)
			continue;

		byte streamId = (byte)(startCode & 0xFF);
		if (streamId == 0xBA) {
			// MPEG-2 pack header: 10 bytes after the start code, then
			// (low 3 bits of the last byte) 0xFF stuffing bytes.
			ps->skip(9);
			byte stuff = ps->readByte();
			uint32 stuffing = stuff & 0x07;
			while (stuffing-- > 0)
				ps->readByte();
			continue;
		}
		if (streamId == 0xB9) // MPEG program end
			break;
		// Every other program-level stream is length-prefixed.
		if (ps->eos())
			break;
		uint16 len = ps->readUint16BE();

		if (streamId == 0xBD && len >= 7) {
			// PES header: marker/flags/length, then the payload begins
			// with the 4-byte PSP audio sub-header.
			ps->readByte(); // marker (0x80)
			ps->readByte(); // PES flags
			byte hlen = ps->readByte();
			uint32 consumed = 3 + hlen;
			if (consumed > len) {
				ps->skip(len - 3);
				continue;
			}
			ps->skip(hlen);
			uint32 payload = len - consumed;
			if (payload >= 4) {
				ps->skip(4); // PSP audio sub-header (channel + 3 bytes)
				payload -= 4;
				uint32 old = es.size();
				es.resize(old + payload);
				ps->read(es.data() + old, payload);
			}
		} else {
			ps->skip(len);
		}
	}
	delete ps;

	if (es.empty())
		return result;

	// Split into `0F D0` frames and keep only the raw ATRAC3+ bytes.
	// Same quadratic-resize hazard as `es` above -- reserve an upper bound
	// (frames output can't exceed the `es` input) before growing it per frame.
	result.frames.reserve(es.size());
	uint32 pos = 0;
	uint32 frameCount = 0;
	while (pos + 8 <= es.size()) {
		if (es[pos] != 0x0F || es[pos + 1] != 0xD0) {
			pos++;
			continue;
		}
		uint32 frameSize = (((es[pos + 2] & 0x03) << 8) | (es[pos + 3] * 8)) + 0x10;
		if (frameSize < 8 || pos + frameSize > es.size())
			break;
		uint16 dataSize = (uint16)(frameSize - 8);
		if (result.blockAlign == 0)
			result.blockAlign = dataSize;
		if (dataSize == result.blockAlign) {
			uint32 old = result.frames.size();
			result.frames.resize(old + dataSize);
			memcpy(result.frames.data() + old, es.data() + pos + 8, dataSize);
			frameCount++;
		}
		pos += frameSize;
	}

	// 2048 samples per ATRAC3+ frame (ATRAC3P_FRAME_SAMPLES).
	result.numSamples = frameCount * 2048;
	if (result.frames.empty())
		result.blockAlign = 0;

	debug("PMF: extracted %u ATRAC3+ frames (%u bytes, blockAlign %u) from %s",
	      frameCount, (unsigned)result.frames.size(), result.blockAlign, _filename.c_str());
	return result;
}

} // namespace Common
