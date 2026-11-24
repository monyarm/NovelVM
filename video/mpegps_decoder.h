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

#ifndef VIDEO_MPEGPS_DECODER_H
#define VIDEO_MPEGPS_DECODER_H

#include "common/formats/adx.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/queue.h"
#include "graphics/surface.h"
#include "video/video_decoder.h"

namespace Audio {
class ATRACFrameDecoder;
class PacketizedAudioStream;
}

namespace Common {
class SeekableReadStream;
}

namespace Graphics {
struct PixelFormat;
}

namespace Video {

/** True when a packet contains an H.264 Annex-B SPS/PPS rather than MPEG-PS video. */
bool isH264AnnexBPacket(const byte *data, uint32 size);

class MPEGFrameDecoder;
class H264FrameDecoder;

/**
 * Decoder for MPEG Program Stream videos.
 * Video decoder used in engines:
 *  - mtropolis
 *  - qdengine
 *  - zvision
 */
class MPEGPSDecoder : public VideoDecoder {
public:
	// Which ATRAC codec a container's audio stream carries.
	enum ATRACCodec {
		kATRACCodecATRAC3,             // plain ATRAC3, SINGLE (per-channel) coding
		kATRACCodecATRAC3JointStereo,  // plain ATRAC3, JOINT_STEREO coding
		kATRACCodecATRAC3Plus
	};

	MPEGPSDecoder(double decibel = 0.0);
	virtual ~MPEGPSDecoder();

	bool loadStream(Common::SeekableReadStream *stream) override;
	void close() override;

	// Set the number of prebuffered packets in demuxer
	// Used only by qdEngine
	void setPrebufferedPackets(int packets);

	// Container-supplied ATRAC audio parameters (e.g. a PSMF audio stream
	// table). When set, the matching private stream is decoded by a
	// decoder-owned ATRACAudioTrack so the VideoDecoder clocks the audio
	// (rather than the engine playing it separately through the mixer).
	void setATRACAudioInfo(ATRACCodec codec, uint16 channels, uint32 sampleRate, uint16 blockAlign);

	// Whether getTime() follows a playing audio track (AV sync). Off by
	// default; containers with trustworthy video timestamps can enable it.
	void setUseAudioSync(bool enabled) { _useAudioSync = enabled; }

protected:
	void readNextPacket() override;
	bool useAudioSync() const override { return _useAudioSync; }

private:
	class MPEGPSDemuxer {
	public:
		MPEGPSDemuxer();
		~MPEGPSDemuxer();

		bool loadStream(Common::SeekableReadStream *stream);
		void close();

		Common::SeekableReadStream *getFirstVideoPacket(int32 &startCode, uint32 &pts, uint32 &dts);
		Common::SeekableReadStream *getNextPacket(uint32 currentTime, int32 &startCode, uint32 &pts, uint32 &dts);

		void setPrebufferedPackets(int packets) { _prebufferedPackets = packets; }

	private:
		class Packet {
		public:
			Packet(Common::SeekableReadStream *stream, int32 startCode, uint32 pts, uint32 dts) : _stream(stream), _startCode(startCode), _pts(pts), _dts(dts) {}

			Common::SeekableReadStream *_stream;
			int32 _startCode;
			uint32 _pts;
			uint32 _dts;
		};
		bool queueNextPacket();
		bool fillQueues();
		int readNextPacketHeader(int32 &startCode, uint32 &pts, uint32 &dts);
		int findNextStartCode(uint32 &size);
		uint32 readPTS(int c);
		void parseProgramStreamMap(int length);

		Common::SeekableReadStream *_stream;
		Common::Queue<Packet> _videoQueue;
		Common::Queue<Packet> _audioQueue;
		// If we come across a non-packetized elementary stream
		bool _isESStream;

		uint32 _firstAudioPacketPts = 0xFFFFFFFF;
		uint32 _firstVideoPacketPts = 0xFFFFFFFF;

		int _prebufferedPackets = 150;
	};

