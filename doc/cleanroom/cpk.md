# CRI CPK archives: clean-room specification

This document specifies the CRI Middleware "CPK" archive container and its CRILAYLA
compression in enough detail to write `Common::CPK` (`common/formats/cpk.{h,cpp}`)
from it alone. It is written for NovelVM's needs: read-only access to the named file
table (TOC), the numeric-ID file table (ITOC), and CRILAYLA decompression of members.

## Provenance and confidence tags

Every claim below carries one of these tags:

* **[VERIFIED]**: checked by black-box byte inspection of the two retail archives listed
  in "Reference data" with throwaway Python readers written for this document.
* **[VGMSTREAM]**: taken from vgmstream (ISC license), `src/meta/cpk.c` and
  `src/util/cri_utf.c`. vgmstream only reads ITOC-style CPK audio banks.
* **[TEST]**: required by the existing NovelVM test `test/common/formats/cpk.h`.
* **[DESIGN]**: a NovelVM decision, not a fact about the format. The implementer must
  follow it so that behaviour is predictable. The comparator should not treat it as a
  format claim.
* **[UNCERTAIN]**: not confirmed by any allowed source or by real data.

The vg-resource wiki page (https://wiki.vg-resource.com/CPK) was a permitted source, but
the server refused access (HTTP 403) while this spec was written. No **[WIKI]** claims
appear. The CRILAYLA section was reconstructed and then confirmed by decoding 3,581
real compressed members with zero failures and exact output sizes, with recognizable
decoded content (see the CRILAYLA section).

All byte offsets are hexadecimal. "u32le" means a little-endian 32-bit unsigned
integer. "u64le" means a little-endian 64-bit unsigned integer.

## 1. Overall layout

A CPK file is a sequence of **chunks**. Each chunk is a 16-byte chunk header followed
by one CRI `@UTF` table. Member file data lives elsewhere in the same file, at
offsets that the tables give. [VERIFIED]

### 1.1 Chunk header (16 bytes)

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | 4 | ASCII tag: `"CPK "` (header), `"TOC "`, `"ITOC"`, `"ETOC"`, `"GTOC"` |
| 0x04 | 4 | u32le, observed value `0x000000FF` in every chunk |
| 0x08 | 8 | u64le: byte length of the `@UTF` table that follows (the "packet") |

* The `@UTF` table begins at chunk offset + 0x10. [VERIFIED, VGMSTREAM: vgmstream
  opens the header table at 0x10 and the ITOC table at `ItocOffset + 0x10`.]
* The packet length at +0x08 equals the `@UTF` table's own size field + 8. Real
  values: header `0x290`, umd0 TOC `0x393C0`, umd0 ETOC `0x119F8`. [VERIFIED]
* Bytes at +0x0C..0x0F were zero in every observed chunk. Treat +0x08 as a u64le
  value. Reject a packet length that is 0 or exceeds the bytes left in the file after
  the chunk header. [VERIFIED for zero upper half; DESIGN for rejection]
* The test fixture writes +0x04 as u32le 0xFF and +0x08 as u32le length with +0x0C
  zero, which matches this reading. [TEST]
* Do **not** check the value at +0x04. [DESIGN]
* Check the tag only for the `"CPK "` chunk at file offset 0. For TOC and ITOC chunks,
  the implementation may log a warning on a tag mismatch, but must not reject on that
  alone. [DESIGN] The tags `"TOC "` and `"ETOC"` were observed on real data
  [VERIFIED]. The test fixture writes `"ITOC"` [TEST].

### 1.2 `@UTF` tables

Use the existing `Common::UTFTable` (`common/formats/utf_table.h`). Pass it the packet
bytes, which start at the `"@UTF"` magic, and the packet length. `get(row, name)`
returns a `UTFValue`. `valid == false` means the column is absent or has no stored
value. Read integer columns with `asUint()` whatever their declared width. This lets
16-bit and 32-bit size columns behave the same way. [TEST: the fixture uses u16 ID,
u32 sizes, u64 offsets, u16 Align, u32 Files]

Real tables use a mix of per-row columns and constant (schema-stored) columns. For
example, umd1's TOC stores `DirName` once, as the constant `"data/sound/voice"`.
`UTFTable` already resolves this. The CPK layer only calls `get()`. [VERIFIED]

Table names observed: `CpkHeader`, `CpkTocInfo`, `CpkEtocInfo`. vgmstream also
requires `CpkItocInfo`, `CpkItocL` and `CpkItocH` for ITOC. [VERIFIED, VGMSTREAM]
The table name is not exposed by `UTFTable`. The implementation does **not** check
table names. [DESIGN]

### 1.3 Masked or encrypted `@UTF` tables

Unmasking CPK packets is the caller's job: `UTFTable::parse` accepts only plain tables.
Both reference archives store plain tables: every packet starts with the bytes `40 55 54 46`
(`"@UTF"`). [VERIFIED]

Required behaviour, per `utf-table.md` §10.4:

1. If the packet starts with `"@UTF"`, parse it.
2. Else if `UTFTable::isMasked(packet, length)`, call `UTFTable::toggleMask(packet,
   length)` on the whole packet buffer, then parse it.
3. Else treat that table as unreadable [DESIGN]:
   * If the header packet is unreadable, the archive opens with zero members.
   * If the TOC or ITOC packet is unreadable, that table contributes no members.

   Log a warning in each case.

The mask scheme is described in `utf-table.md` §10. It comes from LibCPK and is
unverified on real data. [UNCERTAIN]

## 2. Header chunk (`"CPK "`, at file offset 0)

The archive is valid only if all of these hold. Otherwise it has zero members and
every lookup fails. No crash and no `error()` are allowed. [DESIGN]

* The file is at least 0x10 bytes long.
* Bytes 0..3 are `"CPK "`.
* The header packet parses as a `@UTF` table with at least one row.

Read row 0 of the header table. Columns used:

| Column | Real type | Use |
|---|---|---|
| `TocOffset` | u64 | Absolute file offset of the `"TOC "` chunk. 0 or absent = no TOC. |
| `ItocOffset` | u64 | Absolute file offset of the `"ITOC"` chunk. 0 or absent = no ITOC. |
| `ContentOffset` | u64 | Absolute offset of the first ITOC member (ITOC packing base). |
| `Align` | u16 | ITOC packing alignment. 0 or absent = no alignment. |
| `Files` | u32 | Declared member count. Informational only. |
| `EtocOffset`, `GtocOffset` | u64 | Ignored (see section 5). |

* All offsets are stored as native 64-bit columns. No high/low word combination is
  needed. Read them with `asUint()` into `uint64`. [VERIFIED for TocOffset,
  ContentOffset, EtocOffset; TEST and VGMSTREAM for ItocOffset]
* Column flags on real data: `ItocOffset` and `GtocOffset` were present in the schema
  but had no stored value in both P3P archives. `UTFTable` reports them as
  `valid == false`. [VERIFIED]
* `Files` equals the TOC row count in both P3P archives: 0x16E4 = 5860 and
  0x152 = 338. Do not depend on it. Use the row counts. [VERIFIED]
* Other header columns, such as `ContentSize`, `TocSize`, `EtocSize`, `Version`,
  `Revision`, `Sorted`, `CpkMode`, `Tvers` and `EnabledPackedSize`, are not needed.
  [VERIFIED present; DESIGN unused]

Reading order [DESIGN]:

1. If `TocOffset` is valid and nonzero, read the TOC (section 3).
2. If `ItocOffset` is valid and nonzero, read the ITOC (section 4).

Both may be present. Each produces its own entries, and both sets are added to the
archive. The behaviour with both present was not seen on real data. [UNCERTAIN]

## 3. TOC (named members)

### 3.1 Location

The chunk sits at absolute offset `TocOffset`, and its `@UTF` packet at
`TocOffset + 0x10`. On P3P, `TocOffset = 0x800`. The 16 bytes before it end with the
ASCII text `(c)CRI`, which is padding and not parsed. [VERIFIED]

### 3.2 Columns (one row per member)

| Column | Real type | Use |
|---|---|---|
| `DirName` | string | Directory part of the path. May be a constant column. |
| `FileName` | string | File name part. |
| `FileSize` | u32 | Bytes stored in the archive. |
| `ExtractSize` | u32 | Size after decompression. |
| `FileOffset` | u64 | Offset of the stored bytes, **relative to `TocOffset`**. |
| `ID` | u32 | Numeric ID. Not unique-order. Ignored by NovelVM (see below). |
| `UserString` | string | Observed constant `"<NULL>"`. Ignored. |

[VERIFIED: column set, types, and `UserString` constant on both archives]

### 3.3 Absolute data offset

`absolute offset = TocOffset + FileOffset`. **[VERIFIED]**

This was checked on all 5860 umd0 rows and all 338 umd1 rows:

* With base `TocOffset`, every member lies inside the file.
* Every compressed member then starts with `CRILAYLA`, and every umd1 member starts
  with `AFS\0`.
* Every resulting offset is a multiple of `Align` (0x800).

With base `ContentOffset`, 46 umd0 members and 1 umd1 member fall outside the file,
and the magic numbers do not match. On real data the smallest `FileOffset` is
`0x39800` in umd0 and `0x3800` in umd1. Adding `TocOffset` (0x800) gives exactly
`ContentOffset`: 0x3A000 and 0x4000.

### 3.4 Member path

[DESIGN, consistent with VERIFIED data]

* Let `dir = DirName.asString()` and `file = FileName.asString()`.
* If `dir` is empty, the path is `file`. Otherwise the path is `dir + "/" + file`.
* Real `DirName` values use `/` separators. They have no leading or trailing slash and
  are never empty. Example: `data/battle/boss/BED`. [VERIFIED]
* Defensive normalization [DESIGN]:
  * Convert any `\` to `/`.
  * Strip leading `/` characters from the joined path.
  * Skip a row whose resulting path is empty.
* Build the `Common::Path` with `/` as the separator.
* Lookups are case-insensitive. Normalize the key with `Common::Path`
  case-insensitive comparison or a lower-cased key. Real archives contain no paths that
  differ only by case. [VERIFIED for P3P; DESIGN for the rule]
* If two rows map to the same case-insensitive path, the first row wins and later rows
  are ignored. [DESIGN] Real archives do this: both Persona 2 PSP `P2PT_ALL.cpk` files
  have two different root-level `card.bin` rows (EP: row 9, ID 9, 3,147,776 bytes; row
  10, ID 2002, 76,688 bytes), so the second is reachable only by its ID. [VERIFIED]
* Keep the members in TOC row order for `listMembers()`. The rows are **not** sorted by
  offset, and umd0 is not sorted by (dir, name) either. [VERIFIED]

### 3.5 Entry fields

`name` = path from 3.4. `offset` = absolute offset from 3.3. `size = FileSize`.
`extractSize = ExtractSize`. `id = 0`. `hasId = false`.

TOC entries are **not** reachable through `getEntryById`, even though the TOC has an
`ID` column. The ID column is not unique-order on real data (umd0's first row has
ID 224). [DESIGN, per the public API comment "0 for TOC-named entries"]

## 4. ITOC (numeric-ID members)

No real `DataL`/`DataH` ITOC archive was available. This section follows vgmstream and
the NovelVM test. [VGMSTREAM, TEST; UNCERTAIN on real data]

The Persona 2 PSP archives have a TOC *and* an ITOC of a different kind: a
`CpkExtendId` table with signed 32-bit `ID` and `TocIndex` columns, mapping numeric IDs
to TOC rows (EP: `(0,19) (1,18) (2,26) (3,27)` ...). It has no `DataL`/`DataH`, so the
rules below yield no ITOC members for it and its IDs are not reachable through
`getEntryById`. [VERIFIED; supporting it is an open item]

### 4.1 Location and info table

The chunk sits at absolute offset `ItocOffset`, and its `@UTF` packet at
`ItocOffset + 0x10`. Row 0 of this "info" table has these columns:

| Column | Type | Use |
|---|---|---|
| `DataL` | vldata | An embedded `@UTF` table of "small" members. |
| `DataH` | vldata | An embedded `@UTF` table of "large" members. |
| `FilesL`, `FilesH` | u32 | Row counts of the two sub-tables. Informational only. |

* vgmstream requires `FilesL + FilesH == Files` and each sub-table's row count to
  equal its `Files*` value. NovelVM does not enforce this. The test fixture omits
  `FilesL` and `FilesH`. [VGMSTREAM; TEST; DESIGN]
* For a vldata value `v`, the embedded table's bytes are
  `dataSegment() + v.dataOffset`, `v.dataLength` bytes long. Check the bounds first:
  `v.dataOffset + v.dataLength <= dataSegmentSize()`, using 64-bit arithmetic. Parse
  the bytes with a second `UTFTable`. [TEST]
* If a column is missing, invalid, or has zero length, that sub-table is empty.

### 4.2 Sub-table columns (`DataL` and `DataH` alike)

| Column | Type in vgmstream | Use |
|---|---|---|
| `ID` | u16 | Member ID. |
| `FileSize` | u16 in DataL, u32 in DataH | Stored bytes. |
| `ExtractSize` | u16 in DataL, u32 in DataH | Size after decompression. |

Read all three with `asUint()`. The width difference then needs no special code. The
test uses u32 sizes in both sub-tables. [VGMSTREAM; TEST]

Members of the two sub-tables are interleaved by ID. One sub-table does not come
before the other. [VGMSTREAM: "files can be mixed (small small big small big)"]

### 4.3 Offsets: sequential packing by ascending ID

1. Collect `(ID, FileSize, ExtractSize)` from every row of DataL and DataH.
2. Sort the collected rows by ascending `ID`. [VGMSTREAM; TEST: DataL lists IDs 2
   then 0, DataH lists ID 1, and the expected order is 0, 1, 2]
3. Set `offset = ContentOffset`, where an absent `ContentOffset` means 0. For each
   member in sorted order:
   * Set `entry.offset = offset`.
   * Advance: `offset += FileSize`.
   * If `Align > 0` and `offset % Align != 0`, round up:
     `offset += Align - (offset % Align)`.

   [VGMSTREAM; TEST: 0x400, then 0x410, then 0x430 with Align 0x10]
4. Duplicate IDs: keep the first occurrence and drop the rest. Log a warning.
   vgmstream rejects the whole table instead. Each dropped row still takes no space in
   the packing. [DESIGN; UNCERTAIN what real archives do]
5. IDs need not start at 0. vgmstream saw archives whose IDs start at a nonzero base.
   NovelVM stores the IDs as read. [VGMSTREAM; DESIGN]

vgmstream requires `FileSize == ExtractSize` for ITOC audio members. NovelVM allows
them to differ. A difference marks a compressed member (section 6). [DESIGN]

### 4.4 Entry fields and naming

* `id = ID`.
* `hasId = true`.
* `size = FileSize`.
* `extractSize = ExtractSize`.
* `name` = the decimal ID with no padding or extension, for example `"0"` or `"17"`.
* These names are registered in the same case-insensitive path map as TOC names, so
  `hasFile("0")` and `createReadStreamForMember("0")` work. [TEST]
* If a TOC member already uses that name, the TOC member keeps the name. The ITOC
  entry is still reachable through `getEntryById`. [DESIGN]

## 5. ETOC and GTOC

* ETOC (`"ETOC"` chunk, `CpkEtocInfo`) has columns `UpdateDateTime` and `LocalDir`
  only. It has one row per member plus one extra row: umd0 has 5861 rows for 5860
  files. It holds no offsets or sizes. **Ignore it.** [VERIFIED]
* GTOC (groups) is absent in both reference archives: `GtocOffset` has no value, and
  `Groups = 0`. **Ignore it.** [VERIFIED absent; DESIGN ignore]

## 6. Deciding whether a member is compressed

A member is CRILAYLA-compressed if and only if `ExtractSize != FileSize`. In that case
the stored bytes must start with the 8 ASCII bytes `CRILAYLA`. [DESIGN rule; VERIFIED
consistent]

These checks hold on all 5860 umd0 and 338 umd1 rows:

* 5018 umd0 rows have `FileSize != ExtractSize`, and all 5018 start with `CRILAYLA`.
* No row with `FileSize == ExtractSize` starts with `CRILAYLA`.
* No row has `FileSize > ExtractSize`.
* umd1 has no compressed members.

If sizes differ but the magic is absent, the member cannot be read and
`createReadStreamForMember` returns `nullptr` with a warning. [DESIGN] If sizes are
equal, return the raw bytes. Do not sniff the magic in this case. [DESIGN]

## 7. CRILAYLA decompression

### 7.1 Stored layout

Offsets are relative to the start of the member's stored bytes.

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | 8 | ASCII `CRILAYLA` |
| 0x08 | 4 | u32le `U`: size of the **compressed region's** decompressed output (excludes the 0x100 prefix) |
| 0x0C | 4 | u32le `C`: byte length of the compressed bit stream |
| 0x10 | `C` | compressed bit stream |
| 0x10 + `C` | 0x100 | raw, uncompressed **prefix**: the first 0x100 bytes of the output |

Invariants confirmed on every one of the 5018 compressed members in umd0 [VERIFIED]:

* `ExtractSize == 0x100 + U`
* `FileSize == 0x10 + C + 0x100`, with no trailing bytes.

### 7.2 Output

* Total output length is `0x100 + U`.
* Copy the 0x100-byte prefix (stored after the bit stream) to output bytes
  `[0, 0x100)`.
* The bit stream then produces output bytes `[0x100, 0x100 + U)`, filling them
  **from the end backwards**. The first decoded byte goes to index `0x100 + U - 1`,
  and decoding stops when the byte at index 0x100 has been written. [VERIFIED]

### 7.3 Bit reader

* Read the stream **backwards by byte**. The first byte consumed is at stored offset
  `0x10 + C - 1`, then `0x10 + C - 2`, and so on toward 0x10.
* Within each byte, consume bits **most-significant first**.
* A multi-bit field of width n is assembled MSB-first: `value = (value << 1) | bit`,
  n times. Fields may span byte boundaries.

[VERIFIED]

The reader must never read below stored offset 0x10. If it would need a byte there,
the input is malformed (section 7.6). On real data, decoding always finished with 1 to
4 low bytes of the bit stream (offsets 0x10 to 0x15) unread and some bits left in the
current byte. That is normal padding. **Do not** require the bit stream to be fully
consumed. [VERIFIED]

### 7.4 Token loop

Write `pos` for the index of the next output byte to write. It starts at
`0x100 + U - 1` and decreases. While `pos >= 0x100`:

1. Read 1 bit, the **flag**.
2. **Flag 0: literal.** Read 8 bits as one byte. Write it at `pos`, then decrement
   `pos`.
3. **Flag 1: back-reference.**
   * Read 13 bits as `d`. The distance is `distance = d + 3`, so distances run from 3
     to 8194.
   * Read the length. It starts at 3 and grows by a chain of fields with widths 2, 3,
     5, then 8 repeated:
     ```
     length = 3
     for width in (2, 3, 5):
         v = read(width); length += v
         if v != all-ones(width): goto done      # all-ones = (1<<width)-1: 3, 7, 31
     repeat:
         v = read(8); length += v
     until v != 0xFF
     done:
     ```
     The smallest length is 3, from a first field of 0. The 8-bit fields repeat until
     one of them is not 0xFF.
   * Copy `length` bytes, one at a time, moving downward:
     ```
     src = pos + distance
     repeat length times:
         out[pos] = out[src]; pos -= 1; src -= 1
     ```
     The source always lies above the destination and has already been written. A
     byte-by-byte copy is required because a copy may overlap its own output: if
     `distance < length`, the copy repeats a pattern. Never use `memcpy`/`memmove`
     over the whole run.

[VERIFIED: this exact loop decoded all 3581 compressed umd0 members smaller than
40,000 stored bytes. There were no errors, the output always ended exactly at index
0x100, and the content was recognizable: `FLW0` script headers, `TMX0` textures, file
name tables in `.pac` files.]

The source index can reach the 0x100-byte prefix region from above, because the
prefix is already filled. That is legal. `src` can never fall below the destination,
because `distance >= 3`.

### 7.5 Return value

`createReadStreamForMember` returns a `Common::MemoryReadStream` that owns the output
buffer (`DisposeAfterUse::YES`). Its size is exactly `extractSize`. [TEST for
uncompressed; DESIGN for compressed]

### 7.6 Validation for hostile input

The decompressor must reject, by returning failure, without crashing or reading out of
bounds, in each of these cases:

* The stored size is less than 0x10, or the magic is not `CRILAYLA`.
* `0x10 + C + 0x100 > FileSize`. Use 64-bit arithmetic. Extra trailing bytes are
  tolerated.
* `0x100 + U != ExtractSize`. [DESIGN: on real data they are always equal. Rejecting a
  mismatch also bounds the allocation by the TOC size.]
* `ExtractSize` exceeds a sanity cap. [DESIGN: suggest 512 MiB. The largest real
  compressed ExtractSize in umd0 is 3,343,308 bytes. The largest member of any kind
  is 32,143,360 bytes, and it is uncompressed.] [VERIFIED for the sizes]
* The bit reader would need a byte below stored offset 0x10.
* A back-reference whose `length` would move `pos` below 0x100. That means
  `length > pos - 0x100 + 1`.
* A back-reference whose source byte index `pos + distance` is `>= 0x100 + U`. Check
  the start of the copy. Because `src` and `pos` decrease together, one check per
  token is enough.
* A length chain that would overflow. Keep `length` in 32 bits and stop as soon as it
  exceeds the remaining output (the rule above). Do not keep reading 0xFF fields after
  the output is full.

On failure, `createReadStreamForMember` returns `nullptr` and logs a warning naming
the member. [DESIGN]

## 8. Reading members and the archive API

### 8.1 Opening

`CPK(filename)` opens the file with `Common::File` through `SearchMan`. The test adds
`/tmp` as a SearchMan directory and opens by bare file name. [TEST] Keep the
`Common::File` open, or reopen it per read; either works. If the open fails, the
archive has zero members. `makeCPKArchive(name)` returns `new CPK(name)`. Returning
`nullptr` when the archive is empty or invalid is acceptable, if `smt.cpp` callers
cope. They construct `Common::CPK` directly, so this choice does not affect them.
[DESIGN]

Read every table into memory with bounds checks against the file size before parsing:

* The header chunk is at 0.
* The TOC chunk is at `TocOffset`.
* The ITOC chunk is at `ItocOffset`.

### 8.2 Bounds for every entry

Drop an entry, with a warning, if any of these holds:

* `offset + size` overflows 64 bits, or exceeds the archive file size.
* The path is empty (TOC entries only).
* For ITOC entries, the computed packing offset is checked the same way.

Members of size 0 are allowed and produce an empty stream. [DESIGN]

### 8.3 API behaviour

* `hasFile(path)`: true if the case-insensitive path is in the name map.
* `listMembers(list)`:
  * Appends one `GenericArchiveMember(path, *this)` per named entry, in insertion
    order: TOC rows first, then ITOC entries by ascending ID.
  * Returns the number appended.
* `getMember(path)`: returns a `GenericArchiveMember` for the path, or an empty
  pointer if not found.
* `createReadStreamForMember(path)`:
  * Returns `nullptr` if the path is not found.
  * Otherwise reads `size` bytes at `offset`. A short read returns `nullptr`.
  * If `extractSize != size`, decompresses as in section 7.
  * Returns a stream of exactly `extractSize` bytes.
* `getEntryById(id, entry)`:
  * Looks up ITOC entries only, through an id-to-index map.
  * Copies the entry into `entry` and returns true, or returns false if the ID is not
    present.

  [TEST: IDs 0, 1, 2 found with the right offsets and sizes; 99 not found]
* Use in `engines/smt/smt.cpp`:
  * Opens `umd0.cpk` (P3P) or `data.cpk`.
  * Calls `listMembers` to find loose `.ADX` tracks.
  * Calls `createReadStreamForMember`.

  It needs nothing beyond the API above.

### 8.4 Hostile and truncated archives

Every offset and size read from a table is untrusted. All arithmetic is in 64 bits and
checked against the file size before any seek or allocation. The CPK layer must never
call `error()` or assert on file contents. It degrades to fewer or no members.
`UTFTable::parse` failing is a normal "table unreadable" outcome. [DESIGN]

## 9. Reference data (P3P USA, PSP)

Files are under `~/Games/Visual Novel/PSP/Shin_Megami_Tensei_Persona_3_Portable_USA_PSP-PSPKiNG/PSP_GAME/USRDIR/`.
All values below are [VERIFIED].

### umd0.cpk (553,712,136 bytes)

The header values are:

* `TocOffset` = 0x800
* `ContentOffset` = 0x3A000
* `EtocOffset` = 0x20FFE000
* `Align` = 0x800
* `Files` = 5860
* `Tvers` = "CPKMC2.08.11, DLL2.51.11"

There is no ITOC or GTOC.

The TOC has 5860 rows. 5018 of them are compressed.

| TOC row | Path | Absolute offset | FileSize | ExtractSize | CRC32 of stored bytes |
|---|---|---|---|---|---|
| 0 | `data/battle/AICALC.TBL` | 0x1F92D000 | 114864 | 595712 | dc78955d |
| 5858 | `data/tool/effect/sample/W_SPIRL0.EP` (uncompressed) | 0x18B9C800 | 324 | 324 | 547df4fa |
| 5859 (last) | `data/tool/effect/sample/X_NOMAL0.EP` | 0x18B9D000 | 3292 | 5392 | 8f4ee7ed |

CRILAYLA test vectors give the CRC32 (zlib/IEEE) and MD5 of the **decompressed**
output:

| Path | Stored | U / C | Output size | CRC32 | MD5 | First output bytes |
|---|---|---|---|---|---|---|
| `data/tool/effect/sample/B_CAMRA0.EP` | 284 @ 0x13C52800 | 36 / 12 | 292 | 816c30b8 | ea36aba31a972c4221877b048c17f242 | `c8 00 00 00 04 00 ...` |
| `data/event/e630/e630_420.bf` | 368 @ 0x201E3800 | 355 / 96 | 611 | 9706d709 | d9ee16c4a78ab0c730935f7370ecba3f | `00 00 00 00 73 01 00 00 "FLW0"` |
| `data/tool/effect/sample/X_NOMAL0.EP` | 3292 @ 0x18B9D000 | 5136 / 3020 | 5392 | 71c09224 | b6993fd9f61ff174b63ae90bb4c679f3 | `d2 00 00 00 17 00 ...` |
| `data/camp/bustup/c_cha07a.tmx` | 15004 @ 0x1E008800 | 66368 / 14732 | 66624 | 076f6a60 | d49162f4028c1074c53fcb23905eb1c7 | `02 00 00 00 40 04 01 00 "TMX0"` |
| `data/field/pack/f026_008.pac` | 15000 @ 0x2096E000 | 50944 / 14728 | 51200 | 8f335cab | 1b9cf712d5e670d375a7647e9c9850c4 | `"f026.bf\0"` |

### umd1.cpk (338,630,752 bytes)

The header values are:

* `TocOffset` = 0x800
* `ContentOffset` = 0x4000
* `EtocOffset` = 0x142F0800
* `Align` = 0x800
* `Files` = 338

The TOC has 338 rows. None are compressed. All members are `AFS\0` archives, and
`DirName` is a constant column with the value `data/sound/voice`.

| TOC row | Path | Absolute offset | Size | CRC32 |
|---|---|---|---|---|
| 0 | `data/sound/voice/v100001.afs` | 0xE762000 | 442368 | a54a04ea |
| 337 (last) | `data/sound/voice/v905304.afs` | 0x81D1000 | 141312 | 0d270895 |

## 10. Open items

* Masked `@UTF` packets (section 1.3): scheme from LibCPK, unverified on real data.
  [UNCERTAIN]
* ITOC behaviour on real archives: 16-bit DataL sizes, nonzero ID bases and duplicate
  IDs come from vgmstream and the synthetic test only. [UNCERTAIN]
* `CpkExtendId` ITOCs (Persona 2 PSP, section 4): IDs map to TOC rows; not yet exposed
  by `getEntryById`, so duplicate-named TOC rows past the first are unreachable.
* Whether any real CPK stores a compressed member whose size fields disagree with the
  CRILAYLA header. This never happens in P3P. The spec rejects such members.
  [UNCERTAIN]
