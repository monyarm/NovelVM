#include "formats/audio/adx.h"

namespace Format::Audio {

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
}
} // namespace Format::Audio
