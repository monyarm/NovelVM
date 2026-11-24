#include "common/formats/at3.h"

#include "audio/decoders/atrac3.h"
#include "audio/decoders/atrac3plus.h"
#include "common/file.h"
#include "common/substream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

const uint16 kWaveFormatATRAC3 = 0x0270;
// Real files in this session's corpus (Tiger & Bunny's *.AT3) use
// WAVE_FORMAT_EXTENSIBLE for ATRAC3+ rather than a classic 2-byte tag.
const uint16 kWaveFormatExtensible = 0xFFFE;

struct ChunkPos {
	uint32 dataOffset = 0;
	uint32 size = 0;
	bool found = false;
};

// Finds the first chunk with the given 4-byte tag; leaves the stream
// position undefined on return (caller always seeks explicitly).
ChunkPos findChunk(SeekableReadStream &f, const char tag[4], uint32 searchEnd) {
	ChunkPos result;
	f.seek(12); // right after "RIFF"+size+"WAVE"
	while ((uint32)f.pos() + 8 <= searchEnd) {
		char chunkTag[4];
		if (f.read(chunkTag, 4) != 4)
			break;
		uint32 chunkSize = f.readUint32LE();
		uint32 chunkDataOffset = (uint32)f.pos();

		if (memcmp(chunkTag, tag, 4) == 0) {
			result.dataOffset = chunkDataOffset;
			result.size = chunkSize;
			result.found = true;
			return result;
		}

		f.seek(chunkDataOffset + chunkSize + (chunkSize & 1)); // chunks are word-aligned
	}
	return result;
}

} // namespace

Audio::SeekableAudioStream *makeAT3Stream(const Common::String &filename) {
	Common::File *f = new Common::File();
	if (!f->open(Common::Path(filename))) {
		warning("makeAT3Stream(): Could not open %s", filename.c_str());
		delete f;
		return nullptr;
	}

	char magic[4];
	if (f->read(magic, 4) != 4 || memcmp(magic, "RIFF", 4) != 0) {
		warning("makeAT3Stream(): '%s' is not a RIFF file", filename.c_str());
		delete f;
		return nullptr;
	}
	uint32 riffSize = f->readUint32LE();
	char waveMagic[4];
	if (f->read(waveMagic, 4) != 4 || memcmp(waveMagic, "WAVE", 4) != 0) {
		warning("makeAT3Stream(): '%s' is not a RIFF/WAVE file", filename.c_str());
		delete f;
		return nullptr;
	}
	uint32 searchEnd = MIN((uint32)f->size(), 8 + riffSize);

	ChunkPos fmtChunk = findChunk(*f, "fmt ", searchEnd);
	if (!fmtChunk.found || fmtChunk.size < 16) {
		warning("makeAT3Stream(): '%s' has no usable fmt chunk", filename.c_str());
		delete f;
		return nullptr;
	}

	f->seek(fmtChunk.dataOffset);
	uint16 formatTag = f->readUint16LE();
	uint16 channels = f->readUint16LE();
	uint32 sampleRate = f->readUint32LE();
	f->readUint32LE(); // avg bytes/sec, unused -- block align is authoritative
	uint16 blockAlign = f->readUint16LE();
	f->readUint16LE(); // bits/sample, not meaningful for a compressed codec

	bool isPlus;
	if (formatTag == kWaveFormatATRAC3) {
		isPlus = false;
	} else if (formatTag == kWaveFormatExtensible) {
		// Real corpus evidence (Tiger & Bunny: Hero's Day's *.AT3, confirmed
		// via ffprobe): every WAVE_FORMAT_EXTENSIBLE AT3 file found this
		// session is ATRAC3+, not plain ATRAC3 wrapped differently.
		isPlus = true;
	} else {
		warning("makeAT3Stream(): '%s' uses unknown WAVE format tag 0x%04x (expected ATRAC3 0x%04x or "
		        "ATRAC3+'s WAVE_FORMAT_EXTENSIBLE 0x%04x)",
		        filename.c_str(), formatTag, kWaveFormatATRAC3, kWaveFormatExtensible);
		delete f;
		return nullptr;
	}

	Audio::ATRAC3CodingMode codingMode = Audio::kATRAC3CodingSingle;
	if (!isPlus) {
		// Real plain-ATRAC3 WAV extradata: 14 bytes (unknown u16LE,
		// samples-per-channel u32LE (unused), coding mode u16LE, a
		// duplicate of it u16LE, frame factor u16LE, unknown u16LE).
		// ATRAC3+ needs none of this -- its channel layout is derived
		// purely from the channel count (see audio/decoders/atrac3plus.cpp).
		if (fmtChunk.size < 16 + 2 + 14) {
			warning("makeAT3Stream(): '%s' fmt chunk is too small for the real 14-byte ATRAC3 extradata", filename.c_str());
			delete f;
			return nullptr;
		}
		f->seek(fmtChunk.dataOffset + 16 + 2); // skip the fixed header and cbSize field
		f->readUint16LE();                     // unknown, always 1 in real files
		f->readUint32LE();                     // "samples per channel" -- unused, doesn't match real playable length
		uint16 codingModeRaw = f->readUint16LE();
		codingMode = codingModeRaw ? Audio::kATRAC3CodingJointStereo : Audio::kATRAC3CodingSingle;
	}

	ChunkPos factChunk = findChunk(*f, "fact", searchEnd);
	ChunkPos dataChunk = findChunk(*f, "data", searchEnd);
	if (!dataChunk.found) {
		warning("makeAT3Stream(): '%s' has no data chunk", filename.c_str());
		delete f;
		return nullptr;
	}

	// Real per-frame sample count: 1024 for plain ATRAC3, 2048 for
	// ATRAC3+ (ATRAC3P_FRAME_SAMPLES in the reference decoder).
	uint32 samplesPerFrame = isPlus ? 2048 : 1024;

	uint32 numSamples;
	if (factChunk.found) {
		f->seek(factChunk.dataOffset);
		numSamples = f->readUint32LE();
	} else {
		// Every real file in this session's corpus had a fact chunk, so
		// this fallback is unverified against real data.
		numSamples = (dataChunk.size / blockAlign) * samplesPerFrame;
	}

	f->seek(dataChunk.dataOffset);
	Common::SeekableSubReadStream *dataStream = new Common::SeekableSubReadStream(
		f, dataChunk.dataOffset, dataChunk.dataOffset + dataChunk.size, DisposeAfterUse::YES);

	if (isPlus)
		return Audio::makeATRAC3PlusStream(dataStream, DisposeAfterUse::YES, channels, sampleRate, blockAlign, numSamples);
	return Audio::makeATRAC3Stream(dataStream, DisposeAfterUse::YES, channels, sampleRate, blockAlign, codingMode, numSamples);
}

} // namespace Common
