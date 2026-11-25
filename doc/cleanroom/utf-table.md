# CRI `@UTF` tables: clean-room specification

This document specifies CRI Middleware's `@UTF` binary table format in enough detail to
write `Common::UTFTable` (`common/formats/utf_table.{h,cpp}`) from it alone. The same
table format is used by the CPK archive reader (`common/formats/cpk.cpp`), the ACB cue
sheet reader (`common/formats/acb.cpp`) and the USM movie readers
(`common/formats/usm.cpp`, `video/usm_decoder.cpp`). The table is read-only for
NovelVM.

## Provenance and confidence tags

Every claim below carries one of these tags:

* **[VERIFIED]**: checked by black-box byte inspection of the retail tables listed in
  section 9 with throwaway Python readers written for this document.
* **[VGMSTREAM]**: taken from vgmstream (ISC license), `src/util/cri_utf.c` and its
  callers in `src/meta/` (`cpk.c`, `acb.c`, `crid.c`, `aax.c`, `csb.c`).
* **[LIBCPK]**: taken from the UTF reader and the table "decryption" routine in
  CriPakTools' LibCPK (`CriPakTools/CPK.cs`). LibCPK has no license. Nothing from it
  may be copied. It is used here only as a description of behaviour.
* **[TEST]**: required by the existing NovelVM tests `test/common/formats/cpk.h`,
  `test/common/formats/acb_awb.h` and `test/common/formats/usm.h`.
* **[DESIGN]**: a NovelVM decision, not a fact about the format. The implementer must
  follow it so that behaviour is predictable. The comparator should not treat it as a
  format claim.
* **[UNCERTAIN]**: not confirmed by any allowed source or by real data.

All multi-byte integers inside a `@UTF` table are **big-endian**. [VERIFIED] All
offsets in this document are hexadecimal. "u16be" means a big-endian 16-bit unsigned
integer.

## 1. Terms

* **Buffer**: the bytes the caller hands to `parse(buf, size)`. It starts at the
  `"@UTF"` magic. It may be longer than the table (see 2.2).
* **Table base**: byte 0 of the buffer, where the magic is.
* **Offset base**: table base + 8. Four header fields are stored relative to it.
* **Extent**: the number of bytes, counted from the table base, that belong to the
  table. Defined in 2.2.
* **Segments**: the table body is split into five consecutive parts: header, schema,
  rows, strings, data. [VERIFIED, VGMSTREAM]

## 2. Header (0x20 bytes)

### 2.1 Fields

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | 4 | ASCII `"@UTF"` (`40 55 54 46`) |
| 0x04 | 4 | u32be **table size**: number of table bytes after this field. Full table length = value + 8. |
| 0x08 | 2 | u16be **version** |
| 0x0A | 2 | u16be **rows offset**, relative to the offset base |
| 0x0C | 4 | u32be **strings offset**, relative to the offset base |
| 0x10 | 4 | u32be **data offset**, relative to the offset base |
| 0x14 | 4 | u32be **table name**: offset of a string inside the strings segment |
| 0x18 | 2 | u16be **column count** |
| 0x1A | 2 | u16be **row width** in bytes |
| 0x1C | 4 | u32be **row count** |

The schema segment starts at table offset 0x20, right after the header. [VERIFIED,
VGMSTREAM]

Notes per field:

* **Table size.** On all 15 real tables, value + 8 equals the CPK chunk's packet length
  exactly. [VERIFIED] The three synthetic test builders store the full table length
  here, not length − 8, so the value overstates the real size by 8. [TEST] Section 2.2
  says how to handle that.
* **Version and rows offset.** vgmstream reads 0x08 as a 16-bit version (0 on older
  files, 1 on files from about 2017 on) and 0x0A as a 16-bit rows offset. [VGMSTREAM]
  LibCPK and the previous NovelVM reader read 0x08..0x0B as one 32-bit rows offset.
  [LIBCPK] The two readings agree only while the version is 0. All 15 real tables have
  version 0. [VERIFIED] The new reader must use the 16-bit reading at 0x0A. It must
  not reject a table because of its version value. [DESIGN] The test fixtures write a
  32-bit value whose upper half is 0, which agrees with this. [TEST]