	// Base class for handling MPEG streams
	class MPEGStream {
	public:
		virtual ~MPEGStream() {}

		enum StreamType {
			kStreamTypeVideo,
			kStreamTypeAudio
		};

		virtual bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) = 0;
		virtual StreamType getStreamType() const = 0;
	};

	// An MPEG 1/2 video track
	class MPEGVideoTrack : public VideoTrack, public MPEGStream {
	public:
		MPEGVideoTrack(Common::SeekableReadStream *firstPacket);
		~MPEGVideoTrack();

		bool endOfTrack() const override { return _endOfTrack; }
		uint16 getWidth() const override;
		uint16 getHeight() const override;
		Graphics::PixelFormat getPixelFormat() const override;
		bool setOutputPixelFormat(const Graphics::PixelFormat &format) override;
		int getCurFrame() const override { return _curFrame; }
		uint32 getNextFrameStartTime() const override { return _nextFrameStartTime.msecs(); }
		const Graphics::Surface *decodeNextFrame() override;

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeVideo; }

		void setEndOfTrack() { _endOfTrack = true; }

	private:
		bool _endOfTrack;
		int _curFrame;
		uint32 _framePts;
		Audio::Timestamp _nextFrameStartTime;
		Graphics::Surface *_surface;

		uint16 _width;
		uint16 _height;
		Graphics::PixelFormat _pixelFormat;

		void findDimensions(Common::SeekableReadStream *firstPacket);

#ifdef USE_MPEG2

		MPEGFrameDecoder *_decoder;
#endif

	};

#ifdef USE_OPENH264
	// An H.264/AVC video track (PSP PSMF openings). PES payloads carry
	// Annex-B NAL units; OpenH264 decodes them to YUV420 which is converted
	// into a surface the same way USM's VP9 path does.
	class H264VideoTrack : public VideoTrack, public MPEGStream {
	public:
		H264VideoTrack(Common::SeekableReadStream *firstPacket);
		~H264VideoTrack();

		bool endOfTrack() const override { return _endOfTrack; }
		uint16 getWidth() const override;
		uint16 getHeight() const override;
		Graphics::PixelFormat getPixelFormat() const override;
		bool setOutputPixelFormat(const Graphics::PixelFormat &format) override;
		int getCurFrame() const override { return _curFrame; }
		uint32 getNextFrameStartTime() const override { return _nextFrameStartTime.msecs(); }
		const Graphics::Surface *decodeNextFrame() override;

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeVideo; }

		void setEndOfTrack() { _endOfTrack = true; }

	private:
		bool _endOfTrack;
		int _curFrame;
		uint32 _framePts;
		uint32 _basePts;
		bool _haveBasePts;
		Audio::Timestamp _nextFrameStartTime;

		uint16 _width;
		uint16 _height;
		Graphics::PixelFormat _pixelFormat;

		H264FrameDecoder *_decoder;
	};
#endif // USE_OPENH264

#ifdef USE_MAD
	// An MPEG audio track
	class MPEGAudioTrack : public AudioTrack, public MPEGStream {
	public:
		MPEGAudioTrack(Common::SeekableReadStream &firstPacket, Audio::Mixer::SoundType soundType);
		~MPEGAudioTrack();

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeAudio; }

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		Audio::PacketizedAudioStream *_audStream;
	};
#endif

#ifdef USE_A52
	class AC3AudioTrack : public AudioTrack, public MPEGStream {
	public:
		AC3AudioTrack(Common::SeekableReadStream &firstPacket, double decibel, Audio::Mixer::SoundType soundType);
		~AC3AudioTrack();

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeAudio; }

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		Audio::PacketizedAudioStream *_audStream;
	};
