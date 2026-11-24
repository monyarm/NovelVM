#ifndef FORMAT_USM_H
#define FORMAT_USM_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {

class SeekableReadStream;

struct USMVideoInfo {
	bool valid = false;
	uint16 width = 0;
	uint16 height = 0;
	uint32 totalFrames = 0;
	uint32 framerateNum = 0;
	uint32 framerateDen = 0;

	// CRI's "mpeg_codec" field. Real Persona 5 Royal data uses kCodecVP9;
	// none of these have a decoder in NovelVM yet (no FFmpeg/libav, no
	// standalone VP9/H264 port) -- see docs/middleware-plan.md.
	enum Codec {
		kCodecUnknown = 0,
		kCodecMPEG1 = 1,
		kCodecH264 = 5,
		kCodecVP9 = 9
	};
	Codec codec = kCodecUnknown;
};

struct USMAudioInfo {
	bool valid = false;
	uint32 sampleRate = 0;
	byte channels = 0;
	uint32 totalSamples = 0;

	enum Codec {
		kCodecUnknown = 0,
		kCodecADX = 2,
		kCodecHCA = 4
	};
	Codec codec = kCodecUnknown;
};

/**
 * Reader for CRI's USM (Sofdec2) video container: a sequence of chunks
 * ("CRID" file marker, then "@SFV"/"@SFA"/"@ALP"/"@SBT" per-stream chunks),
 * each holding either a CRI @UTF metadata packet (same binary table format
 * as CPK's TOC, see common/formats/utf_table.h) describing that stream, or
 * raw payload data for one frame/audio block. Real chunk layout verified
 * against Persona 5 Royal's mov050.usm (real header magics "CRID"@0x00,
 * "@UTF"@0x20; structure cross-checked against FFmpeg's libavformat/usmdec.c,
 * LGPL 2.1+, facts/offsets only, no code copied).
 *
 * Only the first video and first audio stream (index 0) are read, matching
 * this session's real single-video/single-audio sample.
 */
class USM {
public:
	USM(const Common::String &filename);

	bool isValid() const { return _videoInfo.valid || _audioInfo.valid; }
	const USMVideoInfo &getVideoInfo() const { return _videoInfo; }
	const USMAudioInfo &getAudioInfo() const { return _audioInfo; }

	// Concatenated raw audio chunk payloads (the on-disk ADX header chunk
	// followed by all ADX frame-data chunks, back to back -- byte-identical
	// to a standalone .adx file), ready for Common::makeADXStream(). Only
	// valid when getAudioInfo().codec == kCodecADX; nullptr otherwise (HCA
	// has no decoder in NovelVM).
	Common::SeekableReadStream *createAudioStream() const;

private:
	Common::String _filename;
	USMVideoInfo _videoInfo;
	USMAudioInfo _audioInfo;
	Common::Array<byte> _audioData;

	bool load();
};

} // namespace Common

#endif
