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

#include "audio/audiostream.h"
#include "audio/decoders/atrac3.h"
#include "audio/decoders/atrac3_common.h"
#include "audio/decoders/atrac3plus.h"
#include "audio/decoders/raw.h"
#include "audio/decoders/ac3.h"
#include "audio/decoders/mp3.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/stream.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "video/mpegps_decoder.h"
#include "video/mpeg_video_track.h"
#include "image/codecs/codec.h"

// The demuxing code is based on libav's demuxing code

namespace Video {

// --------------------------------------------------------------------------
// Decoder - This is the part that takes a packet and figures out what to do
// with it.
// --------------------------------------------------------------------------

enum {
	kStartCodeSequenceHeader = 0x1B3,
	kStartCodePack = 0x1BA,
	kStartCodeSystemHeader = 0x1BB,
	kStartCodeProgramStreamMap = 0x1BC,
	kStartCodePrivateStream1 = 0x1BD,
	kStartCodePaddingStream = 0x1BE,
	kStartCodePrivateStream2 = 0x1BF
};

MPEGPSDecoder::MPEGPSDecoder(double decibel) {
	_decibel = decibel;
	_demuxer = new MPEGPSDemuxer();
}

MPEGPSDecoder::~MPEGPSDecoder() {
	close();
	delete _demuxer;
}

bool MPEGPSDecoder::loadStream(Common::SeekableReadStream *stream) {
	close();

	if (!_demuxer->loadStream(stream)) {
		close();
		return false;
	}

	if (!addFirstVideoTrack()) {
		close();
		return false;
	}

	return true;
}

void MPEGPSDecoder::setPrebufferedPackets(int packets) {
	_demuxer->setPrebufferedPackets(packets);
}

void MPEGPSDecoder::setATRACAudioInfo(ATRACCodec codec, uint16 channels, uint32 sampleRate, uint16 blockAlign) {
	_atracCodec = codec;
	_atracChannels = channels;
	_atracSampleRate = sampleRate;
	_atracBlockAlign = blockAlign;
	_hasATRACInfo = true;
}

bool MPEGPSDecoder::detectATRAC3PlusInfo(Common::SeekableReadStream &packet, uint16 &channels, uint32 &sampleRate, uint16 &blockAlign) {
	// PSMF audio payload: 4-byte PSP sub-header, then the ATRAC3+ frame header
	// 0F D0 <attr:16 BE>: bits 13-15 sample rate code, 10-12 channels, 0-9 (data size - 8) / 8
	// (as PPSSPP reads it). Verified on the real P3P opening: code 1, 2 channels, 744 bytes,
	// matching its PSMF header (44100 Hz stereo). Other rate codes have no real sample, so
	// those streams still need setATRACAudioInfo().
	const int64 start = packet.pos();
	packet.skip(4);
	const uint16 sync = packet.readUint16BE();
	const uint16 attr = packet.readUint16BE();
	const bool eos = packet.eos();
	packet.seek(start);
	channels = (attr >> 10) & 7;
	if (eos || sync != 0x0FD0 || (attr >> 13) != 1 || channels < 1 || channels > 2)
		return false;
	sampleRate = 44100;
	blockAlign = (attr & 0x3FF) * 8 + 8;
	return true;
}

void MPEGPSDecoder::close() {
	VideoDecoder::close();
	_demuxer->close();
	_streamMap.clear();
	_hasADXTrack = false;
}

MPEGPSDecoder::MPEGStream *MPEGPSDecoder::getStream(uint32 startCode, Common::SeekableReadStream *packet) {
	MPEGStream *stream = 0;

	if (_streamMap.contains(startCode)) {
		// We already found the stream
		stream = _streamMap[startCode];
	} else {
		// We haven't seen this before

		if (startCode == kStartCodePrivateStream1) {
			PrivateStreamType streamType = detectPrivateStreamType(packet);
			packet->seek(0);

			// TODO: Handling of these types (as needed)
			bool handled = false;
			const char *typeName;

			switch (streamType) {
			case kPrivateStreamAC3: {
				typeName = "AC-3";

#ifdef USE_A52
				handled = true;
				AC3AudioTrack *ac3Track = new AC3AudioTrack(*packet, _decibel, getSoundType());
				stream = ac3Track;
				_streamMap[startCode] = ac3Track;
				addTrack(ac3Track);
#endif
				break;
			}
			case kPrivateStreamDTS:
				typeName = "DTS";
				break;
			case kPrivateStreamDVDPCM:
				typeName = "DVD PCM";
				break;
			case kPrivateStreamPS2Audio: {
				typeName = "PS2 Audio";
				handled = true;
				PS2AudioTrack *audioTrack = new PS2AudioTrack(packet, getSoundType());
				stream = audioTrack;
				_streamMap[startCode] = audioTrack;
				addTrack(audioTrack);
				break;
			}
			case kPrivateStreamATRAC3Plus: {
				typeName = "ATRAC3+";
				uint16 channels, blockAlign;
				uint32 sampleRate;
				if (!_hasATRACInfo && detectATRAC3PlusInfo(*packet, channels, sampleRate, blockAlign))
					setATRACAudioInfo(kATRACCodecATRAC3Plus, channels, sampleRate, blockAlign);
				if (_hasATRACInfo && _atracCodec == kATRACCodecATRAC3Plus) {
					handled = true;
					ATRACAudioTrack *audioTrack = new ATRACAudioTrack(kATRACCodecATRAC3Plus,
						ATRACAudioTrack::kFramingPSMF, _atracChannels, _atracSampleRate,
						_atracBlockAlign, getSoundType());
					stream = audioTrack;
					_streamMap[startCode] = audioTrack;
					addTrack(audioTrack);
				}
				break;
			}
			default:
				if (_hasATRACInfo && (_atracCodec == kATRACCodecATRAC3 || _atracCodec == kATRACCodecATRAC3JointStereo)) {
					// Plain ATRAC3 has no in-band sync tag, so only decode it
					// as ATRAC when a container explicitly declared this
					// private stream to be ATRAC3. (No real PSMF sample of
					// this exists yet; the shared frame decoder is already
					// real-data-verified via the AT3 container.)
					typeName = "ATRAC3";
					handled = true;
					ATRACAudioTrack *audioTrack = new ATRACAudioTrack(kATRACCodecATRAC3,
						ATRACAudioTrack::kFramingPlain, _atracChannels, _atracSampleRate,
						_atracBlockAlign, getSoundType());
					stream = audioTrack;
					_streamMap[startCode] = audioTrack;
					addTrack(audioTrack);
					break;
				}
				typeName = "Unknown";
				break;
			}

			if (!handled) {
				warning("Unhandled DVD private stream: %s", typeName);

				// Make it 0 so we don't get the warning twice
				_streamMap[startCode] = 0;
			}
		} else if (startCode >= 0x1E0 && startCode <= 0x1EF) {
			// Video stream
			// TODO: Multiple video streams
			warning("Found extra video stream 0x%04X", startCode);
			_streamMap[startCode] = 0;
		} else if (startCode >= 0x1C0 && startCode <= 0x1DF) {
			// Some PS2 games (real-data-verified: Persona 3/4's .SFD
			// files) put CRI ADX directly on this "MPEG audio" stream ID
			// instead of real MPEG audio. Real ADX starts with magic
			// 0x8000; MPEG audio never does (its sync word starts with
			// 11 set bits), so this check is unambiguous.
			// TODO: encrypted ADX inside SFD is not decrypted (no encrypted sample in the corpus).
			// CRI's player plays one audio stream: the first one found.
			Common::ADXFrameInfo adxInfo;
			if (_hasADXTrack) {
				_streamMap[startCode] = 0;
			} else if (packet->readUint16BE() == 0x8000) {
				packet->seek(0);
				const bool validHeader = Common::parseADXFrameInfo(*packet, adxInfo);
				packet->seek(0);
				if (validHeader) {
					ADXAudioTrack *audioTrack = new ADXAudioTrack(*packet, getSoundType());
					_hasADXTrack = true;
					stream = audioTrack;
					_streamMap[startCode] = audioTrack;
					addTrack(audioTrack);
				} else {
					// Corrupt or truncated header: drop this audio stream, keep the video.
					warning("Skipping MPEG-PS stream 0x%04X: malformed embedded ADX header", startCode);
					_streamMap[startCode] = 0;
				}
			} else {
				packet->seek(0);
#ifdef USE_MAD
				// MPEG Audio stream
				MPEGAudioTrack *audioTrack = new MPEGAudioTrack(*packet, getSoundType());
				stream = audioTrack;
				_streamMap[startCode] = audioTrack;
				addTrack(audioTrack);
#else
				warning("Found audio stream 0x%04X, but no MAD support compiled in", startCode);
				_streamMap[startCode] = 0;
#endif
			}
		} else {
			// Probably not relevant
			debug(0, "Found unhandled MPEG-PS stream type 0x%04x", startCode);
			_streamMap[startCode] = 0;
		}
	}

	return stream;
}

void MPEGPSDecoder::readNextPacket() {
	for (;;) {
		int32 startCode;
		uint32 pts, dts;
		Common::SeekableReadStream *packet = _demuxer->getNextPacket(getTime(), startCode, pts, dts);

		if (!packet) {
			// End of stream
			for (TrackListIterator it = getTrackListBegin(); it != getTrackListEnd(); it++) {
				if ((*it)->getTrackType() != Track::kTrackTypeVideo)
					continue;
				if (MPEGVideoTrack *mpeg = dynamic_cast<MPEGVideoTrack *>(*it))
					mpeg->setEndOfTrack();
#ifdef USE_OPENH264
				if (H264VideoTrack *avc = dynamic_cast<H264VideoTrack *>(*it))
					avc->setEndOfTrack();
#endif
			}
			return;
		}

		MPEGStream *stream = getStream(startCode, packet);

		if (stream) {
			packet->seek(0);

			bool done = stream->sendPacket(packet, pts, dts);

			if (done && stream->getStreamType() == MPEGStream::kStreamTypeVideo)
				return;
		} else {
			delete packet;
		}
	}
}

bool isH264AnnexBPacket(const byte *data, uint32 size) {
	if (!data || size < 4)
		return false;

	// MPEG-1/2 sequence headers are also Annex-B-shaped (00 00 01), but
	// start with 0xB3. Require an SPS or PPS before selecting OpenH264.
	for (uint32 i = 0; i + 4 <= size; i++) {
		uint32 startCodeSize = 0;
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
			startCodeSize = 3;
		else if (i + 5 <= size && data[i] == 0 && data[i + 1] == 0 &&
		         data[i + 2] == 0 && data[i + 3] == 1)
			startCodeSize = 4;
		if (!startCodeSize)
			continue;

		byte nalType = data[i + startCodeSize] & 0x1F;
		if (nalType == 7 || nalType == 8)
			return true;
	}
	return false;
}

bool MPEGPSDecoder::addFirstVideoTrack() {
	int32 startCode;
	uint32 pts, dts;
	Common::SeekableReadStream *packet = _demuxer->getFirstVideoPacket(startCode, pts, dts);

	if (!packet)
		return false;

	// Video stream can be MPEG-1/2 or H.264/AVC (the latter used by PSP
	// PSMF openings). Both formats use 00 00 01 start codes, so require an
	// H.264 SPS/PPS instead of treating every start code as H.264.
	packet->seek(0);
	bool h264 = false;
	if (packet->size() > 0) {
		Common::Array<byte> head(packet->size());
		packet->read(head.data(), head.size());
		h264 = isH264AnnexBPacket(head.data(), head.size());
	}
	packet->seek(0);

#ifdef USE_OPENH264
	if (h264) {
		H264VideoTrack *track = new H264VideoTrack(packet);
		addTrack(track);
		_streamMap[startCode] = track;
		return true;
	}
#endif

	MPEGVideoTrack *track = new MPEGVideoTrack(packet);
	addTrack(track);
	_streamMap[startCode] = track;

	return true;
}

MPEGPSDecoder::PrivateStreamType MPEGPSDecoder::detectPrivateStreamType(Common::SeekableReadStream *packet) {
	uint32 dvdCode = packet->readUint32LE();
	if (packet->eos())
		return kPrivateStreamUnknown;

	uint32 ps2Header = packet->readUint32BE();
	if (!packet->eos() && ps2Header == MKTAG('S', 'S', 'h', 'd'))
		return kPrivateStreamPS2Audio;

	// PSMF audio: a 4-byte PSP audio sub-header, then an ATRAC3+ frame whose
	// real sync tag is 0F D0 (see common/formats/pmf.cpp).
	if (!packet->eos() && (ps2Header >> 16) == 0x0FD0)
		return kPrivateStreamATRAC3Plus;

	switch (dvdCode & 0xE0) {
	case 0x80:
		if ((dvdCode & 0xF8) == 0x88)
			return kPrivateStreamDTS;

		return kPrivateStreamAC3;
	case 0xA0:
		return kPrivateStreamDVDPCM;
	default:
		break;
	}

	return kPrivateStreamUnknown;
}

// --------------------------------------------------------------------------
// Demuxer - This is the part that reads packets from the stream and delivers
// them to the decoder.
//
// It will buffer a number of packets in advance, because otherwise it may
// not encounter any audio packets until it's far too late to decode them.
// Before I added this, there would be 9 or 10 frames of video before the
// first audio packet, even though the timestamp indicated that the audio
// should start slightly before the video.
// --------------------------------------------------------------------------

#define AUDIO_THRESHOLD     100

MPEGPSDecoder::MPEGPSDemuxer::MPEGPSDemuxer() {
	_stream = 0;
}

MPEGPSDecoder::MPEGPSDemuxer::~MPEGPSDemuxer() {
	close();
}

bool MPEGPSDecoder::MPEGPSDemuxer::loadStream(Common::SeekableReadStream *stream) {
	close();

	// Check if the videostream being loaded is an elementary stream (ES) or a program stream (PS)
	// PS streams start with the Pack Header which has a start code of 0x1ba
	// ES streams start with the Sequence Header which has a start code of 0x1b3
	uint32 header = stream->readUint32BE();
	stream->seek(-4, SEEK_CUR);

	// If it is a Sequence Header (ES Stream), pass the stream to a Elementary Stream handler.
	// If it is a Pack Header (PS stream), pass the stream to PS demuxer for demuxing into video and audio packets
	// Currently not handling other stream types like ES audio stream
	// Throwing a warning, so that decoding of further streams isn't affected
	// Unknown stream header types are "handled" (ignored) in the readNextPacketHeader function 

	if (header == kStartCodeSequenceHeader) {
		_isESStream = true;
	} else if (header == kStartCodePack) {
		_isESStream = false;
	} else {
		warning("Unknown Start Code in the MPEG stream, %d", header);
		_isESStream = false;
	}
	
	_stream = stream;

	int queuedPackets = 0;
	while (queueNextPacket() && queuedPackets < _prebufferedPackets) {
		queuedPackets++;
	}

	return true;
}

void MPEGPSDecoder::MPEGPSDemuxer::close() {
	delete _stream;
	_stream = 0;

	_firstAudioPacketPts = 0xFFFFFFFF;
	_firstVideoPacketPts = 0xFFFFFFFF;

	while (!_audioQueue.empty()) {
		Packet packet = _audioQueue.pop();
		delete packet._stream;
	}

	while (!_videoQueue.empty()) {
		Packet packet = _videoQueue.pop();
		delete packet._stream;
	}
}

Common::SeekableReadStream *MPEGPSDecoder::MPEGPSDemuxer::getFirstVideoPacket(int32 &startCode, uint32 &pts, uint32 &dts) {
	if (_videoQueue.empty())
		return nullptr;
	Packet packet = _videoQueue.front();
	startCode = packet._startCode;
	pts = packet._pts;
	dts = packet._dts;
	return packet._stream;
}

Common::SeekableReadStream *MPEGPSDecoder::MPEGPSDemuxer::getNextPacket(uint32 currentTime, int32 &startCode, uint32 &pts, uint32 &dts) {
	queueNextPacket();

	// The idea here is to prioritize the delivery of audio packets,
	// because when the decoder wants a frame it will keep asking until it
	// gets a frame. There is nothing like that in the decoder to ensure
	// speedy delivery of audio.

	if (!_audioQueue.empty()) {
		Packet packet = _audioQueue.front();
		bool usePacket = false;

		if (packet._pts == 0xFFFFFFFF) {
			// No timestamp? Use it just in case. This could be a
			// bad idea, but in my tests all audio packets have a
			// time stamp.
			usePacket = true;
		} else {
			if (packet._pts >= _firstAudioPacketPts)
				packet._pts -= _firstAudioPacketPts;
			uint32 packetTime = packet._pts / 90;
			if (packetTime <= currentTime || packetTime - currentTime < AUDIO_THRESHOLD || _videoQueue.empty()) {
				// The packet is overdue, or will be soon.
				//
				// TODO: We should pad or trim the first audio
				// packet based on the timestamp to get the
				// audio to start at the exact desired time.
				// But for some reason it seems to work well
				// enough anyway. For now.
				usePacket = true;
			}
		}

		if (usePacket) {
			_audioQueue.pop();
			startCode = packet._startCode;
			pts = packet._pts;
			dts = packet._dts;
			return packet._stream;
		}
	}

	if (!_videoQueue.empty()) {
		Packet packet = _videoQueue.pop();
		startCode = packet._startCode;

		if (packet._pts != 0xFFFFFFFF && packet._pts >= _firstVideoPacketPts) {
			packet._pts -= _firstVideoPacketPts;
		}
		pts = packet._pts;
		dts = packet._dts;
		return packet._stream;
	}

	return nullptr;
}

bool MPEGPSDecoder::MPEGPSDemuxer::queueNextPacket() {
	if (_stream->eos())
		return false;

	// Program Streams are nothing but a wrapping around Elementary Stream data, this wrapping or header has 
	// length, pts, dts and other information embedded in it which we parse in the readNextPacketHeader function
	// Elementary stream doesn't have pts and dts, but our sendPacket() function handles that well
	// TODO: Only handling video ES streams for now, audio ES streams are bit more complicated
	if (_isESStream) {
		const uint16 kESPacketSize = 1024;	// FFmpeg uses 1024 to packetize ES streams into PES packets
	  
		Common::SeekableReadStream *stream = _stream->readStream(kESPacketSize);

		int32 startCode = 0x1E0;
		uint32 pts = 0xFFFFFFFF, dts = 0xFFFFFFFF;
		_videoQueue.push(Packet(stream, startCode, pts, dts));
		return true;
	}

	for (;;) {
		int32 startCode;
		uint32 pts, dts;
		int size = readNextPacketHeader(startCode, pts, dts);

		if (size < 0) {
			// End of stream
			return false;
		}

		Common::SeekableReadStream *stream = _stream->readStream(size);

		if (startCode == kStartCodePrivateStream1 || (startCode >= 0x1C0 && startCode <= 0x1DF)) {
			// Audio packet
			_audioQueue.push(Packet(stream, startCode, pts, dts));
			if (_firstAudioPacketPts == 0xFFFFFFFF)
				_firstAudioPacketPts = pts;
			return true;
		}

		if (startCode >= 0x1E0 && startCode <= 0x1EF) {
			// Video packet
			_videoQueue.push(Packet(stream, startCode, pts, dts));
			if (_firstVideoPacketPts == 0xFFFFFFFF)
				_firstVideoPacketPts = pts;
			return true;
		}

		delete stream;
	}
}

int MPEGPSDecoder::MPEGPSDemuxer::readNextPacketHeader(int32 &startCode, uint32 &pts, uint32 &dts) {
	for (;;) {
		uint32 size;
		startCode = findNextStartCode(size);

		if (_stream->eos())
			return -1;

		if (startCode < 0)
			continue;

		uint32 lastSync = _stream->pos();

		if (startCode == kStartCodePack || startCode == kStartCodeSystemHeader)
			continue;

		int length = _stream->readUint16BE();

		if (startCode == kStartCodePaddingStream || startCode == kStartCodePrivateStream2) {
			_stream->skip(length);
			continue;
		}

		if (startCode == kStartCodeProgramStreamMap) {
			parseProgramStreamMap(length);
			continue;
		}

		// Find matching stream
		if (!((startCode >= 0x1C0 && startCode <= 0x1DF) ||
				(startCode >= 0x1E0 && startCode <= 0x1EF) ||
				startCode == kStartCodePrivateStream1 || startCode == 0x1FD))
			continue;

		// Stuffing
		byte c;
		for (;;) {
			if (length < 1) {
				_stream->seek(lastSync);
				continue;
			}

			c = _stream->readByte();
			length--;

			// XXX: for mpeg1, should test only bit 7
			if (c != 0xFF)
				break;
		}

		if ((c & 0xC0) == 0x40) {
			// Buffer scale and size
			_stream->readByte();
			c = _stream->readByte();
			length -= 2;
		}

		pts = 0xFFFFFFFF;
		dts = 0xFFFFFFFF;

		if ((c & 0xE0) == 0x20) {
			dts = pts = readPTS(c);
			length -= 4;

			if (c & 0x10) {
				dts = readPTS(-1);
				length -= 5;
			}
		} else if ((c & 0xC0) == 0x80) {
			// MPEG-2 PES
			byte flags = _stream->readByte();
			int headerLength = _stream->readByte();
			length -= 2;

			if (headerLength > length) {
				_stream->seek(lastSync);
				continue;
			}

			length -= headerLength;

			if (flags & 0x80) {
				dts = pts = readPTS(-1);
				headerLength -= 5;

				if (flags & 0x40) {
					dts = readPTS(-1);
					headerLength -= 5;
				}
			}

			if (flags & 0x3F && headerLength == 0) {
				flags &= 0xC0;
				warning("Further flags set but no bytes left");
			}

			if (flags & 0x01) { // PES extension
				byte pesExt =_stream->readByte();
				headerLength--;

				// Skip PES private data, program packet sequence
				int skip = (pesExt >> 4) & 0xB;
				skip += skip & 0x9;

				if (pesExt & 0x40 || skip > headerLength) {
					warning("pesExt %x is invalid", pesExt);
					pesExt = skip = 0;
				} else {
					_stream->skip(skip);
					headerLength -= skip;
				}

				if (pesExt & 0x01) { // PES extension 2
					byte ext2Length = _stream->readByte();
					headerLength--;

					if ((ext2Length & 0x7F) != 0) {
						byte idExt = _stream->readByte();

						if ((idExt & 0x80) == 0)
							startCode = (startCode & 0xFF) << 8;

						headerLength--;
					}
				}
			}

			if (headerLength < 0) {
				_stream->seek(lastSync);
				continue;
			}

			_stream->skip(headerLength);
		} else if (c != 0xF) {
			continue;
		}

		if (length < 0) {
			_stream->seek(lastSync);
			continue;
		}

		return length;
	}
}

#define MAX_SYNC_SIZE 100000

int MPEGPSDecoder::MPEGPSDemuxer::findNextStartCode(uint32 &size) {
	size = MAX_SYNC_SIZE;
	int32 state = 0xFF;

	while (size > 0) {
		byte v = _stream->readByte();

		if (_stream->eos())
			return -1;

		size--;

		if (state == 0x1)
			return ((state << 8) | v) & 0xFFFFFF;

		state = ((state << 8) | v) & 0xFFFFFF;
	}

	return -1;
}

uint32 MPEGPSDecoder::MPEGPSDemuxer::readPTS(int c) {
	byte buf[5];

	buf[0] = (c < 0) ? _stream->readByte() : c;
	_stream->read(buf + 1, 4);

	return ((buf[0] & 0x0E) << 29) | ((READ_BE_UINT16(buf + 1) >> 1) << 15) | (READ_BE_UINT16(buf + 3) >> 1);
}

void MPEGPSDecoder::MPEGPSDemuxer::parseProgramStreamMap(int length) {
	_stream->readByte();
	_stream->readByte();

	// skip program stream info
	_stream->skip(_stream->readUint16BE());

	int esMapLength = _stream->readUint16BE();

	while (esMapLength >= 4) {
		_stream->readByte(); // type
		_stream->readByte(); // esID
		uint16 esInfoLength = _stream->readUint16BE();

		// Skip program stream info
		_stream->skip(esInfoLength);

		esMapLength -= 4 + esInfoLength;
	}

	_stream->readUint32BE(); // CRC32
}

// --------------------------------------------------------------------------
// Video track
// --------------------------------------------------------------------------

MPEGPSDecoder::MPEGVideoTrack::MPEGVideoTrack(Common::SeekableReadStream *firstPacket) {
	_surface = 0;
	_endOfTrack = false;
	_curFrame = -1;
	_framePts = 0xFFFFFFFF;
	_nextFrameStartTime = Audio::Timestamp(0, 27000000); // 27 MHz timer

	findDimensions(firstPacket);

#ifdef USE_MPEG2
	_decoder = new MPEGFrameDecoder();
#endif
}

MPEGPSDecoder::MPEGVideoTrack::~MPEGVideoTrack() {
#ifdef USE_MPEG2
	delete _decoder;
#endif

	if (_surface) {
		_surface->free();
		delete _surface;
	}
}

uint16 MPEGPSDecoder::MPEGVideoTrack::getWidth() const {
	return _width;
}

uint16 MPEGPSDecoder::MPEGVideoTrack::getHeight() const {
	return _height;
}

Graphics::PixelFormat MPEGPSDecoder::MPEGVideoTrack::getPixelFormat() const {
	return _pixelFormat;
}

bool MPEGPSDecoder::MPEGVideoTrack::setOutputPixelFormat(const Graphics::PixelFormat &format) {
	if (format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return false;
	_pixelFormat = format;
	return true;
}

const Graphics::Surface *MPEGPSDecoder::MPEGVideoTrack::decodeNextFrame() {
	return _surface;
}

bool MPEGPSDecoder::MPEGVideoTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
#ifdef USE_MPEG2
	if (!_surface) {
		_surface = new Graphics::Surface();
		_surface->create(_width, _height, _pixelFormat);
	}

	if (pts != 0xFFFFFFFF) {
		_framePts = pts;
	}

	uint32 framePeriod;
	bool foundFrame = _decoder->decode(*packet, framePeriod, _surface);

	if (foundFrame) {
		_curFrame++;

		// If there has been a timestamp since the previous frame, use that for
		// syncing. Usually it will be the timestamp from the current packet,
		// but it might not be.

		if (_framePts != 0xFFFFFFFF) {
			_nextFrameStartTime = Audio::Timestamp(_framePts / 90, 27000000);
		} else {
			_nextFrameStartTime = _nextFrameStartTime.addFrames(framePeriod);
		}

		_framePts = 0xFFFFFFFF;
	}
#endif

	delete packet;

#ifdef USE_MPEG2
	return foundFrame;
#else
	return true;
#endif
}

void MPEGPSDecoder::MPEGVideoTrack::findDimensions(Common::SeekableReadStream *firstPacket) {
	// First, check for the picture start code
	if (firstPacket->readUint32BE() != 0x1B3)
		error("Failed to detect MPEG sequence start");

	// This is part of the bitstream, but there's really no purpose
	// to use Common::BitStream just for this: 12 bits width, 12 bits
	// height
	_width = firstPacket->readByte() << 4;
	_height = firstPacket->readByte();
	_width |= (_height & 0xF0) >> 4;
	_height = ((_height & 0x0F) << 8) | firstPacket->readByte();
	_pixelFormat = Image::Codec::getDefaultYUVFormat();

	debugC(3, kDebugLevelGVideo, "MPEG dimensions: %dx%d", _width, _height);

	firstPacket->seek(0);
}

// --------------------------------------------------------------------------
// H.264/AVC video track (OpenH264)
// --------------------------------------------------------------------------

#ifdef USE_OPENH264

MPEGPSDecoder::H264VideoTrack::H264VideoTrack(Common::SeekableReadStream *firstPacket) {
	(void)firstPacket; // owned by the demuxer; content re-delivered via sendPacket
	_endOfTrack = false;
	_curFrame = -1;
	_framePts = 0xFFFFFFFF;
	_basePts = 0;
	_haveBasePts = false;
	_nextFrameStartTime = Audio::Timestamp(0, 27000000); // 27 MHz timer
	_width = _height = 0;
	_pixelFormat = Image::Codec::getDefaultYUVFormat();

	// The shared H264FrameDecoder owns the OpenH264 context and the Annex-B
	// reassembly buffer (extracted from this class so USM can reuse it).
	// OpenH264 wants an SPS; streams carry SPS/PPS NAL units inline
	// (Annex-B), so wiring the decoder up and feeding packets is enough --
	// dimensions arrive with the first decoded frame.
	_decoder = new H264FrameDecoder();
}

MPEGPSDecoder::H264VideoTrack::~H264VideoTrack() {
	delete _decoder;
}

uint16 MPEGPSDecoder::H264VideoTrack::getWidth() const {
	return _width;
}

uint16 MPEGPSDecoder::H264VideoTrack::getHeight() const {
	return _height;
}

Graphics::PixelFormat MPEGPSDecoder::H264VideoTrack::getPixelFormat() const {
	return _pixelFormat;
}

bool MPEGPSDecoder::H264VideoTrack::setOutputPixelFormat(const Graphics::PixelFormat &format) {
	if (format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return false;
	_pixelFormat = format;
	return true;
}

const Graphics::Surface *MPEGPSDecoder::H264VideoTrack::decodeNextFrame() {
	return _decoder->getSurface();
}

bool MPEGPSDecoder::H264VideoTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	uint32 size = packet->size();
	if (size == 0) {
		delete packet;
		return false;
	}

	if (pts != 0xFFFFFFFF) {
		_framePts = pts;
		if (!_haveBasePts) {
			_basePts = pts;
			_haveBasePts = true;
		}
	}

	Common::Array<byte> data(size);
	packet->read(data.data(), size);
	delete packet;

	uint16 width = _width, height = _height;
	if (!_decoder->decode(data.data(), size, width, height, _pixelFormat))
		return false;

	if (width && height) {
		_width = width;
		_height = height;
	}

	_curFrame++;

	// Pace with the real PES PTS (90 kHz), normalised so the first frame
	// starts at t=0. The previous code used Audio::Timestamp(_curFrame, 30),
	// which is the (msecs, framerate) constructor -- frame N started at N
	// *milliseconds*, so the video advanced ~33x too fast and drifted out of
	// sync with the audio. Fall back to the stream's constant 29.97 fps when
	// a packet carries no PTS.
	if (_framePts != 0xFFFFFFFF)
		_nextFrameStartTime = Audio::Timestamp((_framePts - _basePts) / 90, 1000);
	else
		_nextFrameStartTime = Audio::Timestamp(_curFrame * 1001 / 30, 1000);
	_framePts = 0xFFFFFFFF;

	return true;
}

#endif // USE_OPENH264

// --------------------------------------------------------------------------
// Audio track
// --------------------------------------------------------------------------

#ifdef USE_MAD

// The audio code here is almost entirely based on what we do in mp3.cpp

MPEGPSDecoder::MPEGAudioTrack::MPEGAudioTrack(Common::SeekableReadStream &firstPacket, Audio::Mixer::SoundType soundType) :
		AudioTrack(soundType) {
	_audStream = Audio::makePacketizedMP3Stream(firstPacket);
}

MPEGPSDecoder::MPEGAudioTrack::~MPEGAudioTrack() {
	delete _audStream;
}

bool MPEGPSDecoder::MPEGAudioTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	_audStream->queuePacket(packet);
	return true;
}