* **Offset base.** The rows, strings and data offsets are relative to table offset 8,
  not to the magic. Real check: the umd0.cpk header stores rows offset 0xBD; the schema
  of its 33 columns ends at table offset 0xC5 = 0xBD + 8, where the first row starts.
  [VERIFIED on all 15 tables: schema end == rows offset + 8]
* **Table name.** Real tables store the name at strings offset 7, after the string
  `"<NULL>"` at offset 0. Observed names: `CpkHeader`, `CpkTocInfo`, `CpkTocInfoEx`,
  `CpkEtocInfo`, `CpkExtendId`. [VERIFIED] vgmstream uses names such as
  `CRIUSF_DIR_STREAM`, `AUDIO_HDRINFO` and `CpkItocInfo`. [VGMSTREAM] The test
  fixtures write table name 0. [TEST] The public API does not expose the name. The
  reader must not validate it. [DESIGN]
* **Column count.** Zero is allowed. The USM test builds an `@SBT` table with zero
  columns. [TEST, DESIGN] vgmstream rejects zero columns. [VGMSTREAM]
* **Row count.** Zero is allowed: vgmstream notes empty tables in some games.
  [VGMSTREAM, DESIGN]

### 2.2 Extent and segment bounds [DESIGN unless noted]

Let `size` be the buffer length passed to `parse`.

1. Reject if `size < 0x20` or the first 4 bytes are not `"@UTF"`.
2. Compute `declared = tableSize + 8` in 64-bit arithmetic.
   `extent = declared` if `declared <= size`, else `extent = size`.
   This clamps to the real table on real data (where `declared` is exact) and keeps
   trailing bytes out. Example: a USM payload with padding, or an ACB file buffer. It
   also accepts the test fixtures, whose `declared` value is 8 bytes larger than the
   buffer. [VERIFIED for real data; TEST for the fixtures]
3. Compute absolute segment starts, in 64-bit arithmetic:
   * `R = rowsOffset + 8`
   * `S = stringsOffset + 8`
   * `D = dataOffset + 8`
4. Require `0x20 <= R <= S <= D <= extent`. Otherwise reject. All 15 real tables
   keep this order. [VERIFIED] vgmstream requires the same order. [VGMSTREAM]
5. The segments are:
   * schema `[0x20, R)`
   * rows `[R, S)`
   * strings `[S, D)`
   * data `[D, extent)`

   Any of them may be empty. In the USM test, `R == S` and `D == extent`. [TEST] On
   all real CPK tables, `D == extent`: the data segment is empty. [VERIFIED]

### 2.3 Rows segment bounds [DESIGN]

* Reject if `rowCount > 0x7FFFFFFF`. `numRows()` returns `int`.
* Reject if `rowCount * rowWidth > S - R`, using 64-bit arithmetic.
* If `rowWidth == 0`, also reject when `rowCount > extent`. This stops a tiny hostile
  table from claiming 2^31 rows that callers would then loop over. The USM test uses
  `rowWidth == 0` with one row. [TEST]
* On real data, `R + rowCount * rowWidth == S` exactly. [VERIFIED] Do not require
  this.

## 3. Strings segment

* A string value is a u32be offset relative to `S`. The string runs from `S + offset`
  to the first `0x00` byte. [VERIFIED, VGMSTREAM]