#endif

	// CRI ADX embedded directly in the "MPEG audio" stream ID range
	// instead of real MPEG audio -- real-data-verified in Persona 3/4's
	// PS2 FMV (.SFD files under DATA.CVM). See common/formats/adx.h.
	class ADXAudioTrack : public AudioTrack, public MPEGStream {
	public:
		ADXAudioTrack(Common::SeekableReadStream &firstPacket, Audio::Mixer::SoundType soundType);
		~ADXAudioTrack();

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeAudio; }

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		Common::ADXFrameInfo _info;
		Audio::PacketizedAudioStream *_audStream;
		bool _isFirstPacket;

		byte *_frameBuffer;
		uint32 _frameBufPos;
		int32 _hist1[2];
		int32 _hist2[2];
	};

	class PS2AudioTrack : public AudioTrack, public MPEGStream {
	public:
		PS2AudioTrack(Common::SeekableReadStream *firstPacket, Audio::Mixer::SoundType soundType);
		~PS2AudioTrack();

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeAudio; }

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		Audio::PacketizedAudioStream *_audStream;

		enum {
			PS2_PCM = 0x01,
			PS2_ADPCM = 0x10
		};

		uint32 _channels;
		uint32 _soundType;
		uint32 _interleave;
		bool _isFirstPacket;

		byte *_blockBuffer;
		uint32 _blockPos, _blockUsed;

		uint32 calculateSampleCount(uint32 packetSize) const;
	};

	// Decoder-owned streaming ATRAC audio (plain ATRAC3 or ATRAC3+). The
	// frame decoder is supplied by whichever codec the container carries;
	// this class only handles PES-payload-to-frame reassembly, so both
	// codecs share one implementation. Used for PSP PSMF openings (real
	// data: ATRAC3+ tagged 0F D0 behind a 4-byte PSP audio sub-header),
	// replacing the old engine-side mixer playback that could drift.
	class ATRACAudioTrack : public AudioTrack, public MPEGStream {
	public:
		enum Framing {
			kFramingPlain,        // raw blockAlign frames, no per-packet header
			kFramingPSMF          // 4-byte PSP audio sub-header + 0F D0-tagged frames
		};

		ATRACAudioTrack(ATRACCodec codec, Framing framing, uint16 channels, uint32 sampleRate,
		                uint16 blockAlign, Audio::Mixer::SoundType soundType);
		~ATRACAudioTrack();

		bool sendPacket(Common::SeekableReadStream *packet, uint32 pts, uint32 dts) override;
		StreamType getStreamType() const override { return kStreamTypeAudio; }

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		void feed(const byte *data, uint32 size);
		void queueFrame(const byte *frame, uint32 frameBytes);

		Audio::ATRACFrameDecoder *_decoder;
		Audio::PacketizedAudioStream *_audStream;
		Framing _framing;
		uint16 _channels;
		uint16 _blockAlign;
		int _frameSamples;

		Common::Array<byte> _es; // unconsumed elementary-stream bytes
		uint32 _esPos;
	};

	// The different types of private streams we can detect at the moment
	enum PrivateStreamType {
		kPrivateStreamUnknown,
		kPrivateStreamAC3,
		kPrivateStreamDTS,
		kPrivateStreamDVDPCM,
		kPrivateStreamPS2Audio,
		kPrivateStreamATRAC3Plus
	};

	PrivateStreamType detectPrivateStreamType(Common::SeekableReadStream *packet);

	bool addFirstVideoTrack();
	MPEGStream *getStream(uint32 startCode, Common::SeekableReadStream *packet);

	MPEGPSDemuxer *_demuxer;

	// A map from stream types to stream handlers
	typedef Common::HashMap<int, MPEGStream *> StreamMap;
	StreamMap _streamMap;

	// Optional container-supplied ATRAC audio parameters (see setATRACAudioInfo).
	bool _hasATRACInfo = false;
	ATRACCodec _atracCodec = kATRACCodecATRAC3;
	uint16 _atracChannels = 0;
	uint32 _atracSampleRate = 0;
	uint16 _atracBlockAlign = 0;
	bool _useAudioSync = false;

	double _decibel;
};

} // End of namespace Video

#endif
