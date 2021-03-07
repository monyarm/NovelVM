#include "formats/audio/adx.h"
#include "video/mpegps_decoder.h"

namespace Format::Audio {

static int nibble_to_int[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                -8, -7, -6, -5, -4, -3, -2, -1};

static inline int get_high_nibble_signed(uint8 n) {
	return nibble_to_int[n >> 4];
}

static inline int get_low_nibble_signed(uint8 n) {
	return nibble_to_int[n & 0xf];
}

static inline int clamp16(int32_t val) {
	if (val > 32767)
		return 32767;
	if (val < -32768)
		return -32768;
	return val;
}

void ADXFile::SetCoefficients(double cutoff, double sample_rate) {
	// https://wiki.multimedia.cx/index.php/CRI_ADX_ADPCM#Coefficients

	/* temps to keep the calculation simple */
	double z, a, b, c;

	z = cos(2.0 * M_PI * cutoff / sample_rate);

	a = M_SQRT2 - z;
	b = M_SQRT2 - 1.0;
	c = (a - sqrt((a + b) * (a - b))) / b;

	/* compute the coefficients as fixed point values, with 12 fractional bits */
	coefficient[0] = (int16)floor(c * 8192);
	coefficient[1] = (int16)floor(c * c * -4096);
}


ADXFile::ADXFile(const char *path) {

	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

ADXFile::ADXFile(Common::SeekableReadStream *stream) {
	readFile(stream);
}

void ADXFile::readFile(Common::SeekableReadStream *stream) {
	stream->seek(2, SEEK_CUR);

	readHeader(stream);

	stream->seek(dat.header.dataoffset);

	readData(stream);
}

void ADXFile::readHeader(Common::SeekableReadStream *stream) {
	dat.header.dataoffset = stream->readUint16BE();
	dat.header.format = (formatEnum)stream->readByte();
	debug("%i", (int)dat.header.format);
	dat.header.blocksize = stream->readByte();
	dat.header.bitsperchannel = stream->readByte();
	dat.header.channelcount = stream->readByte();
	dat.header.samplerate = stream->readUint32BE();
	dat.header.samplecount = stream->readUint32BE();
	dat.header.highpasscutoff = stream->readUint16BE();
	dat.header.loopdatastyle = stream->readByte();
	dat.header.encrypted = stream->readByte();
	debug("%i", dat.header.loopdatastyle);
	if (dat.header.loopdatastyle == 3) {

		stream->seek(4, SEEK_CUR);
	} else {

		stream->seek(16, SEEK_CUR);
	}
	dat.loopdata.loopflag = stream->readUint32BE();
	dat.loopdata.loopstartsample = stream->readUint32BE();
	dat.loopdata.loopstartbyte = stream->readUint32BE();
	dat.loopdata.loopendsample = stream->readUint32BE();
	dat.loopdata.loopendbyte = stream->readUint32BE();
}

void ADXFile::readData(Common::SeekableReadStream *stream) {
	debug("%i", stream->size());
	stream->seek(dat.header.dataoffset-2,SEEK_SET);
	auto cri = stream->readFourCC();
	debug("%s", cri.c_str());
	stream->seek(dat.header.dataoffset +4, SEEK_SET);

	
}
} // namespace Format::Audio