* Column names, the table name and string cell values all live here. [VERIFIED]
* Real tables begin the segment with `"<NULL>\0"`. Columns whose value is that string
  (for example the TOC's constant `UserString`) mean "no string" to CRI's tools.
  [VERIFIED for the bytes; UNCERTAIN for the meaning] Return the text `"<NULL>"`
  literally. Do not translate it. [DESIGN, matches the previous reader]
* The segment ends with zero padding on real tables. [VERIFIED] Do not depend on it.
* **Bounds**:
  * An offset `> D - S` is invalid. [DESIGN]
  * An offset equal to `D - S` gives the empty string. [DESIGN; vgmstream also
    accepts this offset]
  * If no `0x00` byte occurs before `D`, the string ends at `D`. Never read past
    `D`. [DESIGN]
* **Encoding**: every string on the real tables is 7-bit ASCII. [VERIFIED] No source
  documents an encoding flag. Japanese titles may use Shift-JIS. [UNCERTAIN] Copy the
  bytes unchanged into `Common::String` without conversion. [DESIGN]

## 4. Schema segment (column descriptors)

The schema holds `columnCount` variable-length entries, packed with no padding, in
column order. [VERIFIED, VGMSTREAM]

| Size | Meaning |
|---|---|
| 1 | descriptor byte: storage bits (high nibble) and type code (low nibble) |
| 4 | u32be offset of the column name in the strings segment |
| *w* | only for constant columns: the constant value, *w* = the type's width (section 5) |

### 4.1 Storage bits (high nibble)

| Bit | Meaning |
|---|---|
| 0x10 | the column has a name. Set on every real column. |
| 0x20 | the value is stored once, in the schema, right after the name offset ("constant") |
| 0x40 | the value is stored in every row ("per-row") |
| 0x80 | not defined |

[VGMSTREAM for the bit meanings; VERIFIED for the values below]

The combinations, and what the reader does with each [DESIGN unless noted]:

| High nibble | Meaning | Reader action |
|---|---|---|
| 0x1_ | named, no stored value | valid column; `get` returns `valid == false` |
| 0x3_ | named, constant | read *w* bytes from the schema |
| 0x5_ | named, per-row | reserve *w* bytes in each row |
| 0x7_ | named, both bits | treat as constant: the constant wins and **no** row space is reserved. vgmstream notes this combination on one game (Muramasa Rebirth, Vita). [VGMSTREAM; UNCERTAIN on real data] |
| 0x0_, 0x2_, 0x4_, 0x6_ (name bit clear) | not seen | reject the table |
| 0x8_ to 0xF_ (bit 0x80 set) | not defined | reject the table |

Descriptor bytes seen on the 15 real tables: `0x12 0x14 0x16 0x1A` (named, no value),
`0x3A` (constant string), and `0x52 0x54 0x55 0x56 0x5A` (per-row). [VERIFIED]

**Descriptor byte 0x00.** LibCPK treats a descriptor byte of 0 as a sign that the
descriptor is really 4 bytes wide: it skips 3 bytes and reads the next byte as the
descriptor. [LIBCPK] vgmstream rejects such a table. [VGMSTREAM] Not one of the 249
column descriptors in the 15 real tables is 0. [VERIFIED] The new reader must **not**
skip ahead. A 0x00 descriptor has the name bit clear, so the table is rejected under
the rules above. [DESIGN] Whether any real file has this layout is [UNCERTAIN].

Also note that LibCPK does not step over the constant value bytes in the schema.
vgmstream does, and so do the real tables: the schema ends exactly at `R` only if
the constant values are counted. [VERIFIED on umd1/P2 TOCs with constant `DirName`
and `UserString`; VGMSTREAM] The new reader must step over them.

### 4.2 Column name

* The name offset is relative to `S`. Reject the table if it is `> D - S`. [DESIGN,
  same limit as vgmstream] Read the name as in section 3.
* Names compare **exactly**, byte for byte and case-sensitively. [DESIGN]
* **Duplicate names exist on real data.** The P2 Eternal Punishment header has
  `EnableTocCrc` and `EnableFileCrc` twice each, all four per-row u16. [VERIFIED] `get`
  uses the **first** column whose name matches, even if that column has no stored
  value. [DESIGN]

### 4.3 Schema walk and bounds [DESIGN]

Keep a cursor `c = 0x20` and a per-row offset `rowOff = 0`. For each column:

1. Require `c + 5 <= R`. Read the descriptor byte and the name offset. `c += 5`.
2. Check the storage bits (4.1) and the type code (section 5). Reject if either is
   unknown.
3. If constant: require `c + w <= R`, record the value position `c`, then `c += w`.
4. Else if per-row: record `rowOff` as this column's offset inside a row, then
   `rowOff += w`.
5. Else (no value): record nothing.

After the loop, do **not** require `c == R`. It is true on all real tables. [VERIFIED]

Do **not** require `rowOff <= rowWidth`. vgmstream reports real ACB/HCA header tables
whose last per-row columns lie past the declared row width. It bounds them by the rows
segment instead. [VGMSTREAM; UNCERTAIN on real data] Every cell read is
bounds-checked instead (6.2). On all real tables, `rowOff == rowWidth`. [VERIFIED]

## 5. Type codes (low nibble)

| Code | Type | Width *w* | Seen on real data |
|---|---|---|---|
| 0x0 | u8 | 1 | (TEST, VGMSTREAM) |
| 0x1 | s8 | 1 | |
| 0x2 | u16 | 2 | yes |
| 0x3 | s16 | 2 | |
| 0x4 | u32 | 4 | yes |
| 0x5 | s32 | 4 | yes (P2 ITOC `ID`, `TocIndex`) |
| 0x6 | u64 | 8 | yes |
| 0x7 | s64 | 8 | |
| 0x8 | float32 (IEEE-754) | 4 | |
| 0x9 | float64 (IEEE-754) | 8 | |
| 0xA | string: u32be strings offset | 4 | yes |
| 0xB | vldata: u32be offset + u32be length | 8 | (TEST, VGMSTREAM) |
| 0xC | 128-bit value (vgmstream says "for GUIDs") | 16 | |
| 0xD-0xF | undefined | n/a | |

* Codes 0x0 to 0xB come from vgmstream's type list. LibCPK uses the same widths for
  0x0 to 0x8, 0xA and 0xB. [VGMSTREAM, LIBCPK]
* vgmstream lists 0x9 and 0xC but rejects both as "not seen". Its 0xC width constant
  is written as `0x16`, which looks like a typo for 16. [VGMSTREAM] NovelVM accepts
  0x9 with width 8 and 0xC with width 16, so that a table carrying such a column stays
  readable. [DESIGN; UNCERTAIN for 0xC width]
* Reject the table on codes 0xD to 0xF. [DESIGN]

## 6. Rows and cell lookup

### 6.1 Row layout

* Row `r` starts at `R + r * rowWidth`. [VERIFIED, VGMSTREAM]
* Per-row columns are packed in schema order with no alignment padding. A cell's
  offset inside the row is the sum of the widths of the per-row columns before it.
  [VERIFIED, VGMSTREAM]
* No-value and constant columns take no space in a row. [VERIFIED]

Real example, umd0.cpk TOC row 0, 28 bytes [VERIFIED]:

```
00 00 00 51 | 00 00 00 5d | 00 01 c0 b0 | 00 09 17 00 | 00 00 00 00 1f 92 c8 00 | 00 00 00 e0
DirName str | FileName str| FileSize u32| ExtractSize | FileOffset u64          | ID u32
```

### 6.2 `get(row, name)` [DESIGN]

1. Return an invalid value if `row < 0` or `row >= numRows()`.
2. Find the first column named `name` (4.2). Return an invalid value if there is none.
3. Its storage decides where the value bytes are:
   * no value: return invalid, but with `type` set to the column's type code.
   * constant: the bytes recorded in the schema (for every row alike).
   * per-row: `pos = R + row * rowWidth + rowOff`. If `pos + w > S` (64-bit),
     return invalid.
4. Decode the value per section 7.

`get` is `const` and never reads outside the copied table bytes.

## 7. Value semantics (`UTFValue`)

The public `UTFValue` struct and its helpers stay exactly as they are now:

```
struct UTFValue {
	bool valid = false;
	byte type = 0;
	uint64 u = 0;
	Common::String str;
	uint32 dataOffset = 0;
	uint32 dataLength = 0;
	uint64 asUint(uint64 def = 0) const { return valid ? u : def; }
	Common::String asString() const { return valid ? str : Common::String(); }
};
```

Fill it like this. Members not named stay at their defaults. [DESIGN]

| Type | `valid` | `u` | `str` | `dataOffset` / `dataLength` |
|---|---|---|---|---|
| u8/u16/u32/u64 | true | stored value, zero-extended | empty | 0 |
| s8/s16/s32/s64 | true | stored bits **zero-extended**, no sign extension | empty | 0 |
| float32 | true | raw IEEE-754 bits in the low 32 bits | empty | 0 |
| float64 | true | raw IEEE-754 bits | empty | 0 |
| string | true if the offset is in bounds (section 3), else false | 0 | the string bytes | 0 |
| vldata | true | 0 | empty | the two stored u32be values, unchanged |
| 0xC (128-bit) | false | 0 | empty | 0 |

* `type` is the raw type code (low nibble). It is set even when `valid` is false.
  No current caller reads it.
* **Integers of any width** are read by callers through `asUint()` and then cast to
  the width they want. The CPK fixture mixes u16, u32 and u64 for fields of one
  meaning. [TEST]
* **Why no sign extension**: every caller casts the result down to the column's width
  or narrower. ACB code compares `ExtensionData` (a u16 in vgmstream) with `0xFFFF`.
  [VGMSTREAM] P2's ITOC stores `ID` and `TocIndex` as s32, so zero-extension keeps
  them as plain 32-bit numbers. [VERIFIED] This matches the previous reader. [DESIGN]
* **Floats**: no caller reads a float yet. Callers that need one convert the bits
  themselves. [DESIGN]
* **vldata**: `dataOffset` is relative to the start of the **data segment**, that is
  to `dataSegment()`. [VGMSTREAM: it adds table base + data offset; TEST: the ACB and
  CPK fixtures place nested tables this way] `get` does **not** check
  `dataOffset + dataLength` against `dataSegmentSize()`. Callers do this check
  (`acb.cpp readNestedUTF`, the AwbFile path, `cpk.cpp collectItocRows`). [DESIGN,
  matches the callers] No real vldata cell was available. [UNCERTAIN on real data]
* **No-value columns** (high nibble 0x1): CRI's format means "value is zero/empty"
  here. vgmstream returns zero with success. [VGMSTREAM] NovelVM returns
  `valid == false`, so `asUint(def)` yields the caller's default. `cpk.md` §2 relies
  on this for `ItocOffset`/`GtocOffset`. [DESIGN, keeps the existing caller contract]

## 8. Public API and object state

```
class UTFTable {
public:
	bool parse(const byte *buf, uint32 size);
	int numRows() const;
	UTFValue get(int row, const char *name) const;
	const byte *dataSegment() const;
	uint32 dataSegmentSize() const;

	// New, section 10:
	static bool isMasked(const byte *buf, uint32 size);
	static void toggleMask(byte *buf, uint32 size);
};
```

Private members are the implementer's choice. Do not reuse the previous file's
storage enum names (`kStorageNone/Zero/Constant/PerRow`). Name the storage bits after
their meaning in 4.1. [DESIGN]

* `parse` copies `extent` bytes of the buffer into the object. It does not keep
  `buf`. Callers free their buffer right after `parse` (acb.cpp, usm.cpp). [DESIGN]
* `parse` first resets the object to the **empty state**. On any failure it leaves the
  object in that state and returns false. In the empty state:
  * `numRows()` returns 0.
  * `get` always returns an invalid value.
  * `dataSegment()` returns `nullptr`.
  * `dataSegmentSize()` returns 0.

  [DESIGN] A default-constructed object is in the empty state too. `parse` may be
  called again on the same object.
* After a successful `parse`:
  * `dataSegment()` = copy base + `D`.
  * `dataSegmentSize()` = `extent - D`. This may be 0.
* `parse` must never crash, assert or call `error()` on any input. [DESIGN]

## 9. Real-data facts and test vectors

All [VERIFIED] unless noted. Sources (black-box reads with Python `-I`):

* P3P: `~/Games/Visual Novel/PSP/Shin_Megami_Tensei_Persona_3_Portable_USA_PSP-PSPKiNG/PSP_GAME/USRDIR/`
  `umd0.cpk`, `umd1.cpk`, `MEMST.CPK`
* P2 EP: `~/Games/Visual Novel/PSP/Persona 2 - Eternal Punishment (English v1.00)[English Dub]/PSP_GAME/USRDIR/pack/P2PT_ALL.cpk`
* P2 IS: `~/Games/Visual Novel/PSP/Persona 2 Innocent Sin (USA) DLC enabler mod (v3)/PSP_GAME/USRDIR/pack/P2PT_ALL.cpk`

That gives 15 tables: 5 headers, 5 TOCs, 3 ETOCs and 2 ITOCs. All 15 are plain (not
masked) and have version 0. No local ACB, AWB or USM sample was found under
`~/Games/Visual Novel/PSP` or `PS2`.

### 9.1 umd0.cpk header table (packet at file 0x10, 0x290 bytes)

First 0x40 bytes:

```
40 55 54 46 00 00 02 88 00 00 00 bd 00 00 01 21
00 00 02 88 00 00 00 07 00 21 00 64 00 00 00 01
56 00 00 00 11 16 00 00 00 20 56 00 00 00 29 56
00 00 00 37 56 00 00 00 43 56 00 00 00 4d 14 00
```

The header fields decode to:

* table size 0x288, so the extent is 0x290
* version 0
* rows offset 0xBD, so `R = 0xC5`
* strings offset 0x121, so `S = 0x129`
* data offset 0x288, so `D = 0x290` (empty data segment)
* table name offset 7, which reads `"CpkHeader"`
* 33 columns
* row width 100 (0x64)
* 1 row

The strings segment starts `"<NULL>\0CpkHeader\0UpdateDateTime\0..."`.

Columns, in order (descriptor, row-0 value):

| Column | Desc | Row 0 |
|---|---|---|
| UpdateDateTime | 56 | 1 |
| FileSize | 16 | invalid |
| ContentOffset | 56 | 0x3A000 |
| ContentSize | 56 | 553402368 |
| TocOffset | 56 | 0x800 |
| TocSize | 56 | 234448 |
| TocCrc | 14 | invalid |
| EtocOffset | 56 | 0x20FFE000 |
| EtocSize | 56 | 72200 |
| ItocOffset, ItocSize | 16 | invalid |
| ItocCrc | 14 | invalid |
| GtocOffset, GtocSize | 16 | invalid |
| GtocCrc | 14 | invalid |
| EnabledPackedSize | 56 | 1728146218 |
| EnabledDataSize | 56 | 1411548403 |
| TotalDataSize | 16 | invalid |
| Tocs | 14 | invalid |
| Files | 54 | 5860 |
| Groups | 54 | 0 |
| Attrs | 54 | 0 |
| TotalFiles, Directories, Updates | 14 | invalid |
| Version | 52 | 5 |
| Revision | 52 | 0 |
| Align | 52 | 0x800 |
| Sorted | 52 | 1 |
| EID | 12 | invalid |
| CpkMode | 54 | 1 |
| Tvers | 5A | `"CPKMC2.08.11, DLL2.51.11"` |
| Comment | 1A | invalid |

### 9.2 umd0.cpk TOC table (packet at file 0x810, 0x393C0 bytes)

First 0x20 bytes:

```
40 55 54 46 00 03 93 b8 00 00 00 3f 00 02 81 2f
00 03 93 b8 00 00 00 07 00 07 00 1c 00 00 16 e4
```

The header fields decode to:

* `R = 0x47`
* `S = 0x28137`
* `D = 0x393C0 = extent`
* 7 columns
* row width 28
* 5860 rows
* table name `"CpkTocInfo"`

Columns:

* `DirName` 5A
* `FileName` 5A
* `FileSize` 54
* `ExtractSize` 54
* `FileOffset` 56
* `ID` 54
* `UserString` 3A, with constant bytes `00 00 00 00`, which reads `"<NULL>"`

Rows:

| Row | DirName | FileName | FileSize | ExtractSize | FileOffset | ID |
|---|---|---|---|---|---|---|
| 0 | `data/battle` | `AICALC.TBL` | 114864 | 595712 | 0x1F92C800 | 224 |
| 1 | `data/battle` | `battle_pack.bin` | 281572 | 911616 | 0x1F9A0000 | 225 |
| 5859 | `data/tool/effect/sample` | `X_NOMAL0.EP` | 3292 | 5392 | 0x18B9C800 | 7487 |

Row 0 raw bytes are in 6.1.

### 9.3 Other facts

* **umd1.cpk TOC** (7 columns, row width 24, 338 rows): `DirName` is constant `3A`
  with offset 0x1A, which reads `"data/sound/voice"`. Row 0 is `v100001.afs`,
  FileSize 442368.
* **P2 EP header**: 32 columns. `EnableTocCrc` and `EnableFileCrc` each appear
  twice. Both copies are `52` (per-row u16), with value 0.
* **P2 EP ITOC** `CpkExtendId` (2 columns, row width 8, 167 rows): `ID` 55 and
  `TocIndex` 55 (s32). Rows 0 to 3 give `(ID, TocIndex)` = `(0,19) (1,18) (2,26)
  (3,27)`.
* **P2 EP TOC** `CpkTocInfoEx`: `DirName` is constant `3A`, offset 0x1C, which
  reads the empty string `""`. The table also has a per-row u32 `CRC` column.
* Descriptor counts over all 15 tables:

  | Descriptor | Count |
  |---|---|
  | 56 | 53 |
  | 54 | 41 |
  | 14 | 30 |
  | 52 | 28 |
  | 16 | 22 |
  | 5A | 15 |
  | 3A | 8 |
  | 1A | 5 |
  | 55 | 4 |
  | 12 | 3 |

  There is no `00` descriptor and none with bit 0x80 set.

### 9.4 Synthetic test vectors [DESIGN; computed by this document's script]

Plain 0x48-byte table:

* 1 row, table name `"T"`
* column `A`: u16 per-row (`52`), value 0x1234
* column `B`: u8 constant (`30`), value 7
* column `S`: string per-row (`5A`), value `"hi"`

```
40 55 54 46 00 00 00 40 00 00 00 28 00 00 00 2e
00 00 00 40 00 00 00 07 00 03 00 06 00 00 00 01
52 00 00 00 09 30 00 00 00 0b 07 5a 00 00 00 0d
12 34 00 00 00 0f 3c 4e 55 4c 4c 3e 00 54 00 41
00 42 00 53 00 68 69 00
```

Expected results:

* `numRows() == 1`
* `get(0,"A").u == 0x1234`
* `get(0,"B").u == 7`
* `get(0,"S").str == "hi"`
* `get(0,"T")` is invalid: `T` is the table name, not a column.
* `get(1,"A")` is invalid.
* `dataSegmentSize() == 0`

The same table masked (section 10):

```
1f 9e f3 f5 af 5b 77 83 ff eb 47 fb 4f 7b 17 cd
9f 0b e7 b3 ef 9b b7 04 3f 28 87 15 8f bb 57 22
8d 4b 27 33 26 eb f7 43 7f 60 c0 09 cf fb 97 6e
0d bf 67 73 6f 14 0b cd ea e7 4b ad 0f 6f d7 e2
5f 89 a7 e0 af 33 1e c3
```

Hostile vectors that must make `parse` return false without crashing [DESIGN]:

* the plain table truncated to any length below 0x20
* bytes 0x0A..0x0B changed to `00 30`, so that `R = 0x38 > S = 0x36`
* the descriptor at 0x20 changed to `0x00`, `0x42` or `0x9A`
* the descriptor at 0x20 changed to `0x5D`
* bytes 0x1C..0x1F changed to `7F FF FF FF`, which makes rows exceed the segment
* the name offset at 0x21 changed to `00 00 00 FF`

## 10. Masked tables

### 10.1 The mask [LIBCPK; UNCERTAIN on real data]

Some CPK files store their table packets XOR-masked. LibCPK unmasks any CPK chunk
packet that does not start with `"@UTF"`. It does this for the header, TOC, ETOC, ITOC
and GTOC chunks. [LIBCPK] vgmstream has no masking support. [VGMSTREAM] No masked
table was available: all 15 real packets are plain. [VERIFIED]

The mask is a byte-wise XOR with a key stream:

```
k = 0x5F                      (low byte of LibCPK's seed 0x655F)
for each byte i of the packet, starting at the "@UTF" position (i = 0):
    packet[i] ^= k
    k = (k * 0x15) mod 256      (0x15 = low byte of LibCPK's multiplier 0x4115)
```

* LibCPK keeps a 32-bit running product of the seed 0x655F and the multiplier
  0x4115. It XORs each byte with the low 8 bits of that product. Only the low 8 bits
  of a product depend only on the low 8 bits of its factors. So the 8-bit recurrence
  above produces the same key stream. This was checked by script for the first 256
  bytes. The stream repeats every 64 bytes.
* First 32 key bytes:

  ```
  5f cb a7 b3 af 5b 77 c3 ff eb 47 d3 4f 7b 17 e3
  9f 0b e7 f3 ef 9b b7 03 3f 2b 87 13 8f bb 57 23
  ```
* XOR is its own inverse. The same operation masks and unmasks.
* The mask covers the whole packet, from the magic to the chunk header's packet
  length, including the magic itself. [LIBCPK]
* A masked table therefore starts with `1F 9E F3 F5` ("@UTF" XOR `5F CB A7 B3`).

### 10.2 Detection [DESIGN]

LibCPK's own test is meant to be "first 4 bytes are not @UTF". As written, it fires
only when all four bytes differ. [LIBCPK] NovelVM detects masking positively, so a
random non-table packet is not "unmasked" into garbage:

* plain: bytes 0..3 are `40 55 54 46`
* masked: bytes 0..3 are `1F 9E F3 F5`
* anything else: not a table

### 10.3 API addition

Add two static members to `UTFTable`:

* `static bool isMasked(const byte *buf, uint32 size)`: true if and only if
  `size >= 4` and bytes 0..3 are `1F 9E F3 F5`.
* `static void toggleMask(byte *buf, uint32 size)`: XORs `buf[0..size)` in place
  with the key stream of 10.1, starting at key index 0. It is its own inverse.

`parse` itself accepts only plain tables. It does not unmask. [DESIGN]

### 10.4 Where the CPK reader uses it

`cpk.md` §1.3 currently says a packet that does not start with `"@UTF"` makes the
table unreadable. Replace that rule with this one:

1. If the packet starts with `"@UTF"`, parse it.
2. Else if `UTFTable::isMasked(packet, length)`, call
   `UTFTable::toggleMask(packet, length)` on the whole packet buffer, then parse it.
3. Else the table is unreadable (warning, same outcomes as today).

In the code, this goes in `cpk.cpp`'s `readChunkTable()`, right after the packet has
been read and before the existing `hasUTFMagic` check. That function serves the
header, TOC and ITOC chunks alike.

The nested ITOC `DataL`/`DataH` tables are not unmasked separately. Once the
enclosing ITOC packet has been unmasked, they are plain bytes. [LIBCPK: it unmasks
only chunk packets.] Whether ACB, AWB or USM tables are ever masked is
[UNCERTAIN]. Their readers do not call the helpers.

The `cpk.md` §10 open item on masking becomes "scheme from LibCPK, unverified on real
data".

## 11. Hostile input checklist [DESIGN]

`parse` returns false, and leaves the empty state, when any of these fails:

* The buffer is at least 0x20 bytes long and starts with `"@UTF"`.
* `0x20 <= R <= S <= D <= extent`, computed in 64-bit arithmetic.
* `rowCount <= 0x7FFFFFFF`.
* `rowCount * rowWidth <= S - R`.
* If `rowWidth == 0`: `rowCount <= extent`.
* For every column:
  * The 5-byte descriptor fits in the schema.
  * The name bit is set.
  * Bit 0x80 is clear.
  * The type code is 0x0 to 0xC.
  * The name offset is `<= D - S`.
  * A constant value fits in the schema.

`get` returns an invalid value, and never reads out of bounds, when any of these
holds:

* the row is out of range
* the name is unknown
* the column has no value
* a per-row cell would end past `S`
* a string offset is `> D - S`

Strings never read past `D`. Every addition that combines two untrusted 32-bit
values is done in 64 bits.

## 12. Differences from the previous reader (for the comparator)

| Topic | Previous reader | This spec |
|---|---|---|
| Field at 0x08..0x0B | read as one u32 rows offset | u16 version + u16 rows offset |
| Descriptor byte 0x00 | skips 3 bytes and re-reads | rejects the table |
| Storage | exact values 0x30/0x50 only | bit test, so 0x70 counts as constant |
| Unknown storage | read as "no value" | rejects the table |
| Per-row widths | required to fit in the row width | only each cell is bounds-checked, against the rows segment |
| Extent | the whole buffer | clamped to the declared size |
| Section order | not checked | checked |
| Duplicate names | first column that has a stored value | first column by name |
| Failure state | could leave partial state | resets to the empty state |
| Masking | none | static helpers added |
