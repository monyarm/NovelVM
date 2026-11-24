#include "common/formats/utf_table.h"

namespace Common {

namespace {

enum UTFStorage : byte {
	kStorageMask = 0xf0,
	kStorageNone = 0x00,
	kStorageZero = 0x10,
	kStorageConstant = 0x30,
	kStoragePerRow = 0x50,
};

enum UTFType : byte {
	kTypeMask = 0x0f,
	kTypeUint8 = 0,
	kTypeUint8b = 1,
	kTypeUint16 = 2,
	kTypeUint16b = 3,
	kTypeUint32 = 4,
	kTypeUint32b = 5,
	kTypeUint64 = 6,
	kTypeUint64b = 7,
	kTypeFloat = 8,
	kTypeString = 0xA,
	kTypeData = 0xB,
};

uint32 readU32BE(const byte *p) {
	return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | (uint32)p[3];
}

uint16 readU16BE(const byte *p) {
	return (uint16)(((uint16)p[0] << 8) | p[1]);
}

uint64 readU64BE(const byte *p) {
	uint64 v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | p[i];
	return v;
}

uint32 typedSize(byte type) {
	switch (type) {
	case kTypeUint8:
	case kTypeUint8b:
		return 1;
	case kTypeUint16:
	case kTypeUint16b:
		return 2;
	case kTypeUint32:
	case kTypeUint32b:
	case kTypeFloat:
	case kTypeString:
		return 4;
	case kTypeUint64:
	case kTypeUint64b:
		return 8;
	case kTypeData:
		return 8;
	default:
		return 0;
	}
}

} // namespace

Common::String UTFTable::readCString(uint32 pos) const {
	if (pos >= _packet.size())
		return Common::String();
	uint32 end = pos;
	while (end < _packet.size() && _packet[end] != 0)
		end++;
	return Common::String((const char *)&_packet[pos], end - pos);
}

UTFValue UTFTable::readTyped(uint32 &pos, byte type) const {
	UTFValue v;
	v.type = type;
	const byte *p = _packet.data();
	uint32 size = typedSize(type);
	if (size == 0 || pos > _packet.size() || size > _packet.size() - pos)
		return v;
	v.valid = true;

	switch (type) {
	case kTypeUint8:
	case kTypeUint8b:
		v.u = p[pos];
		pos += 1;
		break;
	case kTypeUint16:
	case kTypeUint16b:
		v.u = readU16BE(p + pos);
		pos += 2;
		break;
	case kTypeUint32:
	case kTypeUint32b:
		v.u = readU32BE(p + pos);
		pos += 4;
		break;
	case kTypeUint64:
	case kTypeUint64b:
		v.u = readU64BE(p + pos);
		pos += 8;
		break;
	case kTypeFloat:
		// Stored as a float but only ever consulted as raw bits here; no
		// real field used by any caller so far is a float.
		v.u = readU32BE(p + pos);
		pos += 4;
		break;
	case kTypeString: {
		uint32 strOff = readU32BE(p + pos);
		pos += 4;
		if (strOff > _packet.size() - _stringsOffset) {
			v.valid = false;
			break;
		}
		v.str = readCString(_stringsOffset + strOff);
		break;
	}
	case kTypeData: {
		// Byte-array columns: 8 bytes = offset (u32 BE) + length (u32 BE)
		// into the data segment.
		v.dataOffset = readU32BE(p + pos);
		v.dataLength = readU32BE(p + pos + 4);
		pos += 8;
		v.valid = true;
		break;
	}
	default:
		v.valid = false;
		break;
	}
	return v;
}

bool UTFTable::parse(const byte *buf, uint32 size) {
	if (size < 32 || memcmp(buf, "@UTF", 4) != 0)
		return false;

	_packet.resize(size);
	memcpy(_packet.data(), buf, size);
	const byte *p = _packet.data();

	uint32 rowsOffsetRaw = readU32BE(p + 8);
	uint32 stringsOffsetRaw = readU32BE(p + 12);
	uint32 dataOffsetRaw = readU32BE(p + 16);
	if (rowsOffsetRaw > size - 8 || stringsOffsetRaw > size - 8 || dataOffsetRaw > size - 8)
		return false;
	_rowsOffset = rowsOffsetRaw + 8;
	_stringsOffset = stringsOffsetRaw + 8;
	_dataOffset = dataOffsetRaw + 8;
	// table_name (a string offset, at p+20) is unused here.
	uint16 numColumns = readU16BE(p + 24);
	_rowLength = readU16BE(p + 26);
	uint32 numRows = readU32BE(p + 28);
	if (numRows > 0x7fffffffU || (uint64)_rowLength * numRows > size - _rowsOffset)
		return false;
	_numRows = (int)numRows;

	uint32 off = 32;
	uint32 rowDataSize = 0;
	_columns.clear();
	for (uint16 i = 0; i < numColumns; i++) {
		if (off > size - 1)
			return false;
		byte flags = p[off];
		off++;
		if (flags == 0) {
			if (off > size - 4)
				return false;
			off += 3;
			flags = p[off];
			off++;
		}
		if (off > size - 4)
			return false;

		UTFColumn col;
		col.flags = flags;
		uint32 nameOff = readU32BE(p + off);
		off += 4;
		if (nameOff > size - _stringsOffset)
			return false;
		col.name = readCString(_stringsOffset + nameOff);

		if ((flags & kStorageMask) == kStorageConstant) {
			col.constantValue = readTyped(off, flags & kTypeMask);
			if (!col.constantValue.valid)
				return false;
		} else if ((flags & kStorageMask) == kStoragePerRow) {
			uint32 width = typedSize(flags & kTypeMask);
			if (width == 0 || width > _rowLength || rowDataSize > _rowLength - width)
				return false;
			rowDataSize += width;
		}

		_columns.push_back(col);
	}

	return true;
}

UTFValue UTFTable::get(int row, const char *name) const {
	if (row < 0 || row >= _numRows)
		return UTFValue();
	uint32 pos = _rowsOffset + (uint32)row * _rowLength;
	UTFValue result;

	for (uint32 i = 0; i < _columns.size(); i++) {
		const UTFColumn &col = _columns[i];
		byte storage = col.flags & kStorageMask;
		byte type = col.flags & kTypeMask;

		if (storage == kStorageConstant) {
			if (!result.valid && col.name == name)
				result = col.constantValue;
			continue;
		}
		if (storage == kStorageNone || storage == kStorageZero)
			continue;

		// kStoragePerRow: value lives inline in this row; must always be
		// consumed (even when it's not the column we want) to keep `pos`
		// advancing correctly for the columns after it.
		UTFValue v = readTyped(pos, type);
		if (!result.valid && col.name == name)
			result = v;
	}

	return result;
}

} // namespace Common
