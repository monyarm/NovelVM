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

		BinaryFormatVersion Version;
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
		uint16 SpeakerId;
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
	bool readHeader(Common::SeekableReadStream *stream);
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

	Common::String getSpeaker(int index);
	static void ParsePages(Common::Array<int> lineStartAddresses, Common::Array<byte> buffer, BinaryFormatVersion version);

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
