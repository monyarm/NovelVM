#ifndef ADX_H
#define ADX_H

#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"

#include "common/str.h"
#include "graphics/colormasks.h"
#include "graphics/surface.h"
#include "graphics/transparent_surface.h"

enum class formatEnum : byte {
	fixed = 2,
	ADX = 3,
	ADXexp = 4,
	AHX = 0x11
};

struct ADXHeader {
	uint16 dataoffset;
	formatEnum format;
	byte blocksize;
	byte bitsperchannel;
	byte channelcount;
	uint32 samplerate;
	uint32 samplecount;
	uint16 highpasscutoff;
	byte loopdatastyle;
	byte encrypted;
};

struct ADXLoopData {
    uint32 loopflag;
    uint32 loopstartsample;
    uint32 loopstartbyte;
    uint32 loopendsample;
    uint32 loopendbyte;
};

struct ADXData {
	ADXHeader header;
    ADXLoopData loopdata;
};

class ADXFile {
public:
	ADXFile(const char *path);
	ADXFile(Common::SeekableReadStream *stream);
	~ADXFile(){};

private:
	ADXData dat;

	void readFile(Common::SeekableReadStream *stream);

	void readData(Common::SeekableReadStream *f);

	void readHeader(Common::SeekableReadStream *f);
};

#endif
