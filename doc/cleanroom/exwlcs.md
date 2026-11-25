# EXWLCS archive format

Functional specification for the container the Koihime Doki engine reads as
`lcsebody1`. It describes the on-disk layout and the behaviour the engine
relies on, so that a reader can be written from this document alone.

Provenance: this document was written by describing the observable behaviour of
the reference reader that shipped with the engine, plus the invariants that
follow from the described algorithms. No reference source was copied into
this text. The format is not documented anywhere else that we know of.

Status of the byte-level details: **unverified against a real archive**. No
`lcsebody1`/`lcsebody1.lst` pair was available in the repository or in the
local corpora when this document was written (see "Verification" at the end).
Everything below is either derived from the reference reader or from arithmetic
that follows from it; the places where that is not enough are marked
**[uncertain]**.

## 1. Container model

An archive is a *pair* of files in the game directory, sharing one base name:

| File | Role |
| --- | --- |
| `<base>` (e.g. `lcsebody1`) | Body: the concatenated member payloads, no header, no footer |
| `<base>.lst` | Index: member count followed by one fixed-size record per member |

The two are independent files: the body has no self-description at all, and the
index carries every offset, length and name. A body without its `.lst` cannot be
read, and vice versa. Both are opened through the normal ScummVM search path,
so they may equally come from the game directory or from a path configured by
`--path`/`path` in the config file.

The engine instantiates exactly one of these pairs, base name `lcsebody1`
(`engines/koihime_doki/koihime_doki.cpp`). The base name is passed to the
factory, so any sibling pair with the same layout would work, but no other pair
name has been confirmed for this game.

## 2. Integer conventions

All integers in the index are 32-bit little-endian unsigned values. Offsets and
lengths are `uint32` and therefore cannot express members of 4 GiB or more; the
body is assumed to stay well below that.

## 3. Index layout

### 3.1 Header

The first 4 bytes of the `.lst` file are a single 32-bit encoded member count.
The encoding is a reversible obfuscation whose parameter is carried by the value
itself:

* Take the most significant byte *k* of the stored 32-bit value, that is file
  byte 3 because the value is little-endian.
* Form a 32-bit mask by replicating *k* into all four byte positions.
* The real member count is the stored value XOR that mask.

Two properties follow and should be preserved by any reader:

* The XOR clears the top byte (a byte XOR itself is zero), so the decoded count
  is always below 2^24 (16,777,216) whatever the stored value was. A decoded
  count with a non-zero top byte is therefore a contradiction and must be
  treated as a corrupt index.
* A stored value whose top byte is zero decodes to itself, i.e. for archives of
  fewer than 16.7 million members the whole obfuscation can be a no-op. Whether
  real archives use a non-zero *k* is **[uncertain]**; the decoder must handle
  both.

The same mask *k* is used to deobfuscate the entry records (section 3.2). It is
derived once, from the header, and never changes for the rest of the file.

### 3.2 Entry record

Immediately after the header comes one record per member, in index order,
76 bytes each, with no padding or alignment between records:

| Offset in record | Size | Field |
| --- | --- | --- |
| 0 | 4 | Absolute byte offset of the payload in the body file |
| 4 | 4 | Payload length in bytes |
| 8 | 64 | Member name, raw bytes, NUL-padded |
| 72 | 4 | Unknown (see below) |

The offset and length values and every byte of the name up to the stored
terminator are XORed with the mask from section 3.1. The unknown field is *not*
deobfuscated by the reference reader, which implies that either it is stored in
clear or its encoding was never needed.

Name field details:

* The name is a NUL-terminated byte string inside a fixed 64-byte field. Names
  can therefore be at most 63 bytes plus the terminator. The bytes after the
  terminator are padding (normally zeros) and are ignored.
* Because the obfuscation is applied to the terminator as well, an obfuscated
  record stores `k` where an unobfuscated record would store the zero
  terminator. The reference walk stops at the first *stored* zero byte and
  decodes everything before it, which is equivalent to decoding up to and
  including the terminator as long as the padding is zero. A name that contains
  a byte equal to `k` would be cut short by that rule; a rewrite should decode
  the fixed field and then stop at the first decoded zero instead.