Audio::AudioStream *MPEGPSDecoder::MPEGAudioTrack::getAudioStream() const {
	return _audStream;
}

#endif

#ifdef USE_A52

MPEGPSDecoder::AC3AudioTrack::AC3AudioTrack(Common::SeekableReadStream &firstPacket, double decibel, Audio::Mixer::SoundType soundType) :
		AudioTrack(soundType) {
	_audStream = Audio::makeAC3Stream(firstPacket, decibel);
	if (!_audStream)
		error("Could not create AC-3 stream");
}

MPEGPSDecoder::AC3AudioTrack::~AC3AudioTrack() {
	delete _audStream;
}

bool MPEGPSDecoder::AC3AudioTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	// Skip DVD code
	packet->readUint32LE();
	if (packet->eos())
		return true;

	_audStream->queuePacket(packet);
	return true;
}

Audio::AudioStream *MPEGPSDecoder::AC3AudioTrack::getAudioStream() const {
	return _audStream;
}

#endif

MPEGPSDecoder::PS2AudioTrack::PS2AudioTrack(Common::SeekableReadStream *firstPacket, Audio::Mixer::SoundType soundType) :
		AudioTrack(soundType) {
	firstPacket->seek(12); // unknown data (4), 'SShd', header size (4)

	_soundType = firstPacket->readUint32LE();

	if (_soundType == PS2_ADPCM)
		error("Unhandled PS2 ADPCM sound in MPEG-PS video");
	else if (_soundType != PS2_PCM)
		error("Unknown PS2 sound type %x", _soundType);

	uint32 sampleRate = firstPacket->readUint32LE();
	_channels = firstPacket->readUint32LE();
	_interleave = firstPacket->readUint32LE();

	byte flags = Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN;
	if (_channels == 2)
		flags |= Audio::FLAG_STEREO;

	_blockBuffer = new byte[_interleave * _channels];
	_blockPos = _blockUsed = 0;
	_audStream = Audio::makePacketizedRawStream(sampleRate, flags);
	_isFirstPacket = true;

	firstPacket->seek(0);
}

