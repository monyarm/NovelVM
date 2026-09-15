#include "ikura/formats/script/script.h"

namespace Ikura::Format::Script {

namespace {

bool readU32(const Common::Array<byte> &buf, uint32 pos, uint32 &out) {
	if (pos + 4 > buf.size())
		return false;
	out = buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16) | ((uint32)buf[pos + 3] << 24);
	return true;
}

// Both on-disk variants use this same rotate transform (rotate-left-6,
// equivalently rotate-right-2) on every byte from `from` onward. Despite
// the reference field name "xorkey", it's a bit rotation, not an XOR.
void decrypt(Common::Array<byte> &buf, uint32 from) {
	for (uint32 i = from; i < buf.size(); i++)
		buf[i] = ((buf[i] << 6) | (buf[i] >> 2)) & 0xFF;
}

} // namespace

Script *Script::load(Common::SeekableReadStream &stream) {
	uint32 len = stream.size();
	Common::Array<byte> raw(len);
	stream.seek(0);
	if (stream.read(raw.data(), len) != len)
		return nullptr;

	Script *script = new Script();
	static const char kDRSMagic[] = "DigitalRomanceSystem.";
	bool isDRS = len >= sizeof(kDRSMagic) - 1 && memcmp(raw.data(), kDRSMagic, sizeof(kDRSMagic) - 1) == 0;
	bool ok = isDRS ? script->loadDRS(raw) : script->loadISF(raw);
	if (!ok) {
		delete script;
		return nullptr;
	}
	return script;
}

bool Script::loadISF(const Common::Array<byte> &raw) {
	uint32 ssoffset;
	if (!readU32(raw, 0, ssoffset) || ssoffset < 8 || (ssoffset - 8) % 4 != 0 || ssoffset > raw.size())
		return false;
	uint32 tableCount = (ssoffset - 8) / 4;

	_buffer = raw;
	decrypt(_buffer, 8);

	_jumpTable.resize(tableCount);
	for (uint32 i = 0; i < tableCount; i++) {
		uint32 v;
		if (!readU32(_buffer, 8 + i * 4, v))
			return false;
		_jumpTable[i] = v;
	}

	_revision = _buffer[5] | (_buffer[4] << 8); // reference reads this big-endian, faithfully kept
	_xorKey = _buffer[6];
	_scriptStart = ssoffset;
	_positions.clear();
	_positions.push_back(ssoffset);
	return true;
}

bool Script::loadDRS(const Common::Array<byte> &raw) {
	const uint32 kHeaderEnd = 0x19;
	if (raw.size() < kHeaderEnd)
		return false;

	_buffer = raw;
	decrypt(_buffer, kHeaderEnd);

	uint32 pos = kHeaderEnd;

	// Jump table: [size:4][count:4][size bytes of uint32 entries]
	uint32 tsize;
	if (!readU32(_buffer, pos, tsize))
		return false;
	pos += 8; // skip size + count
	if (tsize % 4 != 0)
		return false;
	uint32 tableCount = tsize / 4;
	_jumpTable.resize(tableCount);
	for (uint32 i = 0; i < tableCount; i++) {
		uint32 v;
		if (!readU32(_buffer, pos + i * 4, v))
			return false;
		_jumpTable[i] = v;
	}
	pos += tsize;

	// Reference reads and discards two more [size:4][count:4][data] tables
	// here ("unknown table" and "calltable") without using either - DRS
	// calls apparently never needed them. Skip the same way.
	for (int table = 0; table < 2; table++) {
		if (!readU32(_buffer, pos, tsize))
			return false;
		pos += 8 + tsize;
	}

	// Script data: [size:4] then the instruction stream itself
	if (!readU32(_buffer, pos, tsize))
		return false;
	pos += 4;
	if (pos > _buffer.size())
		return false;

	_scriptStart = pos;
	_positions.clear();
	_positions.push_back(pos);
	return true;
}

bool Script::getOpcode(byte &opcode, const byte *&data, uint32 &length) {
	if (_positions.empty())
		return false;
	uint32 pos = _positions.back();
	if (pos + 2 > _buffer.size())
		return false;

	uint32 lengthField, headerSize;
	if (_buffer[pos + 1] & 0x80) {
		if (pos + 3 > _buffer.size())
			return false;
		lengthField = ((_buffer[pos + 1] & 0x7F) << 8) | _buffer[pos + 2];
		headerSize = 3;
	} else {
		lengthField = _buffer[pos + 1];
		headerSize = 2;
	}
	if (lengthField < headerSize)
		return false;
	uint32 payloadLength = lengthField - headerSize;
	if (pos + headerSize + payloadLength > _buffer.size())
		return false;

	opcode = _buffer[pos];
	data = _buffer.data() + pos + headerSize; // may be one-past-the-end when payloadLength is 0; only valid to form, not index
	length = payloadLength;
	_positions.back() = pos + headerSize + payloadLength;
	return true;
}

bool Script::jump(uint16 index) {
	if (_positions.empty() || index >= _jumpTable.size())
		return false;
	_positions.back() = _scriptStart + _jumpTable[index];
	return true;
}

bool Script::call(uint16 index) {
	if (_positions.empty())
		return false;
	_positions.push_back(_positions.back());
	if (!jump(index)) {
		_positions.pop_back();
		return false;
	}
	return true;
}

bool Script::ret() {
	if (_positions.empty())
		return false;
	_positions.pop_back();
	return true;
}

} // End of namespace Ikura::Format::Script