* The name has no extension. Extensions are synthesised by the reader
  (section 4), and the name never contains a directory separator: all members
  are flat.
* The bytes are passed through as-is. No transcoding is applied, so names are
  very likely Shift-JIS or ASCII, and any byte outside ASCII is opaque to the
  case-insensitive comparison used for lookup.
* Names in the index are trimmed of leading and trailing ASCII whitespace
  after the extension has been appended. In practice padding is NUL, which
  already terminates the string, so this trim is mostly relevant for names
  that were padded with spaces; note that such padding ends up *inside* the
  final name, between the name and the synthesised extension.

The reference reader does not validate the unknown field, does not require the
records to be sorted, and does not check the body for a header of its own: the
first member may legitimately start at offset 0. Index order appears to be
packer order rather than alphabetical order and must not be relied on.

## 4. Member names and the synthesised extension

Since the index stores no extension, the reader sniffs bytes of the payload in
the body to decide what the member is. Three signatures are recognised, compared
as plain byte sequences at fixed positions:

| Test | Extension |
| --- | --- |
| bytes 0-1 are `42 4D` (`BM`) | `.bmp` |
| bytes 0-3 are `89 50 4E 47` | `.png` |
| bytes 6-9 are `4A 46 49 46` (`JFIF`) | `.jpg` |
| anything else | `.bin` |

The JPEG test looks at offset 6 rather than 0 because the standard JPEG stream
begins with two marker segments before the `JFIF` identifier.

The sniffed extension is appended to the stored name, so `TCG_BUN41AC` becomes
`TCG_BUN41AC.bmp`; that is the name the engine passes to
`createReadStreamForMember()` and the name `listMembers()` reports. Lookup is by
the full name including the extension; there is no extensionless fallback.

Consequences worth keeping in mind:

* The sniff runs on the *stored* bytes, not on decoded bytes.
* A payload shorter than 10 bytes cannot be tested for JPEG; a payload shorter
  than 4 cannot be tested for PNG. A rewrite must bound the tests by the actual
  length instead of reading past the buffer.
* A zero-length member has nothing to test and becomes `.bin`.

## 5. Payload obfuscation

The reader treats every member that is not one of the three image types as
XOR-encoded with a single-byte key, and takes that key from the fourth byte of
the *stored* payload (index 3). It applies the XOR to the whole member, start to
end, in place after loading it. There is one key per member and no chaining, so
the transformation is symmetric. Whether the packer really encoded those members
is the open question discussed at the end of this section.

Three structural facts follow from this (all certain, given the algorithm):

* Decoding always leaves a zero at offset 3, because the key XORs itself there.
  So every member that goes through the decode path has `00` at offset 3
  afterwards, whatever it held before.
* Because the key is read back from the stored data, encoding only round-trips
  when the plaintext's byte 3 is zero: the packer writes `stored[3] = 0 ^ k = k`
  and the reader then recovers `k`. A payload type with a non-zero fourth byte
  cannot be stored this way. BMP happens to qualify, since byte 3 of its header
  is the top byte of the 32-bit file size and is zero for files below 16 MiB.
* An encoded member has every one of its bytes shifted by `k`, so unless `k` is
  zero its first bytes no longer read `BM`, `\x89PNG` or `JFIF`; it therefore
  normally fails the sniff, falls into the `.bin` branch, and is decoded, while
  a plain image keeps its signature and is passed through untouched. This is
  presumably why the original reader could get away with guessing. Note the
  converse direction is not decidable from the data: a stored byte 3 of zero can
  belong to a plaintext file just as well as to an encoded one, so nothing in
  the payload proves which of the two it is.

Which members are obfuscated is decided *only* by the sniff result in the
reference reader: any member that is not recognised as BMP/PNG/JPEG is XORed
unconditionally. This has two consequences that a rewrite must decide about
rather than inherit silently:

