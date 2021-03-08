#include "bmd.h"
#include "common/array.h"
#include "common/endian.h"
#include "common/list.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace SMT::Format::Script {

BMD::BMD(const char *path) {

	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

BMD::BMD(Common::SeekableReadStream *stream) {
	readFile(stream);
}

void BMD::readFile(Common::SeekableReadStream *stream) {
	readHeader(stream);
	readRelocationTable(stream);
	readDialogHeaders(stream);
	readSpeakerTableHeader(stream);

	
}

void BMD::readHeader(Common::SeekableReadStream *stream) {
	mPositionBase = stream->pos();
	header.FileType = stream->readByte();
	header.IsCompressed = stream->readByte() != 0;
	header.UserId = stream->readSint16LE();
	header.FileSize = stream->readSint32LE();
	stream->read(header.Magic, 4);
	header.Field0C = stream->readSint32LE();
	header.RelocationTable.Offset = stream->readSint32LE();
	header.RelocationTableSize = stream->readSint32LE();
	header.DialogCount = stream->readSint32LE();
	header.IsRelocated = stream->readSint16LE() != 0;
	header.Field1E = stream->readSint16LE();

	if (memcmp(header.Magic, BMDHeader::MAGIC_V1, 4) == 0 || memcmp(header.Magic, BMDHeader::MAGIC_V0, 4) == 0) {
		Endianness = 0;
	} else if (memcmp(header.Magic, BMDHeader::MAGIC_V1_BE, 4) == 0) {
		Endianness = 1;
		swapHeader();
	} else {
		warning("BMD Header magic value does not match");
	}
}

void BMD::readRelocationTable(Common::SeekableReadStream *stream) {
	if (header.RelocationTable.Offset != 0) {
		int oldPos = stream->pos();
		stream->seek(mPositionBase + header.RelocationTable.Offset);
		header.RelocationTable.Value = Common::Array<byte>(header.RelocationTableSize);
		stream->read(header.RelocationTable.Value.data(), header.RelocationTableSize);
		stream->seek(oldPos);
	}
}
void BMD::readDialogHeaders(Common::SeekableReadStream *stream) {
	dialogHeaders = Common::Array<BinaryDialogHeader>(header.DialogCount);

	for (uint i = 0; i < dialogHeaders.size(); i++) {
		dialogHeaders[i].DialogKind = (BinaryDialogKind)(Endianness ? stream->readSint32BE() : stream->readSint32LE());
		dialogHeaders[i].Dialog.Offset = (Endianness ? stream->readSint32BE() : stream->readSint32LE());

		if (dialogHeaders[i].Dialog.Offset != 0) {

			switch (dialogHeaders[i].DialogKind) {
			case BinaryDialogKind::Message:
				readMessageDialog(stream, &dialogHeaders[i].Dialog.Value.bmd, dialogHeaders[i].Dialog.Offset);

				break;
			case BinaryDialogKind::Selection:
				readSelectionDialog(stream, &dialogHeaders[i].Dialog.Value.bsd, dialogHeaders[i].Dialog.Offset);

				break;
			default:
				error("Unknown message type: %#08x", dialogHeaders[i].DialogKind);
				break;
			}
		}
	}
}

void BMD::readSpeakerTableHeader(Common::SeekableReadStream *stream) {

	speakerTableHeader.SpeakerNameArray.Offset = (Endianness ? stream->readSint32BE() : stream->readSint32LE());
	speakerTableHeader.SpeakerCount = (Endianness ? stream->readSint32BE() : stream->readSint32LE());
	speakerTableHeader.Field08 = (Endianness ? stream->readSint32BE() : stream->readSint32LE());
	speakerTableHeader.Field0C = (Endianness ? stream->readSint32BE() : stream->readSint32LE());

	if (speakerTableHeader.SpeakerNameArray.Offset != 0)
		speakerTableHeader.SpeakerNameArray.Value = readSpeakerNames(stream, speakerTableHeader.SpeakerNameArray.Offset, speakerTableHeader.SpeakerCount);

	// if (header.Field08 != 0)
	// 	Debug.WriteLine($ "{nameof( BinarySpeakerTableHeader )}.{nameof( header.Field08 )} = {header.Field08}");

	// if (header.Field0C != 0)
	// 	Debug.WriteLine($ "{nameof( BinarySpeakerTableHeader )}.{nameof( header.Field0C )} = {header.Field0C}");
}

Common::Array<Common::OffsetTo<Common::Array<byte> > > BMD::readSpeakerNames(Common::SeekableReadStream *stream, uint32 address, int count) {

	stream->seek(mPositionBase + BMDHeader::SIZE + address);

	Common::Array<int32> speakerNameAddresses;
	//stream->read(speakerNameAddresses.data(), count * 4);
	for (int i = 0; i < count; i++) {
		speakerNameAddresses.push_back((Endianness ? stream->readSint32BE() : stream->readSint32LE()));
	}

	auto speakerNames = Common::Array<Common::OffsetTo<Common::Array<byte> > >(count);

	for (uint i = 0; i < speakerNameAddresses.size(); i++) {

		stream->seek(mPositionBase + BMDHeader::SIZE + speakerNameAddresses[i]);
		Common::Array<byte> bytes;
		while (true) {
			byte b = stream->readByte();
			if (b == 0)
				break;

			bytes.push_back(b);
		}

		speakerNames[i] = Common::OffsetTo<Common::Array<byte> >(speakerNameAddresses[i], bytes);
	}

	return speakerNames;
}

void BMD::readMessageDialog(Common::SeekableReadStream *stream, BinaryMessageDialog *message, int offset) {

	int oldPos = stream->pos();
	stream->seek(mPositionBase + BMDHeader::SIZE + offset);
	const char *_name = new const char[24]();
	stream->read((void *)_name, 24);
	message->Name = Common::String(_name, 24);
	//debug("%s", _name);
	message->PageCount = (Endianness ? stream->readSint16BE() : stream->readSint16LE());
	// speakerId = 0xFFFF = use no speaker name?
	// speakerId = 0x8000 = use variable speaker name?
	message->SpeakerId = (Endianness ? stream->readUint16BE() : stream->readUint16LE());

	if (message->PageCount > 0) {
		message->PageStartAddresses = Common::Array<int32>(message->PageCount);
		stream->read(message->PageStartAddresses.data(), message->PageCount * 4);
		message->TextBufferSize = (Endianness ? stream->readSint32BE() : stream->readSint32LE());
		message->TextBuffer = Common::Array<byte>(message->TextBufferSize);
		stream->read(message->TextBuffer.data(), message->TextBufferSize);
		//Common::hexdump(message->TextBuffer.data(), message->TextBufferSize);
	} else {
		//message.PageStartAddresses = null;
		message->TextBufferSize = 0;
		//message.TextBuffer = null;
	}

	stream->seek(oldPos);
}

void BMD::readSelectionDialog(Common::SeekableReadStream *stream, BinarySelectionDialog *message, int offset) {
	int oldPos = stream->pos();
	stream->seek(mPositionBase + BMDHeader::SIZE + offset);

	const char *_name = new const char[24]();
	stream->read((void *)_name, 24);
	message->Name = Common::String(_name, 24);
	//debug("%s", _name);

	message->Field18 = (Endianness ? stream->readSint16BE() : stream->readSint16LE());
	message->OptionCount = (Endianness ? stream->readSint16BE() : stream->readSint16LE());
	message->Field1C = (Endianness ? stream->readSint16BE() : stream->readSint16LE());
	message->Field1E = (Endianness ? stream->readSint16BE() : stream->readSint16LE());

	message->OptionStartAddresses = Common::Array<int32>(message->OptionCount);
	stream->read(message->OptionStartAddresses.data(), message->OptionCount * 4);
	message->TextBufferSize = (Endianness ? stream->readSint32BE() : stream->readSint32LE());
	message->TextBuffer = Common::Array<byte>(message->TextBufferSize);
	stream->read(message->TextBuffer.data(), message->TextBufferSize);
	//Common::hexdump(message->TextBuffer.data(), message->TextBufferSize);

	if (message->Field18 != 0)
		debug("%s = %#08x", "BinarySelectionDialog.Field18", message->Field18);

	if (message->Field1C != 0)
		debug("%s = %#08x", "BinarySelectionDialog.Field1C", message->Field1C);

	if (message->Field1E != 0)
		debug("%s = %#08x", "BinarySelectionDialog.Field1E", message->Field1E);

	stream->seek(oldPos);
}

void BMD::swapHeader() {
	header.UserId = SWAP_BYTES_16(header.UserId);
	header.FileSize = SWAP_BYTES_32(header.FileSize);
	header.Field0C = SWAP_BYTES_32(header.Field0C);
	header.RelocationTable.Offset = SWAP_BYTES_32(header.RelocationTable.Offset);
	header.RelocationTableSize = SWAP_BYTES_32(header.RelocationTableSize);
	header.DialogCount = SWAP_BYTES_32(header.DialogCount);
	header.Field1E = SWAP_BYTES_16(header.Field1E);
}

} // namespace SMT::Format::Script