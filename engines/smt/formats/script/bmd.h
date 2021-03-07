#ifndef SMT_BMDFILE_H
#define SMT_BMDFILE_H

#include "common/list.h"
#include "common/offsetto.h"
#include "common/ptr.h"
#include "common/str-array.h"
#include "common/str.h"

#include "common/bufferedstream.h"
#include "common/debug.h"
#include "common/file-format.h"
#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "common/util.h"

namespace SMT::Format::Script {

class BMD {
	enum BinaryDialogKind {
		Message,
		Selection
	};

	enum BinaryFormatVersion : uint32 {
		Unknown = 1 << 0,
		Version1 = 1 << 1,
		Version1DDS = 1 << 2,
		BigEndian = 1 << 15,
		Version1BigEndian = Version1 | BigEndian
	};

	struct BMDHeader {

		static const byte SIZE = 32;
		static const byte FILE_TYPE = 7;
		constexpr static const byte MAGIC_V0[4] = {'M', 'S', 'G', '0'};
		constexpr static const byte MAGIC_V1[4] = {'M', 'S', 'G', '1'};
		constexpr static const byte MAGIC_V1_BE[4] = {'1', 'G', 'S', 'M'};

		byte FileType;
		bool IsCompressed;
		int16 UserId;
		int32 FileSize;
		byte Magic[4];
		int32 Field0C;
		Common::OffsetTo<Common::Array<byte> > RelocationTable;
		int32 RelocationTableSize;
		int32 DialogCount;
		bool IsRelocated;
		int16 Field1E;
	};


	struct BinarySpeakerTableHeader {
		const byte SIZE = 10;
		Common::OffsetTo<Common::Array<Common::OffsetTo<Common::Array<byte> > > > SpeakerNameArray;
		int SpeakerCount;
		int Field08;
		int Field0C;
	};

	struct BinaryMessageDialog {
		Common::String Name;
		short PageCount;
		ushort SpeakerId;
		Common::Array<int32> PageStartAddresses;
		int TextBufferSize;
		Common::Array<byte> TextBuffer;
	};

	struct BinarySelectionDialog {
		Common::String Name;
		short Field18;
		short OptionCount;
		short Field1C;
		short Field1E;
		Common::Array<int32> OptionStartAddresses;
		int TextBufferSize;
		Common::Array<byte> TextBuffer;
	};

	//Should ideally be union, but there were issues with constructors and deconstructors
	struct DialogObject{
		BinaryMessageDialog bmd;
		BinarySelectionDialog bsd;
	};

	struct BinaryDialogHeader {
		//const byte SIZE = 8;
		//const byte IDENTIFIER_LENGTH = 24;

		BinaryDialogKind DialogKind;

		Common::OffsetTo<DialogObject> Dialog;
	};
	
	void readFile(Common::SeekableReadStream *stream);
	void readHeader(Common::SeekableReadStream *stream);
	void readRelocationTable(Common::SeekableReadStream *stream);
	void readDialogHeaders(Common::SeekableReadStream *stream);
	void readMessageDialog(Common::SeekableReadStream *stream, BinaryMessageDialog *message, int offset);
	void readSelectionDialog(Common::SeekableReadStream *stream, BinarySelectionDialog *message, int offset);
	void readSpeakerTableHeader(Common::SeekableReadStream *stream);
	Common::Array<Common::OffsetTo<Common::Array<byte> > > readSpeakerNames(Common::SeekableReadStream *stream, uint32 address, int count);
	void swapHeader();

	BMDHeader header;

	Common::Array<BinaryDialogHeader> dialogHeaders;
	BinarySpeakerTableHeader speakerTableHeader;

	bool Endianness;
	int32 mPositionBase;
	//typedef Common::HashMap<Common::String, Common::ScopedPtr<BinaryMessageDialog>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> BinaryMessageMap;
	//typedef Common::HashMap<Common::String, Common::ScopedPtr<BinarySelectionDialog>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> BInarySelectionMap;

public:
	BMD(const char *path);
	BMD(Common::SeekableReadStream *stream);

	
};
} // namespace SMT::Format::Script

#endif