* A plaintext member of an unrecognised type (a script, a text file, an
  audio header, a raw image format) would be silently corrupted by the XOR.
* An obfuscated member whose first bytes happen to spell a known signature
  would be left encoded.

The real discriminator is **[uncertain]**. The unused 4th record field would be
the natural place for a flag or per-entry key, but it has not been confirmed
against data, and the cipher leaves no marker of its own in the payload. Until
it is confirmed, the safest reading is: the archive only ever exposes images
through the engine today (the call site loads a `.bmp`), so preserving the
sniff-based rule keeps working behaviour, and the reader should document the
risk next to the decision.

There is no compression, no block structure and no dictionary; the XOR above is
the only encoding step. Payloads are stored as plain byte runs, but the index
does not require them to be contiguous or ordered, so holes between members and
overlaps are possible.

## 6. Public API

Namespace `KoihimeDoki::Format::Archive`.

```cpp
class EXWLCSArchive : public Common::Archive {
public:
    EXWLCSArchive(const Common::String &archiveBaseName);
    ~EXWLCSArchive() override;

    bool hasFile(const Common::Path &name) const override;
    int listMembers(Common::ArchiveMemberList &list) const override;
    const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
    Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;
};

EXWLCSArchive *EXWLCSFactory(const Common::String &name);
```

That is the whole contract; the private section, the `LCSENTRY`/`LCSHEADER`
structs and the `EXWLCSHeadersMap` typedef in the current header are internal
and have no users outside this translation unit, so a rewrite may drop them.

Required semantics:

* **Construction.** `archiveBaseName` is the body file name without `.lst`.
  Construction may be expensive: it reads the index and touches the body once
  per member to sniff extensions, so every member's name is known up front. The
  reference reads each payload in full just to look at its first bytes; only the
  first 10 bytes are needed, which matters for a body of hundreds of megabytes.
  After construction the archive is read-only and holds no open file handle;
  the base name is remembered because each read reopens the body.
* **Factory.** `EXWLCSFactory()` heap-allocates the archive and hands ownership
  to the caller; the engine stores it in a `Common::ScopedPtr<Common::Archive>`.
  The factory must not register the archive with `SearchMan` itself, and must
  not keep a static reference.
* **Failure.** The declared contract of the factory is "may return null on
  failure". The current call site does not check the result, so either the
  factory keeps returning a valid, empty archive when the index or body is
  missing (the present behaviour, which makes every query return "not found"),
  or the call site gains a null check. Pick one and make it explicit; a null
  return plus an unchecked call site is a crash.
* **`hasFile(name)`.** Case-insensitive (ASCII) comparison against the resolved
  names from section 4. `Common::Path` is converted to its string form first,
  so a path with directories will simply not match, which is correct for a flat
  archive. Must be safe for empty names.
* **`listMembers(list)`.** Appends one `Common::GenericArchiveMember` per
  member, parented to `*this`, and returns how many were appended. Order is
  unspecified (the reference iterates a hash map); index order is an acceptable
  and preferable choice. The returned members must keep working as long as the
  archive lives; they are owned by the shared pointers the list holds.
* **`getMember(name)`.** Returns a member for a known name, or an empty
  `ArchiveMemberPtr` for an unknown one. The reference returns a
  `GenericArchiveMember` built from the *requested* spelling of the name, which
  is why the case-insensitive lookup has to happen here too.
* **`createReadStreamForMember(name)`.** Returns a seekable, memory-backed
  stream over the payload, or null when the name is unknown. The caller owns
  the stream. The reference copies the payload into a heap buffer and wraps it
  in a `MemoryReadStream` with `DisposeAfterUse::YES`; the decoded size always
  equals the length from the index. Errors while reading the body are not
  reported by a null return in the reference, so a rewrite should either
  validate the body up front or return null on a failed read, but must not hand
  out a partially filled buffer.
* **Lifetime and threading.** Archive is immutable after construction and safe
  to query concurrently as long as each call opens its own file handle (or
  takes a lock). Destruction releases only the name table; nothing else is
  reference counted.