MPEGPSDecoder::PS2AudioTrack::~PS2AudioTrack() {
	delete[] _blockBuffer;
	delete _audStream;
}

bool MPEGPSDecoder::PS2AudioTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	packet->skip(4);

	if (_isFirstPacket) {
		// Skip over the header which we already parsed
		packet->skip(4);
		packet->skip(packet->readUint32LE());

		if (packet->readUint32BE() != MKTAG('S', 'S', 'b', 'd'))
			error("Failed to find 'SSbd' tag");

		packet->readUint32LE(); // body size
		_isFirstPacket = false;
	}

	uint32 size = packet->size() - packet->pos();
	uint32 bytesPerChunk = _interleave * _channels;
	uint32 sampleCount = calculateSampleCount(size);

	byte *buffer = (byte *)malloc(sampleCount * 2);
	int16 *ptr = (int16 *)buffer;

	// Handle any full chunks first
	while (size >= bytesPerChunk) {
		packet->read(_blockBuffer + _blockPos, bytesPerChunk - _blockPos);
		size -= bytesPerChunk - _blockPos;
		_blockPos = 0;

		for (uint32 i = _blockUsed; i < _interleave / 2; i++)
			for (uint32 j = 0; j < _channels; j++)
				*ptr++ = READ_UINT16(_blockBuffer + i * 2 + j * _interleave);

		_blockUsed = 0;
	}

	// Then fallback on loading any leftover
	if (size > 0) {
		packet->read(_blockBuffer, size);
		_blockPos = size;

		if (size > (_channels - 1) * _interleave) {
			_blockUsed = (size - (_channels - 1) * _interleave) / 2;

			for (uint32 i = 0; i < _blockUsed; i++)
				for (uint32 j = 0; j < _channels; j++)
					*ptr++ = READ_UINT16(_blockBuffer + i * 2 + j * _interleave);
		}
	}

	_audStream->queuePacket(new Common::MemoryReadStream(buffer, sampleCount * 2, DisposeAfterUse::YES));

	delete packet;
	return true;
}

Audio::AudioStream *MPEGPSDecoder::PS2AudioTrack::getAudioStream() const {
	return _audStream;
}

uint32 MPEGPSDecoder::PS2AudioTrack::calculateSampleCount(uint32 packetSize) const {
	uint32 bytesPerChunk = _interleave * _channels, result = 0;

	// If we have a partial block, subtract the remainder from the size. That
	// gets put towards reading the partial block
	if (_blockPos != 0) {
		packetSize -= bytesPerChunk - _blockPos;
		result += (_interleave / 2) - _blockUsed;
	}

	// Round the number of whole chunks down and then calculate how many samples that gives us
	result += (packetSize / bytesPerChunk) * _interleave / 2;

	// Total up anything we can get from the remainder
	packetSize %= bytesPerChunk;
	if (packetSize > (_channels - 1) * _interleave)
		result += (packetSize - (_channels - 1) * _interleave) / 2;

	return result * _channels;
}

MPEGPSDecoder::ADXAudioTrack::ADXAudioTrack(Common::SeekableReadStream &firstPacket, Audio::Mixer::SoundType soundType) :
		AudioTrack(soundType) {
	if (!Common::parseADXFrameInfo(firstPacket, _info))
		error("Failed to parse embedded ADX header in MPEG-PS video");

	byte flags = Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN;
	if (_info.channelCount == 2)
		flags |= Audio::FLAG_STEREO;

	_audStream = Audio::makePacketizedRawStream(_info.sampleRate, flags);
	_frameBuffer = new byte[_info.frameSize()];
	_frameBufPos = 0;
	for (int c = 0; c < 2; ++c) {
		_hist1[c] = _info.history[c][0];
		_hist2[c] = _info.history[c][1];
	}
	_isFirstPacket = true;
}