* **No alt streams, no directories, no case-preserving writeback.** The
  inherited defaults for `createReadStreamForMemberAltStream()`,
  `isPathDirectory()`, `getChildren()` and friends are sufficient.

## 7. Validation and edge cases

These are the cases a rewrite must define; the first group is what the
reference reader already does implicitly, the second is what it does *not* do
and what the rewrite should do instead.

Observed behaviour to preserve:

* Missing body, or missing `.lst`: warning, empty archive, no members.
* Member names are deduplicated by full resolved name, case-insensitively, with
  the last record in index order winning. Duplicates are silent today; a
  warning is cheap and useful.
* Zero members is legal and yields an empty archive.
* Trailing bytes after the last record in the `.lst` are ignored.
* An entry whose payload length is zero is legal and yields an empty stream.

Missing checks that must be added (a truncated or hand-edited pair must not
allocate wildly or stream uninitialised memory):

* Index shorter than `4 + 76 * count` bytes: reject the archive, do not
  allocate for a count that the file cannot hold. Since the decoded count is
  bounded by 2^24, a size check is enough to make the allocation safe.
* Header read failing outright, or a decoded top byte that is non-zero: reject.
* Name field without a NUL byte: the reference treats the field as a C string
  and reads past it. Use all 64 bytes as the name (and warn), or reject the
  entry; either way stay inside the field.
* `offset + length` beyond the end of the body, or wrapping in 32-bit
  arithmetic: skip the entry with a warning rather than returning a stream
  shorter than the declared length.
* The JPEG test must not read past a short payload; the BMP/PNG tests must be
  length-guarded too.
* A name that is empty after trimming would collide with the bare extension
  (`.bmp`); accept it as a name rather than inventing one, but do not crash.

## 8. Known, derived, uncertain

Known from the reference reader's behaviour: the file pair and the base-name
convention; the 4-byte header; the count/key scheme; the 76-byte record layout;
the field order; the three image signatures and the `.bin` fallback; the XOR
key at stored offset 3 applied to non-image members; the case-insensitive,
extension-appended name space.

Derived (follows from the above, no extra data needed): the count is always
below 2^24 and its top byte is the key; decoding zeroes a member's fourth byte,
and encoding is only reversible when that byte was already zero; a member that
was encoded looks scrambled to the sniff and therefore lands in the `.bin`
branch, which is what makes the guess work.

Uncertain, needs a real archive:

* Whether real `lcsebody1.lst` files use a non-zero key at all, and whether the
  4th record field is a flag, a key, a timestamp, or padding.
* Whether non-image members are really XOR-encoded, and what the true
  discriminator between encoded and plain members is.
* Whether the body ever has a leading header that offsets are relative to
  (evidence says no: the reference seeks to the recorded offset directly).
* The character encoding of member names, and whether any name exceeds 63
  bytes or contains a separator.
* Whether the index is exhaustive and unique in practice (no duplicate names,
  no overlapping payload ranges, no holes). The reference tolerates all three;
  the format does not forbid them.

## Verification

The format was not checked against real bytes for this document. The
repository contains no archive or index fixture, and a lookup of `lcsebody1`
under the local visual-novel corpus directories found none. Once a copy of the
game's data is available, the following checks would settle most of the
uncertainties above in a few minutes:

1. Read the first four bytes of `lcsebody1.lst`, decode the count, and compare
   it against `(file size - 4) / 76`; a match confirms the header and record
   size.
2. Dump the first few records and check that offsets and lengths sum to no more
   than the body size and that the name fields read as plausible Shift-JIS.
3. For one member that the sniff calls `.bin`, look at the stored and the
   decoded bytes side by side: if the decoded bytes look like a known format,
   the member really is encoded; if the stored bytes already look sane, the
   reader is silently corrupting a plaintext payload and the sniff needs a
   better discriminator.
4. Extract a `.bmp` and open it, confirming that the payload is stored plain.