MPEGPSDecoder::ADXAudioTrack::~ADXAudioTrack() {
	delete[] _frameBuffer;
	delete _audStream;
}

bool MPEGPSDecoder::ADXAudioTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	if (_isFirstPacket) {
		packet->skip(_info.streamDataOffset);
		_isFirstPacket = false;
	}

	uint32 frameSize = _info.frameSize();
	uint32 sampleBytesPerFrame = _info.samplesPerFrame() * _info.channelCount * sizeof(int16);

	while (!packet->eos()) {
		uint32 avail = packet->size() - packet->pos();
		if (avail == 0)
			break;
		uint32 take = MIN(frameSize - _frameBufPos, avail);
		packet->read(_frameBuffer + _frameBufPos, take);
		_frameBufPos += take;
		if (_frameBufPos < frameSize)
			break;

		_frameBufPos = 0;
		// Stop at the end marker or the header's sample count, like CRI's player.
		if (_samplesDecoded >= _info.sampleCount)
			break;
		int16 *pcm = (int16 *)malloc(sampleBytesPerFrame);
		if (!Common::decodeADXFrame(_frameBuffer, _info, _hist1, _hist2, pcm)) {
			free(pcm);
			_samplesDecoded = _info.sampleCount;
			break;
		}
		const uint32 samples = MIN(_info.samplesPerFrame(), _info.sampleCount - _samplesDecoded);
		_samplesDecoded += samples;
		_audStream->queuePacket(new Common::MemoryReadStream((byte *)pcm, samples * _info.channelCount * sizeof(int16), DisposeAfterUse::YES));
	}

	delete packet;
	return true;
}

Audio::AudioStream *MPEGPSDecoder::ADXAudioTrack::getAudioStream() const {
	return _audStream;
}

// --------------------------------------------------------------------------
// ATRAC audio track (plain ATRAC3 or ATRAC3+)
// --------------------------------------------------------------------------

MPEGPSDecoder::ATRACAudioTrack::ATRACAudioTrack(ATRACCodec codec, Framing framing, uint16 channels,
                                                uint32 sampleRate, uint16 blockAlign,
                                                Audio::Mixer::SoundType soundType)
	: AudioTrack(soundType), _decoder(nullptr), _audStream(nullptr), _framing(framing),
	  _channels(channels), _blockAlign(blockAlign), _frameSamples(0), _esPos(0) {
	if (codec == kATRACCodecATRAC3Plus)
		_decoder = new Audio::ATRAC3PlusFrameDecoder();
	else
		_decoder = new Audio::ATRAC3FrameDecoder(codec == kATRACCodecATRAC3JointStereo
		                                             ? Audio::kATRAC3CodingJointStereo
		                                             : Audio::kATRAC3CodingSingle);

	_decoder->init(channels, blockAlign);
	_frameSamples = _decoder->frameSamples();

	byte flags = Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN;
	if (channels == 2)
		flags |= Audio::FLAG_STEREO;
	_audStream = Audio::makePacketizedRawStream(sampleRate, flags);
}

MPEGPSDecoder::ATRACAudioTrack::~ATRACAudioTrack() {
	delete _decoder;
	delete _audStream;
}

void MPEGPSDecoder::ATRACAudioTrack::queueFrame(const byte *frame, uint32 frameBytes) {
	int16 *pcm = (int16 *)malloc((uint32)_channels * _frameSamples * sizeof(int16));
	if (!pcm)
		return;
	if (!_decoder->decodeFrame(frame, frameBytes, pcm)) {
		free(pcm);
		return;
	}
	_audStream->queuePacket(new Common::MemoryReadStream((byte *)pcm,
		_channels * _frameSamples * sizeof(int16), DisposeAfterUse::YES));
}

void MPEGPSDecoder::ATRACAudioTrack::feed(const byte *data, uint32 size) {
	uint32 old = _es.size();
	_es.resize(old + size);
	memcpy(_es.data() + old, data, size);

	if (_framing == kFramingPlain) {
		while (_blockAlign && _es.size() - _esPos >= _blockAlign) {
			queueFrame(_es.data() + _esPos, _blockAlign);
			_esPos += _blockAlign;
		}
	} else { // kFramingPSMF
		// Real PSMF framing: each frame is preceded by its own 0F D0 sync tag
		// (the 4-byte PSP audio sub-header is stripped per PES in sendPacket).
		while (_esPos + 8 <= _es.size()) {
			if (_es[_esPos] != 0x0F || _es[_esPos + 1] != 0xD0) {
				_esPos++;
				continue;
			}
			uint32 frameSize = (((_es[_esPos + 2] & 0x03) << 8) | (_es[_esPos + 3] * 8)) + 0x10;
			if (frameSize < 8) {
				_esPos++;
				continue;
			}
			if (_esPos + frameSize > _es.size())
				break; // wait for the rest of the frame
			queueFrame(_es.data() + _esPos + 8, frameSize - 8);
			_esPos += frameSize;
		}
	}

	// Drop the consumed prefix so the buffer does not grow without bound.
	if (_esPos > 0) {
		uint32 remain = _es.size() - _esPos;
		if (remain)
			memmove(_es.data(), _es.data() + _esPos, remain);
		_es.resize(remain);
		_esPos = 0;
	}
}

bool MPEGPSDecoder::ATRACAudioTrack::sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) {
	if (!packet)
		return true;

	uint32 size = (uint32)packet->size();
	// PSMF wraps each PES payload in a 4-byte PSP audio sub-header.
	if (_framing == kFramingPSMF) {
		if (size <= 4) {
			delete packet;
			return true;
		}
		packet->skip(4);
		size -= 4;
	}

	Common::Array<byte> chunk(size);
	if (size)
		packet->read(chunk.data(), size);
	delete packet;

	if (size)
		feed(chunk.data(), size);
	return true;
}

Audio::AudioStream *MPEGPSDecoder::ATRACAudioTrack::getAudioStream() const {
	return _audStream;
}

} // End of namespace Video
