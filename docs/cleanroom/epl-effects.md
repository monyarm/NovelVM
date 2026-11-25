# EPL effects in Persona 3 FES / Persona 4 RMD models

This document is an implementation specification of the effect data embedded in the
Persona 3 FES (P3F) and Persona 4 (P4) PS2 `RMD` model files:

* chunk `0xF0F000E0` — an **EPL effect tree** (particles, rings, flash quads, wind
  strips, sprites, nested models, screen filters) attached to a model, a bone/helper or
  an animation slot;
* chunk `0xF0F000E1` — an **AfPack** weapon-trail ("afterimage") ribbon pack attached to an
  animation slot;
* chunk `0xF0F00006` — the helper **matrix table** that both use to find their
  attachment transforms.

It was written from the matched P4 decompilation (used as a reference only, nothing is
copied here), the partial P3F decompilation, data tables read from the retail P4
executable `SLUS_217.82` (and spot checks in P3F `SLUS_216.21`), and a 604-file RMD
corpus sample. Every non-obvious claim cites a function name and retail address
(`FUN_xxxxxxxx` = the address in the P4 ELF unless marked P3F) so a reviewer can
cross-check.

Earlier notes in `docs/bed-epl-format-notes.md` are partly superseded: the "192-byte
records" there are the 0xC0-byte tree nodes described here (the tag they saw at
payload+300 is the node *name* at node+0x9C of node 0, i.e. 0x90+0x9C = 300), the
"resKind 210/200" field is the primitive record *version tag*, and the dispatch key they
were looking for is the primitive `kind` at record+0x04 (not +0x0C/+0x14).

Confidence legend used throughout: **[H]** verified in code and corpus bytes;
**[M]** read from code, consistent with data, but some detail (often VU0 inline asm or a
helper whose body was not read) inferred; **[L]** plausible inference / not checked.

---

## 0. Quick summary

| Item | Result | Conf. |
|---|---|---|
| `0xF0F000E0` header | 20 bytes; `skip` = alignment padding so the payload starts on a 16-byte file offset; chunk size field excludes the padding | H |
| `0xF0F000E1` payload | not an EPL tree: an AfPack ribbon pack (`0x65` header word), fully parsed | H |
| EPL tree addressing | all offsets are relative to the payload (EPL) start; node `id` is a byte offset **unless** `sourceFlags` bit 0 is set, then it is a node *index* | H |
| Corpus parse | 25/25 `E0` payloads, 44/44 nodes, 163/163 `E1` payloads parse with zero errors | H |
| Kinds in corpus | 1, 4, 5, 7, 10, 12, 16, 26, 28 | H |
| Tick rate | one effect frame per model update tick (30 Hz) | M |

---

## 1. Conventions

### 1.1 Basic types

* All data is little-endian. `u8/u16/u32/s32/f32` as usual.
* `vec4` = 4×f32 (x, y, z, w). Positions are stored as `vec4` with w = 1 or 0 (w is
  carried along but ignored by the maths unless stated).
* `quat` = 4×f32 (x, y, z, w). Identity = (0,0,0,1).
* Colours are packed `u32` RwRGBA: byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A, so
  the u32 `0xFF00A6FA` is R=0xFA, G=0xA6, B=0x00, A=0xFF. 255 = 1.0.
* Colour multiplication everywhere in this system is "normalise both to [0,1] (×1/255),
  multiply per channel, ×255, truncate toward zero" (`mdlEffectApplyPackedColor`
  FUN_004865C0 and many others; the 1/255 constant is the float at gp-0x7FBC =
  0x00761134).
* When a computed colour is written into an RpMaterial and its alpha is exactly 0xFF,
  the game writes 0xFE instead (and restores 0xFF in its local copy). RenderWare treats
  material alpha 255 as "opaque, no blending"; writing 0xFE keeps blending enabled. An
  implementation that always alpha-blends can ignore this.

### 1.2 Rotation convention

Quaternions are turned into 3×3 matrices by `effMiscBuildRotationVU` (FUN_004BCEB0).
With q = (x,y,z,w) the rows are

```
R0 = (1-2y²-2z²,  2xy-2zw,   2xz+2yw)
R1 = (2xy+2zw,    1-2x²-2z², 2yz-2xw)
R2 = (2xz-2yw,    2yz+2xw,   1-2x²-2y²)
```

and a vector is transformed as a *row vector*: `v' = v.x·R0 + v.y·R1 + v.z·R2`
(RenderWare style, `p' = p·M`). Quaternion products use the Hamilton product
`a⊗b` (`effMiscQuatMultiplyVU` FUN_004BCE50 computes
`xyz = a.w·b.xyz + b.w·a.xyz + a.xyz × b.xyz`, `w = a.w·b.w − a.xyz·b.xyz`). With the
row-vector convention, `M(a⊗b) = M(a)·M(b)`, i.e. `child ⊗ parent` applies the child
rotation first and the parent rotation second.

Euler → quaternion (`effMiscEulerToQuatVU` FUN_004BCF20), angles in radians: with
`hx=-ax/2, hy=-ay/2, hz=-az/2`, `cX=cos hx, sX=sin hx`, etc.:

```
qx = cX·sZ·sY + sX·cZ·cY
qy = cX·cZ·sY − sX·sZ·cY
qz = cX·sZ·cY + sX·cZ·sY
qw = cX·cZ·cY − sX·sZ·sY
```

### 1.3 Random numbers [H]

All effect randomness uses one global generator (`effMiscRand` FUN_004BD050 with a NULL
state), seeded once at boot by `TnInitModule` (FUN_00481A20) with `0x12345678`
(P3F: same seed, P3F decomp `mdlEffect.c` line ~3620).

State is four u32 `x0..x3`. Seeding (`effMiscRandInit` FUN_004BD130), with
`rotr8(v) = (v >> 8) | (v << 24)`:

```
x0 = seed ^ 0x0AED1A0C
x1 = rotr8(x0)
x2 = rotr8(x1 ^ 0xAA5A02FE)
x3 = rotr8(x2 ^ 0x11BE81C7)
```

Step:

```
r  = ((x1 << 2) | (x0 >> 30)) ^ ((x3 << 1) | (x2 >> 31))      (32-bit)
x3 = x2; x2 = x1; x1 = x0; x0 = r; return r
```

`randFloat() = (rand() & 0xFFFFFF) / 16777216.0` (FUN_004BD0B0), range [0,1).

"Variation" fields are applied everywhere as
`value · ((1 − var) + var · randFloat())` — i.e. `var` is the fraction of the value that is
randomised downward (var = 0 → exact, var = 1 → uniform in [0, value)).
Bit-exact replay is only possible if the same calls happen in the same order; since the
generator is shared with the rest of the game this is not reproducible anyway.

### 1.4 Time base [M]

Effects have no time in seconds: every primitive counts integer **frames**. A frame is
one call of the per-model advance (`mdlAdvanceModel` FUN_00478A30 with `tick != 0`,
reached from `mdlQueueModelForDraw` FUN_00479100), i.e. one per game render tick. The
game runs its model/animation clock at 30 Hz (animation time step constant
gp-0x7FC0 = 1/30, AfPack fades divide frame counts by 30). Implement effects at 30 ticks
per second.

### 1.5 Global effect textures [H]

`TnInitModule` (FUN_00481A20) loads 22 textures (`0x16`) from the archive
`effect/effpack.bin` (DATA.CVM), named `effect/be_%04d.tmx` with numbers 1..22, into
slots 0..21. "Texture slot N" in this document therefore means `effect/be_{N+1:04d}.tmx`.
P3F has the same strings (`SLUS_216.21` file offset 0x59BBD7/0x59BBF7) and the same
loop count 22 (P3F decomp `mdlEffect.c` line 3602). `TnSlotObject` (FUN_00481300)
asserts slot < 22.

---

## 2. Chunk level

The RMD aux chunk reader is `mdlReadAuxChunks` (FUN_0047B0C0, `src/Graphics/Model/mdlManager.c`;
P3F equivalent in P3F `mdlManager.c` around line 6760). It walks top-level RW chunks
(12-byte header `u32 type, u32 size, u32 version`) and keeps this state:

* `layer` — incremented by `0xF0F00002` (and `slot` reset to 0);
* `slot` — the animation slot counter, incremented by every `0xF0F00004` chunk;
* whether a clump has been seen (`0x10` chunk) — **before** the first clump the model
  has no clump, so effect chunks go to the model-wide list;
* `capacity` — from `0xF0F000F0` (u16), default 32; size of per-layer slot tables.

### 2.1 `0xF0F000E0` — EPL effect block [H]

Chunk body:

| Off | Type | Name | Meaning |
|---|---|---|---|
| 0x00 | u16 | `first` | first attachment index (matrix-table id) |
| 0x02 | u16 | `last` | last attachment index (inclusive) |
| 0x04 | u32 | `length` | payload length in bytes |
| 0x08 | u32 | `pad` ("skip") | number of padding bytes between this header and the payload |
| 0x0C | u32 | `flags` | bit 0: restart this effect each time the slot's animation loops (§2.4). Other bits unused. |
| 0x10 | u32 | `reserved` | never read by the loader (always 0 in the corpus) |
| 0x14 | `pad` bytes | padding | |
| 0x14+pad | `length` bytes | payload | EPL tree (§3) |

* The chunk-header `size` field is always exactly `20 + length` — **it does not include
  the padding**. A generic RW walker must therefore advance by `12 + size + pad`
  (P4 reads the 20 bytes, skips `pad`, copies `length`, skips `length`). Without this,
  walkers desynchronise after the first E0 chunk, which explains earlier "chunks that do
  not parse". [H: `mdlReadAuxChunks` case `0xf0f000e0`; corpus: all 25 chunks have
  `(chunkOffset + 12 + 20 + pad) % 16 == 0` and pad ∈ 0..15.]
* The padding makes the payload start at a 16-byte-aligned offset from the start of the
  file (the file is loaded into a 16-aligned buffer, and the tree contains `vec4`/quad
  data that VU0 loads with aligned 128-bit loads).
* The payload is copied into a refcounted block (`mdlEffectListAppendBlock` FUN_0047D320:
  a 0x10-byte header {payload ptr, prototype handle, refcount, flags} followed by the
  copy) and one **list node** is appended for index `first`; for `first+1 … last` further
  list nodes share the same block (`mdlEffectListAppendNode` FUN_0047D460). Each list
  node carries its own `matrixIndex` (u16) and its own live effect instance. So one
  E0 chunk with `first=7,last=16` spawns ten independent copies of the effect at
  helpers 7…16.
* Which list it goes to:
  * if no clump has been loaded yet (E0 before the first `0x10` chunk):
    the model-wide list `model->effects`;
  * otherwise: `layer.attachments.primary[slot]` of the current animation layer and
    slot (the table is created on demand with `capacity` entries).

Corpus: P3F 4 model-wide + 6 slot-attached; P4 10 model-wide + 5 slot-attached
(`eplprobe/attach.py`).

### 2.2 `0xF0F000E1` — AfPack (weapon trail) block [H]

The whole chunk body is the payload (no header). It is wrapped by
`mdlEffectListWrapCopy` (FUN_0047DB50) and stored in
`layer.attachments.secondary[slot]` for the current layer/slot. The format is described
in §15. It is **not** an EPL tree; it is consumed by the "afterimage" system
(`eff_afpack.c`, `k_vpad.c`). There is at most one per slot.

### 2.3 `0xF0F00006` — helper matrix table [H]

Body:

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u16 | `count` |
| 0x02 | `count` × 0x48 | entries |

Entry (0x48 bytes on disk; the loader stores it as 0x50 bytes in memory with the
matrix first):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | `id` — helper id referenced by E0 `first..last` and AfPack `start/end` |
| 0x04 | u32 | `frameId` — HAnim node ID of the bone the helper is attached to |
| 0x08 | RwMatrix (64 bytes) | offset matrix in bone space: right(3f)+flags, up(3f)+pad, at(3f)+pad, pos(3f)+pad. The pad words are uninitialised garbage (`0xCDCDCDCD`) |

Chunk size is exactly `2 + count·0x48` in all 382 tables of the corpus.

**Lookup** (`mdlCopyIndexedMatrix` FUN_0047A510 → inline `mdlMatrixFromTable`):

1. Find the entry whose `id == index & 0xFFFF` (linear search, first match).
2. Find the clump frame whose HAnim ID equals `frameId` (search starts at the clump's
   root frame, depth-first; helper `func_00397470` reads a frame's HAnim ID).
3. Result = `entry.matrix × LTM(frame)` (RW `RwMatrixMultiply(dst, A, B)`, row-vector
   order: the offset is expressed in bone space, then the bone's world matrix is
   applied). Return success.
4. If the model has **no** matrix table, instead search the clump for a frame whose
   integer user-data `"per3HelperId"` equals the index and return its LTM
   (`K_Clump_FindIntUsrDataFrame`; key string at 0x007131D8).
5. If nothing was found, return failure.

### 2.4 Runtime life cycle of effect lists [H unless noted]

**Instances.** A list node owns a live tree instance (`handle`). Handles are created
by `mdlEffectListRefreshHandles` (FUN_0047DA30): the first node that uses a data block
builds a tree from the payload (`mdlEffectBuildFromResource`, §3.5) and stores it in
the block as the *prototype*; later nodes using the same block *clone* the prototype
(`mdlEffectCloneWithNodes` FUN_00485C80). Handles are destroyed by
`mdlEffectListReleaseHandles` (FUN_0047DAE0).

* The model-wide list gets its handles when the model is prepared
  (`mdlManager.c` ~line 4493) and when a model object is cloned
  (`mdlCopyModelState` FUN_00478410).
* Per-slot lists: when animation `a` is started on a layer
  (`mdlManager.c` ~line 2490, the layer's "set animation" routine) the layer's
  attachment table records `nextPrimary = primary[a]`, `nextSecondary = secondary[a]`
  (or none if the play flags contain 0x20), sets `pendingSecondary`, and sets
  `delay = blendTicks` (the animation cross-fade length). On the next
  `mdlAdvanceModel`, if `nextPrimary != primaryDraw` the old list's handles are destroyed
  and the new list's handles are created — **every slot switch restarts the slot's
  effects from frame 0**. The same happens for the AfPack (§15).
* While `delay > 0` the slot's effects are not advanced; `delay` counts down once per
  tick.
* When the current animation of a looping layer wraps (`mdlResolveLoopAnimation`
  FUN_00473350, loop callback), every node of the slot's primary list whose data block
  has E0 `flags` bit 0 set is restarted (`mdlEffectListCallFlagged` FUN_0047D840 →
  `mdlEffectStartChildren` FUN_00485FE0: all primitives get their *reset* callback and the
  tree frame counter goes back to 0). Effects without bit 0 keep running across loops.
  (Only 1 corpus chunk sets it: P4 `M016_047.RMD`.)

**Per-tick update** (`mdlAdvanceModel` FUN_00478A30, when `tick != 0`):

1. Model-wide list: set the tree scale to the average of the model's scale vector
   (`mdlEffectListSetUniformScale` FUN_0047D900: `s = (sx+sy+sz)/3`, applied with
   `mdlEffectSetChildScales`, §3.6), then `mdlEffectListBuildTransforms` (below).
2. For each animation layer with an attachment table and `delay == 0`: the same for the
   slot's primary list (scale = the layer's stored scale, which is the model scale copied
   at draw time) and refresh of the AfPack (§15).

**`mdlEffectListBuildTransforms`** (FUN_0047D540), per list node:

1. `ok = mdlCopyIndexedMatrix(model, node.matrixIndex, M)`; if it fails, use the model's
   own world matrix (the first 64 bytes of the Model object).
2. If `matrixIndex >= 1000`: rotation = identity, translation = `M.pos`
   ("position-only" helpers; used by P4 weapons `WP0BB`/`WP0C5` with index 1000 so the
   effect stays upright).
3. Otherwise: orthonormalise `M` (RW `RwMatrixOrthoNormalize`, func_003E0670), convert
   to a quaternion, normalise it, rebuild the rotation from it (scale/shear removed);
   translation = `M.pos`.
4. `mdlEffectSetMatrixPose` (FUN_004B12E0): set the tree's world position to the
   translation (`mdlEffectSetChildPositions`) and its world rotation to the quaternion
   of the rotation (`mdlEffectSetChildRotations`), §3.6.
5. Advance the tree one frame: `mdlEffectCommitTreeFrame` (FUN_00485630, §3.7).

**Per-frame draw** (`mdlQueueModelForDraw` FUN_00479100, after the advance):

* Model-wide list: unless the model flags contain 0x80, compute
  `colour = modelColour × modelTint(model+0x300)` (per-channel, normalised), apply it to
  every tree (`mdlEffectListSetColor` → tree colour, §3.6), and draw the list only if the
  resulting alpha > 0. With flag 0x80 the colour is not applied and the list is always
  drawn. Draw = `mdlEffectListInvokeEach` (FUN_0047D7E0) → per node
  `mdlEffectFlushWithTable(owner, handle)` (FUN_004B11D0): select the owner's draw-list
  table (`TnSetTableBase`) and run `mdlEffectFlushTreeFrame` (FUN_00485870, §3.7).
* Slot lists: the layer's stored colour is the model colour (unless flag 0x80); the
  list's tree colour is set and the list is drawn without the alpha test.
* Model flag 0x20000 suppresses all effect drawing.

---

## 3. EPL tree (payload of `0xF0F000E0`)

All offsets in this section are byte offsets **from the start of the payload** (= the
EPL block). There is no fix-up pass: the runtime builder (`mdlEffectBuildFromResource`
FUN_00484BB0) reads the payload in place, resolving offsets on the fly. The same layout
is the standalone `.EPL` file format (an EPL file is one tree starting at file offset 0;
the earlier note's "first tag at +300" is node 0's name) [M: offsets agree, standalone
files not re-parsed in this work].

```
payload:
  0x000  root header            0x90 bytes
  0x090  node[0 .. count-1]     0xC0 bytes each (node array offset is root+0x88; always 0x90)
  ....   primitive records      each: 0x2C header + parameter block + optional secondary resource
```

### 3.1 Effect state block (0x80 bytes; first part of root and of every node)

`mdlEffectInitParticleState` (FUN_00484B30) initialises it; offsets are shared by root and
nodes, but some fields are only meaningful for one of them.

| Off | Type | Root meaning | Node meaning | Default |
|---|---|---|---|---|
| 0x00 | vec4 | runtime: rotated local offset (see §3.6). **File value = local offset of the whole effect** (copied into both 0x00 and 0x10 at build) | unused | (0,0,0,1) |
| 0x10 | vec4 | runtime: un-rotated local offset. File value **ignored** (overwritten with file 0x00) | unused | (0,0,0,1) |
| 0x20 | vec4 | unused | `scaleVector` — per-axis scale used when state flag 0x100 is set | (1,1,1,1) |
| 0x30 | 16 bytes | unused | unused | 0 |
| 0x40 | vec4 | runtime: world position (set by `SetChildPositions`); file value ignored | `offset` — local position of the child relative to the effect origin | (0,5,0,1) |
| 0x50 | quat | runtime: world rotation; file value ignored | `orientation` — local rotation of the child | (0,0,0,1) |
| 0x60 | f32 | `scale` (runtime; set from model scale) | `scale` — child scale | 1.0 |
| 0x64 | u32 | `colour` (runtime) | `colour` — child colour, multiplied with the tree colour | 0xFFFFFFFF |
| 0x68 | u32 | state flags (copied from file) | state flags | 0x80 |
| 0x6C | u32 | — | — | 0 |
| 0x70 | u32 | — | — | 0 |
| 0x74 | f32 | `multiplier` (copied from file): extra factor on the tree scale given to children | unused | 1.0 |
| 0x78 | 8 bytes | — | — | 0 |

State flag bits (offset 0x68):

| Bit | Effect (node) | Source |
|---|---|---|
| 0x04 | the base point used for this child has its **y replaced by 5.0** (absolute) before the offset is added | `mdlEffectSetChildPositions` FUN_004861F0, `mdlEffectCommitTreeFrame` |
| 0x08 | offset is re-aimed toward the camera every frame (length preserved) | `mdlEffectResolveChildOffset` FUN_00486840 |
| 0x10 | as 0x08 but horizontal only (y of the offset kept) | same |
| 0x20 | orientation = full camera-facing aim (pitch + yaw) | `mdlEffectResolveChildOrientation` FUN_00486970 |
| 0x40 | orientation = node orientation ⊗ yaw-only camera aim | same |
| 0x80 | offset is multiplied by the parent scale | `SetChildPositions`, `SetChildScales`, `Commit` |
| 0x100 | scale is a vector (`scaleVector × scale`), sent through the primitive's vector-scale callback instead of the scalar one | `mdlEffectSetChildScales` FUN_00486400 |
| 0x80000000 | root only, runtime: "commit already done this frame" | `Commit`/`Flush` |

Corpus: node flags are 0x80 (37 nodes) or 0xA0 (7 nodes: flash quads and discs that
must face the camera); root flags 0x80 in all trees.

### 3.2 Root header (0x90 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | state block (0x80) | see §3.1; only 0x00 (local offset), 0x68 (flags) and 0x74 (multiplier) are read from the file |
| 0x80 | s32 | `count` — number of nodes |
| 0x84 | u32 | frame counter (runtime; 0 in file) |
| 0x88 | s32 | file: offset of the node array (always 0x90). Runtime: last-node pointer |
| 0x8C | s32 | file: **must be 0**. Runtime: first-node pointer. The builder uses "0x8C == 0" to recognise a file image (indexed mode) versus an already-linked tree |

Corpus root local offsets: mostly 0; e.g. (0,80,0) and (0,0,0) with z/w 450 in field 0x10
(ignored). Root multipliers 1.0, 2.0 and 2.5 occur.

### 3.3 Node (0xC0 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | state block (0x80) | §3.1 |
| 0x80 | s32 | `startFrame` — node is skipped until the tree frame counter ≥ this |
| 0x84 | u32 | node flags: bit 0 = scale inherits parent scale (scale × parentScale); bit 1 = node disabled (never advanced or drawn). Builder default 1; corpus always 1 |
| 0x88 | u8[4] | `attributes` — builder default {8,0,0,0}; **no runtime reader found** (corpus: 0x8, 0x305, 0xC00, 0x400, 0xC0C00) — editor data |
| 0x8C | s32 | must be 0 in file (runtime child pointer) |
| 0x90 | s32 | `id`: if `sourceFlags` bit 0 is clear, byte offset of this node's primitive record from the payload start; if set, **index** of an earlier node to clone |
| 0x94 | s32 | `sourceIndex` — node index whose secondary resource is shared (only with `sourceFlags` bit 1) |
| 0x98 | u32 | `sourceFlags` — bit 0: clone the primitive of node `id`; bit 1: share the secondary resource of node `sourceIndex` |
| 0x9C | char[16] | `name`, NUL-terminated (e.g. `P-SPIRL`, `N-SPIRL`, `S-RADI`, `R-CRCLE`, `MODEL`) — informative only, never used for dispatch |
| 0xAC | u32 | runtime next pointer (0 in file) |
| 0xB0 | u32 | runtime previous pointer (0 in file) |
| 0xB4 | 12 bytes | 0 |

The builder copies the first 0x90 bytes (state + 0x80..0x8F) and the name from the file
node into the runtime node (`mdlEffectCopyRecord` FUN_00486710, `strcpy`), so all of
state/startFrame/flags/attributes come from the file.

The name prefix letter matches the kind in the corpus (P-=particle, M-=object particle,
N-=wind, S-=flash quad, R-=ring, T-=board, F-/B-=screen filter, MODEL=model) but the game
never reads it.

### 3.4 Primitive record

Every non-clone node's `id` points to a record:

| Off | Type | Name | Meaning |
|---|---|---|---|
| 0x00 | u32 | `version` | must be ≤ 0xD2 (210) (assert in builder). Corpus: 210 everywhere except one P3F record with 200 (`M034_011` S-SPK_T), same layout |
| 0x04 | u16 | `kind` | primitive kind, < 0x21 (33); selects the handler row (§4) |
| 0x06 | u16 | | 0 |
| 0x08 | u32 | | 0 in file (runtime instance pointer in the runtime copy) |
| 0x0C | u16 | `sub` | sub-type within the kind (emitter model, ring style, …) |
| 0x0E | 2 bytes | | 0 |
| 0x10 | s32 | `paramOffset` | offset of the parameter block **relative to this record** (always 0x2C) |
| 0x14 | s32 | `paramSize` | size of the parameter block (equals the kind/sub table size, §4–§13) |
| 0x18 | s32 | `paramAlloc` | must be 0 in file; non-zero means `paramOffset` is an absolute pointer (runtime-patched records) |
| 0x1C | u16 | `srcKind` | type of the secondary resource (table below) |
| 0x1E | 2 bytes | | 0 |
| 0x20 | s32 | `secOffset` | offset of the secondary resource **relative to this record**, 0 = none |
| 0x24 | s32 | `secSize` | size of the secondary resource in bytes |
| 0x28 | s32 | `secAlloc` | must be 0 in file (like `paramAlloc`) |
| 0x2C | `paramSize` bytes | parameters | kind-specific (§6–§13) |
| …  | | padding to 16 | |
| `secOffset` | `secSize` bytes | secondary resource | |

Pointer resolution (`mdlEffectResolveRelPtrA/B` FUN_00484490/FUN_004844D0): if the
alloc word is non-zero the offset word is already a pointer; else if the offset is 0
there is no block; else `block = record + offset`.

Secondary resource kinds (`srcKind`):

| srcKind | Content | Used by | In corpus |
|---|---|---|---|
| 1 | an embedded **TMX** texture file (`u16 2`, `u16 0`, `u32 fileSize`(=secSize), `"TMX0"`, …) uploaded with `tmxUploadStream` (`TnUploadStreamTagged` FUN_00481390) | particles (kind 1), wind, flash quads, boards | 7 |
| 2 | an animated texture "track" resource (`mdlEffectCreateTrackInstance`) | particles subtype 2, boards | 0 |
| 3 | an embedded RenderWare stream: `0xF0F000F0`, `0x16` texture dictionary, `0x10` clump (+ animations), i.e. a mini-RMD; `secSize` is its length | object particles (28), models (26) | 4 |
| 4 | `u32` global texture slot 0..21 (§1.5) | particles, boards. **Wind (16) and flash quads (10) ignore the value** and use built-in default slots (§9, §10). Rings ignore textures entirely | 31 |
| 5 | a nested primitive record (child primitive per particle) | particles subtype 5 | 0 |
| 6 | a nested EPL tree (sub-effect per particle) | particles subtype 6 | 0 |
| 7 | a TLP resource (`TlpCreate`) | particles subtype 7, boards | 0 |

### 3.5 Building a runtime tree (`mdlEffectBuildFromResource` FUN_00484BB0) [H]

Pseudocode (input `E` = payload start):

```
root = new state block (defaults)
root.localOffset (0x00 and 0x10) = E.root[0x00]
root.multiplier = E.root[0x74];  root.flags = E.root[0x68]
for i in 0 .. E.root.count-1:
    f = node i at E + E.root.arrayOffset + i*0xC0
    n = new node (defaults; flags=1, attributes={8,0,0})
    if f.sourceFlags & 1:                       # clone
        proto = runtime node number f.id (0-based, must already exist)
        n.prim = new record {version 0xD2, kind, sub, srcKind copied from proto}
        n.prim.instance = handlers[kind].clone(proto.prim.instance)
    else:
        rec = E + f.id
        if f.sourceFlags & 2:                   # share secondary
            assert rec.secOffset == 0 and rec.secSize == 0
            src = E + node[f.sourceIndex].id   # the other node's record
            temporarily set rec.secSize = src.secSize and
                             rec.secOffset = (address of src's secondary) - rec
        n.prim = new record {version 0xD2, kind, sub, srcKind copied from rec}
        n.prim.instance = handlers[kind].create(rec)
        (restore rec.secOffset/secSize to 0 if they were patched)
    copy f[0x00..0x8F] into n; copy name
    append n to root's child list
SetChildPositions(root, (0,0,0,1)); SetChildRotations(root, identity)
SetChildScales(root, 1.0); ApplyPackedColor(root, 0xFFFFFFFF)
```

`create` receives the **file record** (not a copy) and reads its parameter block and
secondary resource through the relative offsets. Clones (`sourceFlags` bit 0) share
the prototype's resources (textures, clumps) through the kind's clone handler. If the
tree is given in linked form (root 0x8C ≠ 0) clones are resolved by `id` value
(`mdlEffectFindNodeById` FUN_00486740) — not relevant for file data.

Destruction (`mdlEffectDestroyWithNodes` FUN_00485B20) calls each kind's destroy
handler (slot 0x10) for non-clone nodes.

### 3.6 Transform, scale and colour propagation [H for structure, M for exact VU maths]

Notation: `R(q)` = rotation of §1.2; root fields `P0` = root[0x00] (rotated local offset),
`L` = root[0x10] (local offset), `W` = root[0x40] (world position), `Q` = root[0x50]
(world rotation), `s` = root.scale, `m` = root.multiplier, `S = s·m`.

**`SetChildRotations(root, q)`** (FUN_00486330):

1. `Q = q`; `P0 = L·R(q)` (the local offset rotated into world).
2. For every child: `rotation_child = child.orientation ⊗ q`; call the primitive's
   *set rotation* (slot 0x24).
3. Call `SetChildPositions(root, W)`.

**`SetChildPositions(root, p)`** (FUN_004861F0):

1. `W = p`; `origin = W + P0`.
2. For every child: `base = origin`, and if child flag 0x04: `base.y = 5.0`.
   `o = child.offset`, and if child flag 0x80: `o = o·S`.
   `position = base + o·R(Q)`; call *set position* (slot 0x20).

**`SetChildScales(root, v)`** (FUN_00486400):

1. `root.scale = v`; `S = v·m`.
2. For every child:
   * if child flag 0x100 is clear: `k = child.scale`, times `S` if node flag bit 0;
     call *set scale* (slot 0x28) with `k`;
   * else: `k3 = child.scaleVector · (child.scale · (node bit0 ? S : 1))`; call
     *set vector scale* (slot 0x2C).
   * If child flag 0x80: recompute its position as `origin(+y=5 if 0x04) + child.offset·S`
     (**without** the rotation — a quirk of the retail code) and call *set position*.

**`ApplyPackedColor(root, c)`** (FUN_004865C0): `root.colour = c`; for every child,
`child colour sent = c × child.colour` (normalised multiply); call *set colour*
(slot 0x30).

### 3.7 Advancing and drawing a tree [H]

**`mdlEffectCommitTreeFrame`** (FUN_00485630), once per tick (after the transform for
this tick has been applied):

1. `origin = W + P0`.
2. If root flags & 0x60 (whole tree camera-facing): compute the aim rotation for the
   root (as below) and run `SetChildRotations` with it, then restore `Q` (the stored
   world rotation is not overwritten).
3. For each child with `frame >= child.startFrame` and node flag bit 1 clear:
   * if child flags & 0x18: `o = ResolveChildOffset(child)`, `base = origin` (y = 5 if
     0x04), `o·S` if 0x80, position = `base + o` (**not** rotated by Q) → *set position*;
   * if child flags & 0x60: `r = ResolveChildOrientation(child)` → *set rotation*;
   * call the primitive's **advance** (slot 0x08).
4. Set root flag 0x80000000; `frame += 1`.

**`mdlEffectFlushTreeFrame`** (FUN_00485870), the draw pass:

1. If `frame <= 0` return.
2. If root flag 0x80000000 is *clear* (the tree was not committed this tick), redo the
   camera-dependent position/rotation updates of step 2–3 above.
3. For each child with `frame − 1 >= startFrame` and not disabled: call the primitive's
   **draw** (slot 0x0C).
4. Clear root flag 0x80000000.

**Camera aim.** `d = normalize(origin − cameraPosition)` (`mdlEffectAimDirection`
FUN_0048A2B0; camera = current RW camera frame LTM position).

* `ResolveChildOffset`: if flags & 0x18 == 0 the child offset unchanged. Otherwise
  `len = |offset|` (offset.y treated as 0 when flag 0x10); `dir = −d`
  (with dir.y = 0 when flag 0x10), normalised; result = `dir·len`, and with flag 0x10
  result.y = original offset.y. (The offset is swung around to point at the camera.)
* `ResolveChildOrientation`: if flags & 0x60 == 0 the node orientation. Otherwise
  `pitch = (flag 0x40) ? 0 : −acos(−d.y)`, `yaw = atan2(d.x, d.z)`,
  `aim = EulerToQuat(pitch, yaw, 0)`; result = `aim` for 0x20, or
  `orientation ⊗ aim` when 0x40 is set. (`func_0044B920` = acosf per the P3F
  cross-reference, `func_0044B950` = atan2f(a, b).) [M]

  The effect of 0x20 on a primitive built in its local XZ plane (rings, flash rays,
  boards) is that the plane faces the camera: camera level with the effect gives a 90°
  pitch, camera straight above gives none.

**Tree frame and end of effect.** The frame counter only gates `startFrame`; there is no
tree-level duration or loop. Each primitive owns its own frame counter, duration and
"finished" state (kind-specific). A tree restarts only via the restart path (§2.4,
`mdlEffectStartChildren`: every primitive's reset slot 0x1C, frame = 0). Completion
query `mdlEffectChildrenAllDone` (FUN_00486060, slot 0x34) and activity query
(FUN_004860F0, slot 0x38) exist for scripted effects but are not used by the RMD path.

---

## 4. Primitive kind handler table

P4: 33 rows × 0x40 bytes at **0x00713480** (file offset = vaddr − 0x100000 + 0x80).
Slot semantics (established from the callers listed):

| Slot | Off | Role | Called by |
|---|---|---|---|
| 0 | 0x00 | `create(record)` → instance | builder §3.5 |
| 1 | 0x04 | advance + draw in one call | scripted/battle paths (`mdlEffectAdvanceAndDrawChildren` FUN_00485AE0 uses 2 and 3) |
| 2 | 0x08 | **advance** one frame | `mdlEffectCommitTreeFrame` |
| 3 | 0x0C | **draw** (submit to draw lists) | `mdlEffectFlushTreeFrame` |
| 4 | 0x10 | destroy | `mdlEffectDestroyWithNodes` |
| 5 | 0x14 | clone(instance) | builder (sourceFlags bit 0), `mdlEffectCloneWithNodes` |
| 6 | 0x18 | share/rebind resources from another instance | particle mode copy etc. |
| 7 | 0x1C | reset/restart (frame counters to 0) | `mdlEffectStartChildren` |
| 8 | 0x20 | set position (vec4) | `SetChildPositions`, `Commit` |
| 9 | 0x24 | set rotation (quat) | `SetChildRotations`, `Commit` |
| 10 | 0x28 | set scalar scale (f32) | `SetChildScales` |
| 11 | 0x2C | set vector scale (vec3) | `SetChildScales` |
| 12 | 0x30 | set colour (packed u32) | `ApplyPackedColor` |
| 13 | 0x34 | is finished (null = yes) | FUN_00486060 |
| 14 | 0x38 | is active (null = no) | FUN_004860F0 |
| 15 | 0x3C | update with record | FUN_00486180 |

Rows (create / advance / draw handler names from the P4 decomp; rows 0, 3, 6, 8, 11, 14,
17–20, 22, 24, 25 are null rows using the shared no-op handlers):

| Kind | Name / family | create | advance (0x08) | draw (0x0C) | Notes |
|---|---|---|---|---|---|
| 1 | particle (PTank billboards) | ParticleCreateFromSource FUN_00486B00 | FUN_00489E00 | FUN_00489E50 | §6 |
| 2 | track (trail polygon from emitter) | effTrackCreateFromEmitter | effTrackUpdateSlots | effTrackApplyWidth | not in corpus |
| 4 | screen blur, copied framebuffer | effBlurFilterCopiedInstantiate FUN_004AB060 | FUN_004AB260 | FUN_004AB300 | §13 |
| 5 | screen filter, direct | effBlurFilterDirectInstantiate FUN_004AB5A0 | FUN_004AB7A0 | FUN_004AB840 | §13 |
| 7 | board sprite (T-BOARD) | mdlEffectCreateInstance FUN_004A5BB0 | FUN_004A5FA0 (age++) | EffSpriteUpdate FUN_004A5FC0 | §11 |
| 9 | lens flare | mdlEffectCreateGridForObject | | mdlEffectRenderFlares | not in corpus |
| 10 | flash quads | FlashQuadCopyAllocFromParent FUN_004A1780 | FUN_004A1AF0 | FUN_004A1B50 | §9 |
| 12 | ring | RingCreateKindInstanceFromObject FUN_0049A4E0 | FUN_0049A660 | FUN_0049A6C0 | §8 |
| 13 | thunder | effThunderCreateFromSource | | | not in corpus |
| 15 | track with mode | effTrackCreateWithMode | | | not in corpus |
| 16 | wind strips | WindCreateKindEntry FUN_004A5750 | FUN_004A5A00 | FUN_004A5A60 | §10 |
| 21 | camera shake / sequence | FUN_004A7A90 | | | not in corpus |
| 23 | screen sprite | mdlEffectCreateScreenSprite | | | not in corpus |
| 26 | model | EffModelCreate FUN_004ABE80 | FUN_004AC2C0 | FUN_004AC550 | §12 |
| 27 | helper (model children) | EffHelperCreate FUN_004AD460 | | | not in corpus |
| 28 | object particle (mesh particles) | ObjectParticleCreateFromSource FUN_004AE460 | FUN_004AEC80 | FUN_004AF520 | §7 |
| 29 | distortion particle | DistortParticleCreateFromSource | | | not in corpus |
| 30 | line nova | LineNovaCreateKindInstanceFromObject | | | not in corpus |
| 31 | battle-only effect | EffBattleCreate FUN_004B5200 | | | not in corpus |
| 32 | model drawable (P4 only) | effModelCreateDrawable FUN_004AC640 | | | not in corpus |

`eplprobe/names.py` + `eplprobe/elf.py` reproduce this table from the ELF.

P3F has a 13-slot (0x34-byte) table at P3F vaddr **0x0069BE20** with 32 rows (0–31):
slots 0–12 have the same meaning; P3F has no slots 13–15 and no kind 32. Null rows are the
same kinds as in P4. (P3F decomp `mdlEffect.c` builder indexes `PTR_LAB_0069be20[kind*13]`.)

### 4.1 Shared parameter building blocks

These sub-blocks recur at fixed places inside most parameter blocks.

**Colour keys** (0x24 bytes), sampled by `mdlEffectSamplePackedColor` (FUN_0048ABD0):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u8 | mode 0, 1, 2 (anything else: constant start colour) |
| 0x04 | u32 | start colour |
| 0x08 | u32 | end colour |
| 0x0C | u32 | middle colour 1 |
| 0x10 | f32 | middle fraction 1 (0..1 of duration) |
| 0x14 | u32 | middle colour 2 |
| 0x18 | f32 | middle fraction 2 |
| 0x1C | 8 bytes | unused |

**Opacity block** (0x10 bytes, immediately after the colour keys):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | alpha (0..255) |
| 0x04 | u16 | **blend mode / draw list** 0..6 (see below) — the field every kind reads as its "resource id"/"drawSlot" |
| 0x06 | u16 | 0 |
| 0x08 | f32 | fade-in fraction |
| 0x0C | f32 | fade-out fraction |

Sampling at frame `f` of duration `D` (D = 0 → `startRGB | alpha<<24`):

```
mode 0: lerp(start, end, f/D)
mode 1: f1 = int(frac1·D); f < f1 ? lerp(start, mid1, f/f1) : lerp(mid1, end, (f−f1)/(D−f1))
mode 2: f1 = int(frac1·D), f2 = int(frac2·D):
        f < f1 ? lerp(start, mid1, f/f1) : f < f2 ? lerp(mid1, mid2, (f−f1)/(f2−f1))
                                               : lerp(mid2, end, (f−f2)/(D−f2))
RGB = lerp result (the colours' own alpha bytes are ignored)
fi = int(fadeIn·D), fo = int(fadeOut·D)
env = f < fi ? f/fi : f > fo ? (D−f)/(D−fo) : 1
A = trunc(alpha · env)
```

**Scalar keys** (0x2C bytes), `mdlEffectSampleScalarKeys` (FUN_0048AFF0):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u8 | mode 0/1/2 as above (other: constant start) |
| 0x04 | f32 | start |
| 0x08 | f32 | end |
| 0x0C | f32 | per-instance variation (randomised scale of the sampled value; meaning per user) |
| 0x10 | u8 | extra byte (angle keys: `angleMode`; board scale keys: `loop`) |
| 0x14 | f32 | middle value 1 |
| 0x18 | f32 | middle fraction 1 |
| 0x1C | f32 | middle value 2 |
| 0x20 | f32 | middle fraction 2 |
| 0x24 | f32 | (scaled with the others by emitter scaling, never sampled) |
| 0x28 | 4 bytes | unused |

Same piecewise-linear formula as colours, D = 0 → start.

**Blend modes / draw lists.** Effect geometry is never drawn immediately; it is appended
to one of seven draw lists of the current owner table (`TnGetOffset1C(mode)`
FUN_00481460, remap `{0,1,4,6,2,3,5}` → physical list). The scene sets up each physical
list with a render-state callback (`mainDrawClearOtUpdate` FUN_00101BB0); all lists use
Z test on, Z write **off**, no culling, bilinear filtering, wrap addressing, vertex alpha
on, fog off. In draw order:

| Physical list | Blend modes using it | GS ALPHA | Blend equation | GS TEST |
|---|---|---|---|---|
| 0 | 0 | 0x44 | `Cs·As + Cd·(1−As)` (normal alpha) | 0x717FB: alpha test ≥ 0x7F, failing pixels still drawn (FB only) |
| 1 | 1 | 0x44 | normal alpha | 0x71801 (no alpha test effect) |
| 2 | 4 | 0x42 | `Cd − Cs·As` (subtractive) | 0x71801 |
| 3 | 5 | 0x06 | `Cs − Cd·As` | 0x71801 |
| 4 | 2 | 0x48 | `Cs·As + Cd` (additive) | 0x71801 |
| 5 | 6 | 0x06 | `Cs − Cd·As` | 0x71801 |
| 6 | 3 | 0x42 | subtractive | 0x71801 |

(PS2 alpha 0x80 = 1.0; values from `GeometryBlockSetPartState` FUN_004834E0, which sets
exactly these ALPHA/TEST pairs on PTank atomics, and from `mainDraw.c`.) Modes 0 and 1
look the same in practice; 3/4 and 5/6 differ only in draw order relative to additive.
Corpus blend modes: 2 (additive) dominates; 1, 3, 4, 6 also occur.

**Depth-test flag.** Geometry-based kinds have a per-primitive byte that sets bit 0 of
the geometry block flags; the list dispatcher (`TnDispatchObjectCallback` FUN_004815B0)
enables the Z test (RW render state 6) when bit 0 is set and **disables** it otherwise.
The decomp names this byte variously `visible`, `drawFlag`, `geometryFlag`, `hidden`;
its real meaning is "depth-test on".

**Geometry primitive.** Geometry kinds draw an RW atomic with pre-lit vertex colours,
`MODULATEMATERIALCOLOR`, optional texture (`GeometryCreate*` flags 0x48 untextured /
0x4C textured), placed by a frame matrix built from the instance's position (vec4 at
instance+0x00), rotation quat (+0x10) and vector scale (+0x20)
(`GeometryBlockSetFrameMatrixCombined` FUN_004836B0: `world = scale · R(q) + position`,
i.e. local vertex `v` → `(v ⊙ scale)·R(q) + position`). The material colour is the
kind's sampled colour × the node colour (§3.6).

---

## 5. The particle emitter core (shared by kinds 1, 28, 29, 2)

Kinds 1 (particle), 28 (object particle), 29 and 2 drive their particles with a common
emitter (`mdlEffectAllocateTrackPath` FUN_00492B20, advance `mdlEffectAdvanceEmitter`
FUN_00492D10). The record's `sub` selects the emitter model; the per-model table is at
**0x00713D50** (16 bytes per row: update fn, scale fn, per-particle state size,
parameter size):

| sub | model (decomp name) | update | state | params | in corpus |
|---|---|---|---|---|---|
| 1 | wave (straight motion + sine wobble) | EffWaveUpdate FUN_0048B9E0 | 0x3C | 0xF0 | yes (P-SMOKE, M-SMOKE) |
| 2 | motion | FUN_0048C4E0 | 0x1C | 0xE0 | |
| 3 | helix/orbit (spiral) | FUN_0048CDF0 | 0x40 | 0xF4 | yes (P-SPIRL) |
| 4 | | FUN_0048D8C0 | 0x34 | 0xE8 | |
| 5 | cone | FUN_0048E2F0 | 0x1C | 0xE8 | yes (P-HSPAK) |
| 6 | cone with angle span | FUN_0048EC50 | 0x1C | 0xEC | |
| 7 | bezier | FUN_0048F5F0 | 0x4C | 0x124 | |
| 8 | floor (bouncing cone) | FUN_004903C0 | 0xF8 | 0xF8 | yes (M-CSPAK) |
| 9 | | FUN_00490C40 | 0xE8 | 0xE8 | |
| 10 | swirl | FUN_004916F0 | 0x24 | 0xF8 | |
| 11 | sway | FUN_00492100 | 0x40 | 0x110 | |

P3F has the same sizes (P3F table right after its kind table, rows 34+ in
`eplprobe` dump). The corpus `paramSize` of every kind-1 record equals this table's
parameter size, and kind-28 records are 0x44 + it (§7).

### 5.1 Common emitter parameters (first 0xC8 bytes)

| Off | Type | Meaning | Scaled by node scale |
|---|---|---|---|
| 0x00 | vec4 | emitter position — **overwritten at runtime** by *set position* (file value unused, (0,0,0,1)/0) | |
| 0x10 | quat | emitter rotation — overwritten by *set rotation* | |
| 0x20 | s32 | `emitDuration` — frames during which new particles may be born; 0 = forever | |
| 0x24 | u32 | `rate` — particles born per frame (fractional via accumulator, see below) | |
| 0x28 | f32 | `rateVariation` | |
| 0x2C | colour keys (0x24) | particle colour over its life (frame = age, duration = lifetime) | |
| 0x50 | opacity block (0x10) | particle alpha over life; **0x54 = blend mode** | |
| 0x60 | scalar keys (0x2C) | particle size over life; +0x0C (0x6C) = size variation | start, end, mid1, mid2, +0x24 |
| 0x8C | scalar keys (0x2C) | particle 2D rotation angle (radians) over life; +0x0C (0x98) = angle variation; +0x10 (0x9C) = `angleMode` | |
| 0xB8 | s32 | `lifetime` in frames (0 disables the emitter) | |
| 0xBC | u8 | `local` — 1: particles live in emitter space and are transformed by the current emitter pose when drawn; 0: world space (spawned at the emitter's current position, then left behind) | |
| 0xBD | u8 | `preroll` — 1: on the first frame spawn a full set with random ages so the effect starts "warmed up" | |
| 0xBE | 2 bytes | | |
| 0xC0 | s32 | `trailCount` | |
| 0xC4 | s32 | `trailWidth` | |
| 0xC8… | | model-specific (§5.4) | |

Scaling (`set scale`, slot 0x28 of kind 1 → `EffEmitterApplyScale` FUN_00492E30 → table
column 1): the working copy of the parameters is rewritten from the pristine file copy
as `working = original × scale` for the size keys (start, end and the three middle
values at 0x74/0x7C/0x84) and for the model's distance-like fields listed per model.

### 5.2 Particle slots and lifecycle

* Primary capacity `N = min(300, (emitDuration ? emitDuration : lifetime) · rate)` for
  kind 1 (`ParticleCreateFromSource`), `min(100, …)` for kind 28.
* Each primary particle owns `trailCount · trailWidth` trail slots; total slots
  `N · (trailCount·trailWidth + 1)`, primaries first, then the trail slots of particle i
  at `N + i·trailCount·trailWidth`.
* Particle record (0x20 bytes, `SpawnParticle`): `vec4 position`, `s32 age`, `u32 colour`,
  `f32 size`, `f32 angle`. `age`: ≥ 0 alive, −1 free, −2 dead for good.
* `ticks` counts emitter frames. On the very first advance (`ticks == 0`) every slot is
  set to −1 before the update runs; *reset* (slot 0x1C) sets `ticks = 0`, so a restart
  wipes all particles.

**Per-frame update** (identical skeleton in all models; e.g. EffWaveUpdate):

```
if lifetime == 0: return
if emitDuration != 0 and ticks >= emitDuration:   budget = 0, preroll = false
elif ticks == 0 and preroll:
    budget = rateVariation > 0 ? int(N · (1.1 − rateVariation) · randFloat()) : N
    preroll = true
else:
    preroll = false
    acc += rateVariation > 0 ? rate · (1.1 − rateVariation) · randFloat() : rate
    budget = int(|acc|);  acc −= budget
for each primary slot p (in order):
    if p.age >= lifetime:
        p.age = emitDuration ? −2 : −1;  mark p's trail slots −1
    if p.age == −2: continue
    if p.age == −1:
        if budget == 0: continue
        spawn(p) (model specific, sets age = 0);  budget −= 1
        if preroll: (models 1,3,5) advance p analytically by a random age
                    a = rand() % lifetime, set p.age = a, then age += 1 after sampling
        sample keys at p.age; apply per-particle size/angle factors
        continue
    move(p) (model specific);  sample keys at age;  apply factors;  update trail;  age += 1
ticks += 1
```

(The 1.1 is the literal constant at gp−0x7F84. Note it makes the "variation" of the rate
behave oddly — `(1.1−v)·rand` — but that is what the code does.)

**Key application per particle** (`mdlEffectSampleKeyState` FUN_0048B220):
`colour = SampleColour(0x2C, 0x50, age, lifetime)`; `size = SampleScalar(0x60) · sizeFactor`;
`angle = SampleScalar(0x8C) · angleFactor + angleOffset`, except `angleMode == 2`:
`angle = atan2(Δscreen.y, Δscreen.x)` of the particle's projected screen motion since
the previous frame (0 if it did not move).

Per-particle random factors drawn at spawn:
`sizeFactor = (1−sizeVar) + sizeVar·rand`;
if angleMode ≠ 2: `angleFactor = (1−angleVar) + angleVar·rand`;
angleMode 1 additionally `angleOffset = 2π·rand` and a random sign flip of angleFactor
(`rand() & 1`); angleMode 0: `angleOffset = 0`; angleMode 2: factor 1, offset 0.

### 5.3 Trails [M]

If `trailCount·trailWidth = L > 0`, after each live update (`mdlEffectUpdateParticleTrail`
FUN_0048B340):

1. On the particle's first frame (age 0): copy the particle into trail slot 0 and mark
   all trail slots −1; done.
2. Otherwise: give trail slot 0 (the previous head copy) `age = age−1` (makes it
   visible); shift the trail history back by `trailWidth` slots (slot j ← slot
   j−trailWidth, from the tail toward the head), and set the alpha of each shifted slot k
   (counting k = 0 at the tail) to `A_head · k/L` (RGB kept) — a linear fade toward the
   tail; copy the current particle into slot 0 with age −1 (hidden, the primary is drawn
   instead).
3. If `trailWidth > 1` and `trailCount ≥ 2`, fill slots 1 … trailWidth−1 between the
   previous head (slot trailWidth, must have age ≥ 0) and the current position:
   t_k = k/(trailWidth+1); position = Catmull-Rom through (slot 2·trailWidth, slot
   trailWidth, current, current) when age ≥ 2 and trailCount ≥ 3, else linear;
   size/angle lerped; RGB lerped, alpha = previous head's alpha; age = current age.

Visible trail slots are drawn exactly like primaries (they are part of the same particle
array). With trailCount = 1 no trail slot ever becomes visible.

### 5.4 Emitter models present in the corpus

Below, `rot` = the emitter rotation (param 0x10, i.e. the node's world rotation) applied
as in §1.2, `E` = emitter position (param 0x00). In `local` mode, rotation and `E` are
**not** applied at spawn (they are applied when drawing instead).

**sub 1 — wave** (params 0xF0, state 0x3C):

| Off | Meaning | Scaled |
|---|---|---|
| 0xC8 | `spread` — half-width of the spawn line | yes |
| 0xCC | `speed` (units/frame along the main axis) | yes |
| 0xD0 | speed variation | |
| 0xD4 | `gravity` (deceleration along the main axis, units/frame²) | yes |
| 0xD8 | initial wobble amplitude | yes |
| 0xDC | its variation | |
| 0xE0 | final wobble amplitude | yes |
| 0xE4 | its variation | |
| 0xE8 | wobble phase step (radians/frame) | |
| 0xEC | its variation | |

Spawn: main axis `a = (0,1,0)·rot` (world mode) or (0,1,0) (local); wobble direction
`w = normalize(2r−1, 0, 2r−1)·rot` (no rot in local mode); `speed = speed·var`;
amplitude `A0 = amp0·var`, `A1 = amp1·var`, `ΔA = (A1−A0)/lifetime`;
`phaseStep = step·var`; `phase = 2π·rand`; `prevSin = sin(phase)`;
`position = w · (spread·(2r−1)) + E` (E omitted in local mode).
Preroll (if enabled): `t = rand() % lifetime`; `pos += a·(speed·t − ½·gravity·t²)`;
`phase += phaseStep·t; A += ΔA·t; pos += w·A·sin(phase)`; then one more step of phase/A.
Move each frame: `s = sin(phase)`; `pos += w·A·(s − prevSin)`;
`pos += a·(speed − gravity·age)`; `phase += phaseStep; A += ΔA; prevSin = s`.

**sub 3 — helix / spiral** (params 0xF4, state 0x40):

| Off | Meaning | Scaled |
|---|---|---|
| 0xC8 | `heightRange` — random start height in [0, heightRange) | yes |
| 0xCC | start radius | yes |
| 0xD0 | its variation | |
| 0xD4 | end radius | yes |
| 0xD8 | its variation | |
| 0xDC | angular speed (rad/frame) | |
| 0xE0 | its variation | |
| 0xE4 | rise speed (units/frame) | yes |
| 0xE8 | its variation | |
| 0xEC | angular acceleration (rad/frame²) | |
| 0xF0 | gravity (on the rise speed) | yes |

Spawn: `r = r0·var`, `Δr = (r1·var − r)/lifetime`, `phase = 2π·rand`, `ω = ω·var`,
`v = speed·var`, `h = heightRange·rand`; local position `(r·cos phase, h, r·sin phase)`;
world mode stores the current emitter position/rotation per particle (the helix stays where
it was born) and the particle position is `local·rot + E`.
Move: `θ = age·(ω + ½·angAccel·age) + phase`; `h += v − gravity·age`; `r += Δr`;
local = `(r·cosθ, h, r·sinθ)`; world mode: `pos = local·rot_spawn + E_spawn`.
Preroll: `t = rand()%lifetime`, `h += v·t − ½·gravity·t²`, `r += Δr·t`, position at phase.

**sub 5 — cone** (params 0xE8, state 0x1C):

| Off | Meaning | Scaled |
|---|---|---|
| 0xC8 | `spread` — spawn offset distance along the launch direction | yes |
| 0xCC | its variation | |
| 0xD0 | `cone` 0..1 (0 = straight up, 1 = horizontal) | |
| 0xD4 | its variation | |
| 0xD8 | `speed` (sign: negative launches inward/downward) | yes |
| 0xDC | its variation | |
| 0xE0 | acceleration along the launch direction | yes |
| 0xE4 | gravity (world −Y in world mode) | yes |

Spawn: `c = cone·var`; `d = normalize(c·(2r−1), 1−c, c·(2r−1))` (·rot in world mode);
`speed = |speed·var|`; `pos = (speed<0 ? −d : d) · (spread·var) + E`.
Move: `step = max(0, speed + accel·age)`; `pos += d·step; pos.y −= gravity·age`.
Preroll: `t = rand()%lifetime`; `dist = max(0, speed·t + ½·accel·t²)`;
`pos += d·dist; pos.y −= ½·gravity·t²`.

**sub 8 — floor / bounce** (params 0xF8, state 0x20; used by kind 28 in the corpus):

| Off | Meaning | Scaled |
|---|---|---|
| 0xC8 | spread | yes |
| 0xCC | its variation | |
| 0xD0 | cone | |
| 0xD4 | its variation | |
| 0xD8 | `floorHeight` (absolute y in particle space) | no |
| 0xDC | speed | yes |
| 0xE0 | its variation | |
| 0xE4 | acceleration | yes |
| 0xE8 | gravity | yes |
| 0xEC | bounce factor | |
| 0xF0 | its variation | |
| 0xF4 | bounce decay | |

Spawn: like the cone but the base direction points **down**
`(c·(2r−1), −(1−c), c·(2r−1))`; `bounce = −bounce·var`; no preroll.
Move: `step = max(0, speed + accel·age)`; `d.y −= gravity` (persistent);
`pos.x += d.x·step; pos.y += d.y; pos.z += d.z·step`; if `pos.y < floor`:
`bounce *= decay; d.y *= bounce; pos.y = floor + bounce·(pos.y − floor)`.

Other models (2, 4, 6, 7, 9, 10, 11) are not used by any corpus RMD; their parameter
structs are in `include/mdl_effect_emitter_internal.h` (`MdlEffectMotionParameters`,
`ConeParameters` incl. `angleSpan`, `BezierParameters`, `SwirlParameters`) [L for
semantics].

---

## 6. Kind 1 — particle (PTank billboards)

`ParticleCreateFromSource` (FUN_00486B00); instance 0x60 bytes. Parameter block = §5
emitter parameters for `sub` (paramSize = table size). Secondary resource decides the
look (`instance.kind`):

| srcKind | Behaviour |
|---|---|
| 1 | textured billboards; texture = embedded TMX (§3.4) |
| 4 | textured billboards; texture = global slot (u32) |
| 2 | billboards with per-particle animated UVs from a track resource (not in corpus) |
| 5 | each particle drives a copy of a nested primitive (not in corpus) |
| 6 | each particle drives a copy of a nested EPL tree (not in corpus) |
| 7 | TLP-driven sprites (not in corpus) |
| none | nothing is drawn |

Corpus (kind 1 only): srcKind 4 (18 nodes), 1 (3 nodes), shared secondary (1 node).

**Rendering (srcKind 1/4)** — `ParticleProjectVertices` (FUN_00487FB0) at draw time,
when `ticks > 0`:

* Geometry is an RW **PTank** atomic with flags 0x20080027
  (`GeometryCreateAtomicFromResource` FUN_004830F0; position + colour + 2D size +
  2D rotation per particle, constant UVs (0,0)–(1,1), structure layout). Each live slot
  (primary or visible trail, `age ≥ 0`) becomes one screen-aligned (camera-facing) quad.
* Position: the particle position (world mode), or `position·rot + E` with the current
  emitter pose (local mode).
* Size: `(texScaleX·size, texScaleY·size)` with `texScaleX = textureWidth/32`,
  `texScaleY = textureHeight/32` (pixels of the TMX), i.e. a 64×64 texture at size 1 is a
  2×2-unit quad. The node scale reaches the size through the size keys (§5.1).
* Rotation: `angle` wrapped into (−π, π].
* Colour: per-particle `colour` (RGB from keys, A from opacity envelope); the PTank
  material colour is the node colour (`instance.packedColor`, set by slot 0x30).
* Blend/draw list: emitter parameter 0x54 (blend mode), applied both as PTank render
  state and as draw list (`GeometryBlockSetPartState` FUN_004834E0,
  `GeometryBlockBindResource` FUN_00483490).

Instance handlers: advance (slot 2, FUN_00489E00) = advance emitter (+ child updates for
srcKind 5/6); draw (slot 3, FUN_00489E50) = `ParticleProjectVertices(·, 1.0)`;
set position/rotation write the emitter params 0x00/0x10 (FUN_00489EE0/FUN_00489F10);
set scale (FUN_00489F50) rescales the emitter; reset (FUN_00487C00) = `ticks = 0`;
clone (`ParticleCreateFromParent` FUN_00486FB0) copies parameters and shares the texture.

---

## 7. Kind 28 — object particle (mesh particles)

`ObjectParticleCreateFromSource` (FUN_004AE460); instance 0x94 bytes. Parameter block =
**0x44-byte header + emitter parameters** for `sub` (corpus: `M-SMOKE` sub 1, 0x134;
`M-CSPAK` sub 8, 0x13C). The emitter is the same as §5 except capacity is capped at
**100** and trails are forbidden (assert `trailCount == 0`). Secondary resource must be
srcKind 3 (embedded RW stream; the first texture dictionary and first clump are loaded,
`ObjectParticleLoadClump` FUN_004AEA70).

Header (offsets from the start of the parameter block; the header is copied to
instance+0x10):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u8 | `randomAxis` — 1: each particle gets a random spin axis; 0: use the fixed axis below |
| 0x04 | f32[3] | fixed spin axis |
| 0x10 | u32 | unused |
| 0x14 | f32 | spin speed (rad/frame) |
| 0x18 | f32 | spin speed variation |
| 0x1C | f32 | spin acceleration (rad/frame²) |
| 0x20 | u16 | `sizeNormalise` — non-zero: divide the size by the instance scale |
| 0x22 | u8 (+pad) | `depthTest` — bit 0 set: Z test on while drawing; clear: Z test off (instance byte +0x32, read by `TnDispatchSkyDraw`). Corpus: M-SMOKE 1, M-CSPAK 0 |
| 0x24 | s32 | `paletteMode` — 1: random palette start index; 2: index = particle slot % 6; other: no palette |
| 0x28 | s32 | palette cycle duration (frames for all six colours) |
| 0x2C | u32[6] | palette colours |
| 0x44 | … | emitter parameters (§5.1) |

Per-particle record (re-seeded whenever an emitter particle is born,
`mdlEffectInitDirectionEntry` FUN_004AE0A0): axis (fixed or `normalize(2r−1,2r−1,2r−1)`,
zero → (0,1,0)), `ω = speed·((1−var)+var·r)`, start angle `2π·r`, palette index.

Draw (`ObjectParticleDraw` FUN_004AED70, via the draw list callback `TnDispatchSkyDraw`
FUN_00481740) — for each live emitter particle:

* colour = particle colour × instance colour × (palette colour if a palette mode is set:
  `seg = duration/6`, `i = (start + age/seg) % 6`, lerp(palette[i], palette[i+1 mod 6],
  (age % seg)/seg)) → all materials of the clump (`mdlEffectSetAllColors`);
* angle `f = ω·age + ½·spinAccel·age²`; if `f < 0` the rotation is identity, else rotation
  about `axis` by `(f + startAngle)` radians (RW takes degrees; ×57.29578);
* scale = `size` (or `size/instanceScale` with `sizeNormalise`), uniform;
* world mode: matrix = rotation · emitterRotation, then scale, then translate to the
  particle position; local mode: the particle position is first transformed by the
  current emitter pose;
* render the clump with that frame matrix.

**Draw states** [H for P4; P3F has the same state values in its inlined equivalent, M].
The instance is appended to the draw list of the emitter blend mode `B` (u16 at parameter
offset 0x44+0x54 = 0x98; `TnGetOffset1C(B)`, §4.1). The list's start callback has
already set that list's GS ALPHA/TEST pair and the common states (Z test on, Z write
off, no culling, fog off, §4.1). The per-instance node callback (`TnDispatchSkyDraw`
FUN_00481740) then does, in this order:

1. **Z test** (RW render state 6, `ZTESTENABLE`) = bit 0 of header byte 0x22.
2. **Blend mode 0 only** (the test reads the `B` word of the emitter parameter block;
   the decomp calls it `skyState`, but no texture is involved. There is no "sky
   texture" classification): Z write on (RW state 8 = 1), and GS TEST = **0x70003**.
   That value means alpha test ATST = ALWAYS (no alpha rejection, unlike the list's
   0x717FB) and the depth test enabled with the normal RW depth comparison. For any other
   blend mode the list's TEST and the Z write off are kept.
3. **Culling**: RW render state 0x14 (`CULLMODE`) = 2 = `rwCULLMODECULLBACK`. A
   triangle is back-facing, and therefore not drawn, when its geometric normal
   `(v1−v0)×(v2−v0)`, in RW triangle vertex order, points away from the camera. This is
   the same convention RMD models use with their default cull mode (model draw state
   below, §12). The state value is [H]. The winding wording is [M]: it assumes the RW
   PS2 back-face test agrees with the stored geometry normals.
4. `ObjectParticleDraw` renders all particles (atomic render, material colours as above,
   geometry flag `MODULATEMATERIALCOLOR` set on every atomic at load).
5. Restore: cull mode 1 (`rwCULLMODECULLNONE`). For blend mode 0, Z write off and
   TEST 0x717FB. If the Z test had been turned off, turn it back on.

**Blend**: unchanged from the list (GS ALPHA of physical list `remap[B]`, §4.1).
Material alpha × texel alpha is the source alpha. Corpus: M-SMOKE `B = 1` (normal alpha,
Z test on), M-CSPAK `B = 0` (normal alpha, Z write on, no alpha test, Z test off).

---

## 8. Kind 12 — ring

`RingCreateKindInstanceFromObject` (FUN_0049A4E0) → `RingCreateKindInstance`
(FUN_0049A370). Rings **ignore textures** and the secondary resource. Sub-type table at
**0x00713F10**, 6 words per row (init, alloc, free, advance, draw, paramSize):
sub 1 circle (0x6C), sub 2 random strip (0xD0), sub 3 filled/scaled circle (0x68).
Instance: position (0x00), rotation (0x10), vector scale (0x20), colour (0x30), age
(0x34), sub (0x38), work (0x3C), parameters (0x40, copy at instance+0x50).

Parameters (common prefix 0x5C):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | colour keys | ring colour over `duration` |
| 0x24 | opacity block | alpha/fade; **0x28 = blend mode** |
| 0x34 | s32 | `duration` (frames; 0 = forever) |
| 0x38 | u32 | `segments` (minimum 3) |
| 0x3C | u8 | depth-test flag |
| 0x40 | f32 | base radius |
| 0x44 | u32 | inner colour (vertex colour of ring row 0) |
| 0x48 | u32 | middle colour (rows 1 and 2) |
| 0x4C | u32 | outer colour (row 3) |
| 0x50 | f32 | inner width |
| 0x54 | f32 | middle width |
| 0x58 | f32 | outer width |

sub 1 tail: 0x5C unused, 0x60 `speed`, 0x64 `accel`, 0x68 u8 `reverse`.
sub 3 tail: 0x5C `speed`, 0x60 `accel`, 0x64 u8 `reverse`.

Geometry: a strip grid with `segments+1` rows of 4 vertices (index pattern table
0x00713330: for consecutive rows a, b = a+4: triangles (a0,a1,b1),(a0,b1,b0),(a1,a2,b2),
(a1,b2,b1),(a2,a3,b3),(a2,b3,b2)). Row k has direction `(cos θk, 0, sin θk)`,
`θk = 2πk/segments`; all vertices lie in the local XZ plane.

Per frame (advance = sub's rebuild, then age += 1; draw only when age > 0), while
`age ≤ duration` or `duration == 0`:

```
t = reverse ? duration − age : age
v = |speed|;  if accel < 0: t = min(t, −v/accel)     (stop when the ring stops growing)
R = radius + t·(v + ½·accel·t)
sub 1: row radii = (R, R+inner, R+inner+middle, R+inner+middle+outer)
sub 3: row radii = (0, R+inner, R+inner+middle, R+inner+middle+outer)   (filled disc)
vertex colours per row: inner, middle, middle, outer (constant)
material colour = SampleColour(0x00, 0x24, age, duration) × node colour
```

The draw (`RingUpdateRowColour` FUN_004992A0) is skipped when the material alpha is 0.
Corpus: R-NOMAL (sub 1, P3F, blend 3), R-CRCLE (sub 3, P4, blend 2, node flag 0x20 =
camera-facing disc).

---

## 9. Kind 10 — flash quads

`FlashQuadCopyAllocFromParent` (FUN_004A1780). Sub-type table at **0x00714010**, 7 words
per row (init, alloc, free, allocFromParent, advance, draw, paramSize):

| sub | size | geometry | default texture (srcKind 4 / none) | corpus |
|---|---|---|---|---|
| 1 | 0x80 | rotating rays: 5 verts / 3 tris per ray, untextured | none (alloc asserts no texture) | S-RADI (P4) |
| 2 | 0x8C | radial rays | | |
| 3 | 0x9C | radial rays (wide) | | |
| 4 | 0x80 | growing radial rays, textured | slot 0x12 (be_0019) | S-RAD_T (P3F, P4) |
| 5 | 0x88 | streaks/sparks, textured | slot 0x13 (be_0020) | S-SPK_T (P3F) |
| 6 | 0xA4 | ring of quads ("crown"), textured | slot 0x12 | S-RNG_T (P4, embedded TMX) |
| 7 | 0xA8 | boxes | | |

srcKind 1 (embedded TMX) replaces the default texture; srcKind 4 **ignores** the stored
slot number and uses the default above.

Common parameter prefix (`FlashParameters`):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | colour keys | global colour over `duration` |
| 0x24 | opacity block | **0x28 = blend mode** |
| 0x34 | u32 | `duration` (frames, 0 = forever); nothing is updated or drawn once `frame > duration` |
| 0x38 | u32 | `count` — number of quads (particles) |
| 0x3C | 8 bytes | unused |
| 0x44 | f32 | per-quad fade-in fraction of `life` |
| 0x48 | f32 | per-quad fade-out fraction |
| 0x4C | s32 | `life` (frames per quad; 0 disables) |
| 0x50 | s32 | `births` per frame |
| 0x54 | u8 | `recycle` — dead quads become free again (else stay dead) |
| 0x55 | u8 | `burst` — on frame 0 spawn all `count` quads with random ages |
| 0x56 | u8 | depth-test flag |
| 0x58 | u32 | inner colour (vertices at the base) |
| 0x5C | u32 | outer colour (vertices at the tip) |
| 0x60… | | sub-specific |

Common per-frame logic (advance; then age/frame += 1; draw only when frame > 0):
quad age −1 = free, −2 = dead; free quads are born while `births` remain this frame (all
`count` on frame 0 with `burst`, with age `rand() % life`); a quad whose age reaches
`life` has its vertices zeroed and becomes −1 (`recycle`) or −2. Per-quad fade
`env = age < fi ? age/fi : age > fo ? (life−age)/(life−fo) : 1` with `fi = int(fadeIn·life)`,
`fo = int(fadeOut·life)`. Draw: material colour = `SampleColour(0x00, 0x24, frame,
duration) × node colour`; frame matrix from the instance pose; append to draw list
`blend`. All quads lie in the local XZ plane (use node flag 0x20 to face the camera).

**sub 1 — rotating rays.** Tail: 0x60 width, 0x64 length, 0x68 size variation (applied
to both), 0x6C radius, 0x70 radius variation, 0x74 angular speed, 0x78 its variation,
0x7C angular acceleration. Birth: `φ0 = 2π·r`; `ω = ω·var` with random sign;
`radius = radius·var`; `width,length = (width,length)·var` (same random factor).
Per frame: `φ = φ0 + age·(ω ± ½·accel·age)` (sign of ω); `d = (cos φ, 0, sin φ)`,
`p = (sin φ, 0, −cos φ)`; vertices: v0 = `d·radius + p·length`, v1 = `d·radius − p·length`,
v2 = `p·width`, v3 = origin, v4 = `−p·width`; triangles (0,3,2),(0,1,3),(1,4,3)
(table 0x007133A0). Colours: v0,v1 = outer RGB × env (alpha 0); v2,v4 = black with
alpha `255·env` (even quads) or `128·env` (odd quads); v3 = inner RGB × env with alpha
0x40 (even) or 0xFF (odd).

**sub 4 — growing textured rays.** Tail: 0x60 width, 0x64 its variation, 0x68 length,
0x6C its variation, 0x70 s32 `growFrames` (0 → 1), 0x74 angular speed, 0x78 its
variation, 0x7C angular acceleration. Birth: `φ0 = 2π·r`, `ω` random sign,
`len = 0`, `Δlen = length·var / growFrames`, `w = width·var`; burst births pre-grow by
`age % growFrames` steps. Per frame: `φ = φ0 + age·(ω ± ½·accel·age)`; while
`age < growFrames`: `len += Δlen`; `d, p` as above; v0 = `d·len + p·w`, v1 = `d·len − p·w`,
v2 = `p·w`, v3 = `−p·w`; triangles (0,1,3),(0,3,2) (table 0x00713408); colours v0,v1 =
outer × env, v2,v3 = inner × env (all four channels × env). UVs fixed per quad: odd
quads use the left half of the texture (v0 (0,0), v1 (0.5,0), v2 (0,1), v3 (0.5,1)),
even quads the right half ((0.5,0),(1,0),(0.5,1),(1,1)).

**sub 5 — streaks.** Tail: 0x60 width, 0x64 var, 0x68 half length, 0x6C var, 0x70 start
distance, 0x74 var, 0x78 speed, 0x7C var, 0x80 acceleration, 0x84 u8 `reverse`.
Birth: fixed direction `φ = 2π·r`; values with variation. With `reverse`, age runs from
`life` down to 0 (fade fractions mirrored: fi = (1−fadeOut)·life, fo = (1−fadeIn)·life).
Per frame: `t = age`, if accel < 0: `t = min(t, −v/accel)`;
`dist = t·(v + ½·accel·t) + startDistance`; `c = d·dist`, `l = d·halfLen`, `w = p·width`;
v0 = `c + l + w`, v1 = `c + l − w`, v2 = `c − l + w`, v3 = `c − l − w`; colours v0,v1
outer×env, v2,v3 inner×env; UVs as sub 4.

**sub 6 — crown of quads.** Tail: 0x60 width, 0x64 var, 0x68 length start, 0x6C var,
0x70 length end, 0x74 var, 0x78 radius start, 0x7C var, 0x80 radius end, 0x84 var,
0x88 tilt start, 0x8C var, 0x90 tilt end, 0x94 var, 0x98 angular speed, 0x9C var,
0xA0 angular acceleration. Birth: `φ0 = 2π·r`, each start/end pair randomised and turned
into `value, Δ = (end−start)/life` (burst: pre-advanced by `rand()%life`).
Per frame: `φ = φ0 + age·(ω + ½·accel·age)`; `base = (cos φ, 0, sin φ)·radius`;
`up = normalize(cos φ·tilt, 1−tilt, sin φ·tilt)`; `w = (sin φ, 0, −cos φ)·width`;
v2 = base + w, v3 = base − w, v0 = base + up·len + w, v1 = base + up·len − w;
then `len, radius, tilt += Δ`; colours v0,v1 outer×env, v2,v3 inner×env; UVs as sub 4.

---

## 10. Kind 16 — wind strips

`WindCreateKindEntry` (FUN_004A5750). Sub-type table at **0x00714130** (7 words: init,
alloc, free, allocFromParent, advance, draw, paramSize): sub 1 funnel (0xF8, corpus
`N-SPIRL`), sub 2 falling funnel (0xF4), sub 3 spiral (0xD8). Instance prefix as for rings
(position/rotation/vector scale/colour/age/sub/work/params).

Texture: srcKind 1 → embedded TMX; srcKind 4 → the stored slot is **ignored** and global
slot **0x14** (`be_0021.tmx`) is used (`FunnelWindAllocBlock` FUN_004A20B0).

Funnel parameters (sub 1, 0xF8):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | colour keys | global colour over `duration` |
| 0x24 | opacity block | **0x28 = blend mode** |
| 0x34 | u32 | `duration` (0 = forever; nothing happens once `elapsed > duration`) |
| 0x38 | u32 | `count` — number of strips |
| 0x3C | colour keys | per-strip colour over the strip's life |
| 0x60 | opacity block | per-strip alpha/fade over its life (blend field unused) |
| 0x70 | 8 bytes | unused |
| 0x78 | f32 | fade-in fraction **along the strip length** |
| 0x7C | f32 | fade-out fraction along the length |
| 0x80 | u32 | strip `lifetime` (frames; 0 disables) |
| 0x84 | u32 | strips born per frame |
| 0x88 | u8 | `respawn` — dead strips become free again |
| 0x8C | u32 | `segments` along the strip (minimum 3) |
| 0x90 | f32 | U extent across the strip (U = 0, ⅓, ⅔, 1 × this) |
| 0x94 | | unused |
| 0x98 | f32 | V span along the strip, also the base of the random start V |
| 0x9C | f32 | start-V variation |
| 0xA0 | | unused |
| 0xA4 | f32 | V scroll per frame |
| 0xA8 | f32 | twist — total angle swept along the strip |
| 0xAC | f32 | its variation |
| 0xB0 | f32 | strip width |
| 0xB4 | f32 | its variation |
| 0xB8 | u8 | `spawnAll` — on frame 0 spawn all strips with random ages |
| 0xB9 | u8 | depth-test flag |
| 0xBC | f32 | radius start |
| 0xC0 | f32 | its variation |
| 0xC4 | f32 | radius end |
| 0xC8 | f32 | its variation |
| 0xCC | f32 | height start |
| 0xD0 | f32 | its variation |
| 0xD4 | f32 | height end |
| 0xD8 | f32 | its variation |
| 0xDC | | unused |
| 0xE0 | f32 | swell variation |
| 0xE4 | f32 | swell centre (0..1 along the strip) |
| 0xE8 | f32 | swell (extra radius ∝ (s − centre)²) |
| 0xEC | f32 | angular speed (rad/frame) |
| 0xF0 | f32 | its variation |
| 0xF4 | f32 | angular acceleration |

Geometry (`GeometryCreateStripGridMultiPart`): one part per strip, `segments+1` rows of
4 vertices, same row index pattern as rings, textured, pre-lit. Vertex colours are
static (`mdlEffectBuildWindFadeRows` FUN_004A1D70): along row i (i = 0..segments),
`a_i = i < fi ? i/fi : i > fo ? (segments−i)/(segments−fo) : 1` with
`fi = int(fadeIn·segments)`, `fo = int(fadeOut·segments)`; the four vertices of a row are
`(white, α 0)`, `(white, α 255·a_i)`, `(white, α 255·a_i)`, `(white, α 0)` — feathered
edges across, fade along. U across = 0, w/3, 2w/3, w with `w = param 0x90`.

Strip birth (`FunnelWindUpdateStrip` FUN_004A2310): `r = rStart·var`,
`Δr = (rEnd·var − r)/lifetime`, `h = hStart·var`, `Δh = (hEnd·var − h)/lifetime`,
`swell = swell·var`, `φ0 = 2π·rand`, `ω = ω·var`, `width = width·var`,
`twist = twist·var`, `v0 = vBase·var`; `spawnAll` births get `age = rand()%lifetime`
and pre-advance `r, h`.

Per frame for a live strip (age < lifetime):

```
θ = φ0 + age·(ω + ½·angAccel·age);  r += Δr;  h += Δh
n = segments + 1;  rise = h/n;  step = sign(ω)·twist/n  (step = twist/n if ω ≥ 0, −twist/n if ω < 0)
y = 0
for s in 0..segments:
    c = s/n − centre;  e = swell·c²;  R = r + e
    P = (cos θ·R, y, sin θ·R)
    N = normalize(cos θ·e, rise, sin θ·e) · (width/4)
    row vertices: P+2N, P+N, P−N, P−2N
    V = v0 + vSpan·(s/n)  (same for the 4 vertices)
    θ += step;  y += rise
v0 += vScroll;  age += 1
```

A strip reaching its lifetime has its vertices zeroed and becomes free (`respawn`) or
dead. Draw (`FunnelWindPackColors` FUN_004A2C90): `g = SampleColour(0x00, 0x24,
elapsed, duration) × node colour`; per live strip
`material colour = SampleColour(0x3C, 0x60, age, lifetime) × g`; frame matrix from the
instance pose; draw list = blend (0x28).

Corpus `N-SPIRL` (FP00A, P3F): one strip, additive, radius 300→295, height 35→140,
swell −118, 5 segments, node scale 0.06 — a short twisting flame wisp.

---

## 11. Kind 7 — board (T-BOARD)

`mdlEffectCreateInstance` (FUN_004A5BB0); instance 0xD4 bytes, one textured quad
(`GeometryCreateTableGrid(1, 2, 4, …)`), the 0x98-byte parameter block copied to
instance+0x30.

| Off | Type | Meaning |
|---|---|---|
| 0x00 | colour keys | colour over `duration` |
| 0x24 | opacity block | **0x28 = blend mode** |
| 0x34 | scalar keys | size over `duration`; byte 0x44 (= keys+0x10) = `loop` |
| 0x60 | scalar keys | spin angle (radians) over `duration` |
| 0x8C | s32 | `duration` |
| 0x90 | f32 | size multiplier |
| 0x94 | u8 | depth-test flag |

Texture: srcKind 1 embedded TMX (corpus), 4 global slot, 2 animated track (UV/texture
per frame), 7 TLP. Advance: `age += 1` (FUN_004A5FA0). Draw (`EffSpriteUpdate`
FUN_004A5FC0), skipped while `age == 0`; when `age ≥ duration > 0`: restart at 0 if `loop`,
else stop drawing. Half extents `hx = texW/32 · size`, `hy = texH/32 · size` with
`size = SampleScalar(0x34)·mult`; corners `(±hx, ±hy)` rotated by
`spin = SampleScalar(0x60)` in the local XZ plane (x → x, y → z; y = 0); UV (0,0)–(1,1)
mapped to the corners (track/TLP variants supply sub-rectangles); colour =
`SampleColour × node colour`; frame matrix = instance position + rotation with uniform
instance scale (`GeometryBlockSetFrameMatrix`); draw list = blend.

---

## 12. Kind 26 — model

`EffModelCreate` (FUN_004ABE80). The secondary resource must be srcKind 3: an embedded
mini-RMD (texture dictionary + clump + optional animation), loaded as a full model
object of type 6 with a fresh id (`mdlCreateModelObjectFromMemory`), animation slot 0 of
layer 0 started, layer weight 1. Parameter block 0x68 copied to instance+0x2C:

| Off | Type | Meaning |
|---|---|---|
| 0x00 | colour keys | tint over `duration` |
| 0x24 | opacity block | **0x28 = draw list** |
| 0x34 | scalar keys | scale over `duration`, divided by 10 |
| 0x60 | u32 | `duration` (special value 1225 = 0x4C9 switches the model to alpha blending) |
| 0x64 | u8 | sets model flags 0x18 when non-zero (cleared otherwise) |

Advance (`EffModelRollCycle` FUN_004AC2C0): while `elapsed ≤ duration` or
`duration == 0`: animation speed for this frame = 1, `elapsed += 1`. Draw
(`EffModelUpdateByOffset` FUN_004AC550 → `EffModelUpdate` FUN_004AC300): model colour =
`SampleColour × node colour`; model matrix = `R(instance rotation)`, uniformly scaled by
`instance scale · SampleScalar(0x34)/10`, translated to the instance position; set layer
speed (then reset it to 0 so the animation only advances on frames the effect advanced);
queue the model for drawing into the selected list. Reset rewinds the animation (layer
time 0). Corpus: P4 `N23911_0` `MODEL` (duration 0, 0x64 = 1, alpha 0x37, list 2).

**Colour** [H]. Each drawn frame (`elapsed ≤ duration` or `duration == 0`):
`c = SampleColour(0x00, 0x24; elapsed, duration)`. The opacity envelope goes into `c.a`.
Then `model.colour[k] = trunc(c[k]/255 · node[k]/255 · 255)` for k = R, G, B, A
(truncating, no rounding). Here `node` is the node colour, default 0xFFFFFFFF. The model
colour is not baked once. The model's draw-state callback (`mdlApplyDrawState`
FUN_00478EC0) runs right before the model's command executes in the list. It rewrites
**every material of every atomic** as follows:

```
orig = material user data "per3modelMatColor" (u32, bytes B,G,R,A from low to high;
       created from the material colour at model preparation if absent)
mat.rgb = round(orig.rgb/255 · model.rgb/255 · 255)       round = trunc(x + 0.5)
mat.a   = round(orig.a/255  · model.a/255  · 255)
```

The geometry gets the `MODULATEMATERIALCOLOR` flag (0x40). The vertex/texture colour is
therefore multiplied by this material colour, alpha included. A model whose
`model.colour.a == 0` is not queued at all (`mdlQueueModelForDraw` returns early).

**Render states of the queued model** [H unless marked]. `mdlQueueModelForDraw` appends
one command node to the selected effect list. The list index is the opacity-block word
0x28 (`TnGetOffset1C`). The list only decides the **draw order**. The model's own
start callback overrides the list's blend and test. The start callback sets:

| RW state | Value |
|---|---|
| 6 `ZTESTENABLE` | model flag 0x08 |
| 8 `ZWRITEENABLE` | model flag 0x10 |
| 0xE `FOGENABLE` | model flag 0x100 (set by default) |
| 0x14 `CULLMODE` | 3 (`CULLFRONT`) if model flag 0x40, else 2 (`CULLBACK`); effect models never set 0x40, so back faces are culled (same convention as §7) |

GS ALPHA = 0x44 (`Cs·As + Cd·(1−As)`) and GS TEST = 0x717FB. TEST 0x717FB means: alpha
test `A ≥ 0x7F` on the PS2 scale (0x80 = 1.0, so roughly ≥ 254/255). Pixels that fail
are still drawn with blending, but **without a Z write** (AFAIL = FB_ONLY). The ALPHA
and TEST values come from the model's `skyAlpha`/`skyTest` defaults (clump path), or
from the bucket renderer's fixed 0x44/0x717FB (material-bucket path). The effect list's
own blend mode (additive etc.) is therefore **not** applied to kind 26. [M for "never
the list blend": the list callback runs first and the model callback overrides it]

**Parameter 0x64 (model flags 0x18)** [H]. Newly created models have flags 0x10118
(Z test and Z write on). Each update sets flags |= 0x18 when the byte is non-zero.
Otherwise it clears them. Hence:

* `0x64 ≠ 0`: Z test **on**, Z write **on** for pixels that pass the alpha test. In the
  material-bucket path the bucket renderer also forces Z test/write on explicitly
  (`(flags & 0x18) == 0x18` clears the node's "keep caller Z state" bit).
* `0x64 = 0`: Z test **off** and Z write **off**. The model draws over everything
  already in the frame buffer, in list order, and occludes nothing.

**`duration == 1225` (0x4C9)** [H for the flag; M for the pass details]. On every drawn
frame, `mdlSetAlphaBlend` sets model flag 0x20. Nothing in the effect code clears it.
The duration still counts normally (1225 frames). Flag 0x20 changes the queued
command only while `model.colour.a ≠ 255`. In that case the model is drawn by the
translucent path (command 0x1B, `movieNativeSkyDraw`) instead of the opaque one:

1. **Depth pre-pass** (`mdlDrawSkyPass`): GS ALPHA 0x64 = `(Cs−Cd)·FIX + Cd` with FIX = 0,
   so the colour buffer is left unchanged. GS TEST = 0x7000F: alpha test `A ≠ 0`, failing
   pixels KEEP (nothing written). Z test on and fog off; Z write follows model flag 0x10,
   i.e. parameter 0x64. Effect: depth is laid down for every non-transparent texel.
2. **Colour pass**: GS ALPHA = `skyAlpha` (0x44), TEST = `skyTest` (0x717FB), Z test on,
   **Z write off**. The model blends over the scene with its material alpha. Because of
   the pre-pass, only its nearest surface is visible (no self-overdraw).

Without 1225 (or with alpha 255) the model is drawn in one pass with the states in the
table above. Translucency then still comes from material alpha × ALPHA 0x44, but
back-to-front self-overlap is not resolved and alpha-test-passing pixels write Z.

---

## 13. Kinds 4 and 5 — screen-space blur / filter

The sample has only four nodes of these kinds, all P3F:

* `M005_001` `B-FULL` (kind 4 sub 1);
* `M005_001` `F-FULL` (kind 5 sub 1);
* `M039_303` two `F-MULTI` (kind 5 sub 2).

P4 has the identical code (`effBlurFilter.c`). In the P3F decomp it is in
`mdlEffect.c`: kind 4 sub 1 update/draw FUN_00349870 / FUN_00349A30, kind 5 sub 1 draw
FUN_0034ADA0, kind 5 sub 2 update/draw FUN_0034AE30 / FUN_0034B010. The constants, offsets
and states are the same. The only difference: the P3F kind 4 sub 1 and kind 5 sub 1 draws
do not restore TEST and the texture after drawing. This has no visible effect, because
every later draw sets its own. [H]

Type tables (6 words per sub: init, destroy, update, draw, usesTexture, dataSize): kind 4
at **0x00714390**, kind 5 at **0x00714460** (P4). Corpus rows:

| kind/sub | update | draw | usesTexture | size |
|---|---|---|---|---|
| 4/1 | `effBlurFilterUpdateScreenRect` FUN_004A93D0 | `effBlurFilterDrawSlotPass` FUN_004A9590 | 0 | 0xE8 |
| 5/1 | `effBlurFilterUpdateScreenRectFlagged` FUN_004AA960 | `effBlurFilterDrawAux` FUN_004AAA90 | 0 | 0xD8 |
| 5/2 | `effBlurFilterUpdateRectFromScaleAux` FUN_004AAB50 | `effBlurFilterDrawPassB` FUN_004AAD30 | 1 | 0xE4 |

(Kind 4 subs 2–5 are 0xEC/0xEC/0xEC/0xE8 and are not in the corpus.) None of the three
corpus subs has an init/destroy function.

### 13.1 Instance and parameter block [H]

`effBlurFilterCopiedCreate` (kind 4) / `effBlurFilterDirectCreate` (kind 5) allocate
`0x60 + size` bytes and zero them. The parameter block is copied to instance+0x60, and
**the copy is written to at run time**: bytes 0xC0.. of the block are a scratch area
for the screen quad. A clone therefore gets a fresh copy of the record's block. Instance
header fields used here:

| Off | Meaning | Initial value |
|---|---|---|
| 0x00 | position vec4 (set-position slot; world space) | (0,0,0,1) |
| 0x10 | node colour, packed RGBA (set-colour slot) | 0xFFFFFFFF |
| 0x14 | scalar scale (set-scale slot) | 1.0 |
| 0x18 | sub | |
| 0x1C | `elapsed` | 0 |
| 0x24 | pointer to the parameter copy | |
| 0x28 | texture reference (kind 5 sub 2 only) | |
| 0x2C | draw-list node | |

Texture reference (usesTexture = 1 only): secondary srcKind 1 is an embedded TMX
uploaded as a texture. srcKind 4 uses the stored u16 as a **global texture slot index,
used as is** (unlike kinds 10/16, which ignore the stored slot). Any other srcKind
asserts. Both F-MULTI nodes are srcKind 4, slot 0 (`be_0000.tmx`).

Shared parameter layout (offsets in the block):

| Off | Type | 4/1 B-FULL | 5/1 F-FULL | 5/2 F-MULTI |
|---|---|---|---|---|
| 0x00 | colour keys (§4.1) | tint | fill colour | quad colour |
| 0x24 | opacity block (§4.1): 0x24 alpha, **0x28 u16 list**, 0x2C fade-in, 0x30 fade-out | yes | yes | yes |
| 0x34 | scalar keys (0x2C) | **zoom** `z` | unused | unused |
| 0x60 | scalar keys | **rotation** `r` | unused | unused |
| 0x8C | scalar keys | unused | unused | **radius** `ρ` |
| 0xB8 | s32 | `duration` | `duration` | `duration` |
| 0xBC | u8 | unused | unused | `screenSpace` |
| 0xC0.. | runtime scratch (below); zero in the files | | | |

Scratch layouts (offsets in the block). All coordinates are in 640×448 screen pixels,
origin top-left:

* 4/1: 0xC0 colour u32, 0xC4 copy of word 0x28 (unused), 0xC8 angle f32, 0xCC UV scale
  f32, 0xD0/0xD4 centre (320, 224), 0xD8/0xDC/0xE0/0xE4 left/top/right/bottom
  (0, 0, 640, 448).
* 5/1: 0xC0 colour, 0xC4 copy of 0x28, 0xC8/0xCC/0xD0/0xD4 left/top/right/bottom
  (0, 0, 640, 448).
* 5/2: 0xC4/0xC8 centre, 0xCC colour, 0xD0 copy of 0x28, 0xD4/0xD8/0xDC/0xE0
  left/top/right/bottom.

Corpus values:

| | B-FULL | F-FULL | F-MULTI #1 / #2 |
|---|---|---|---|
| colour keys | mode 0, 0xFFFFFF → 0xFFFFFF | mode 1: 0x969696 → mid 0xC8C8C8 at 0.4 → 0x969696 | mode 0: 0x966464 → 0x968282 |
| alpha, list, fade-in/out | 150, 2, 0.1667/0.8333 | 245, 6, 0/1 | 255 / 200, 2, 0.25/0.9667 |
| keys | zoom mode 1: 1.5 → mid 40 at 0.5 → 1.5; rotation mode 0: 0 → 0 (its mid −5 at 0.25 is ignored in mode 0) | — | radius mode 0: 200 → 840 / 200 → 500 (mids ignored) |
| duration | 25 | 25 | 70 |
| screenSpace | — | — | 0 (world-anchored) |

### 13.2 Timing [H]

* **Advance** (handler slot 2): `f = (duration == 0) ? 0 : elapsed`; run the sub's
  update with `f`; then `elapsed += 1`.
* **Draw** (slot 3): if `elapsed > 0` (always true after the first advance), append the
  instance to **screen list** `TnGetOffset38(word 0x28)` (§13.6). The actual drawing
  happens when that list is flushed, using the scratch values from the last update.
* **Update**, in this order:
  1. (subs 4/1 and 5/1 only) if `f == 0`, write the default scratch rectangle/centre
     values listed above.
  2. If `duration ≥ f`, sample everything at `(f, duration)` and store the colour.
     Otherwise set the scratch colour's alpha byte to 0. 5/2 only writes its centre and
     rectangle in this branch.
* **Draw callback**: does nothing when the scratch colour's alpha byte is 0.

So a node is drawn for `elapsed = 0 … duration`, i.e. **duration + 1** frames. The
fade envelope (§4.1) makes frame 0 alpha 0 when fade-in > 0, and frame `duration`
alpha 0 when fade-out < 1. Those frames are skipped, so B-FULL and F-MULTI show
`duration − 1` frames. F-FULL (fade-in 0, fade-out 1) shows all 26 frames. With
`duration == 0` the node samples frame 0 forever (`D = 0` gives start RGB | alpha) and
never stops. After the last frame the node keeps being appended to its list but draws
nothing.

### 13.3 Colour [H]

`s = SampleColour(0x00, 0x24; f, duration)` (RGB from the keys, A = opacity alpha ×
envelope). Then `c[k] = trunc((s[k]/255) · (node[k]/255) · 255)` for each of R, G, B, A,
using float maths and truncating to int. `c` is written to every vertex as 0..255
floats. As for all RW Im2D colours, 255 means 1.0; the PS2 driver scales it to the GS
0x80 = 1.0 range. Texture function is modulate: `Cs = texel.rgb × c.rgb`,
`As = texel.a × c.a`.

### 13.4 Screen quad [H]

All three subs draw **one RW Im2D quad**. It is an indexed triangle list with indices
(0,1,2, 0,2,3) and vertices v0 = (left, top), v1 = (left, bottom), v2 = (right,
bottom), v3 = (right, top). Each vertex has a constant screen Z (the RW Im2D near-screen
Z global) and `recipZ = 1 / camera.nearPlane`. No 3D projection is applied to the
corners.

**4/1 B-FULL — zoom/rotate echo of the previous frame.** Corners are the full screen
(0,0)–(640,448). The texture is the **display-buffer raster**, cached once at
effect-system init (`mdlEffectCacheDisplayRaster` FUN_004ABB60). This is the buffer
currently being displayed, i.e. the last completed frame. It is not shrunk or
downsampled. Its width W and height H are read from the raster (640 × 448 expected).
Let:

```
zoom  = 1 + 0.01·SampleScalar(0x34; f, duration)
angle = 0.01·SampleScalar(0x60; f, duration)        (radians)
for each corner (X, Y):
    du = zoom·(X − 320)/W,   dv = zoom·(Y − 224)/H
    u  = 320/W + du·cos(angle) − dv·sin(angle)
    v  = 224/H + du·sin(angle) + dv·cos(angle)
```

The rotation acts on normalised UVs, so with W ≠ H it is not aspect-correct. That is
how the original behaves. `zoom > 1` samples a larger area, so the old frame appears
**shrunk** towards the centre. `zoom < 1` magnifies it. The quad is skipped when
`|u0 − u2|` or `|v0 − v2|` (= zoom·640/W, zoom·448/H) is ≤ 0 or ≥ 7. While drawing,
GS CLAMP_1 is set to REGION_CLAMP with U 0..639 and V 0..447; afterwards the previous
CLAMP is restored. Samples outside the old frame therefore repeat its edge pixels. The
filter is the list default (bilinear). The texel alpha is whatever the previous frame
left in the frame-buffer alpha byte. This was not traced [L]. Implementations should
treat it as 1.0, so that `As = c.a`. For B-FULL, zoom goes 1.015 → 1.40 (at
frame 12) → 1.015, with no rotation. The result is an additive, outward-collapsing echo
of the screen at alpha ≤ 150.

**5/1 F-FULL — full-screen colour fill.** Corners are (0,0)–(640,448). Untextured
(texture raster set to none). Colour `c`.

**5/2 F-MULTI — masked sprite.** First compute `ρ = SampleScalar(0x8C; f, duration)`:

* `screenSpace ≠ 0`: centre = (320, 224), half-size `h = ρ` pixels.
* `screenSpace == 0` (corpus): anchor at the instance position `P` (world)
  (`mdlEffectAnchorScreenScale` FUN_0048A650):
  ```
  p   = camera.viewMatrix · P          (RW camera view matrix; x/z, y/z span 0..1 across the view window)
  centre = (640·p.x/p.z, 448·p.y/p.z)
  dist   = |P − camera frame position|
  h      = min(650·ρ·nodeScale / dist, 1500)
  if p.z ≤ near or p.z ≥ far: h = 0     (centre is still the projected point)
  ```
  The 650 is a fixed constant, independent of field of view. There is no on-screen test.
  With `h = 0` the quad is degenerate and nothing appears.

Corners are `(cx−h, cy−h)`–`(cx+h, cy+h)`, a square in screen pixels. The node is
drawn in **two passes** with the same corners and colour `c`:

1. **Mask pass.** The texture is the node's texture, with UVs (0,0), (0,1), (1,1), (1,0)
   for v0..v3. GS FRAME_1 is re-sent with FBMSK = 0x00FFFFFF, so **only the
   frame-buffer alpha byte is written**. ALPHA 0x44, TEST 0x31001. Effect: dest alpha
   := `texel.a × c.a`.
2. **Colour pass.** FRAME_1 is restored and the texture is unset. The same quad is drawn
   untextured. GS ALPHA = (the list's ALPHA) | 0x10. This changes the blend factor C
   from As to **Ad** (destination alpha). For the corpus list 2 (ALPHA 0x48) the result
   is `Cd' = c.rgb·Ad + Cd`. TEST 0x31001. Frame-buffer alpha afterwards = `c.a`.
3. ALPHA, TEST and the texture are restored.

Single-pass equivalent: draw the quad with RGB = `c.rgb` (texture RGB ignored) and
alpha = `texel.a × c.a`, using the list's blend equation with that alpha as the source
alpha. [H. Assumes a 32-bit frame buffer with a destination-alpha channel, M]

### 13.5 GS states [H]

All three set GS TEST = **0x31001** for their passes. The fields are:

* ATE = 1 with ATST = NEVER and AFAIL = FB_ONLY: every pixel "fails" and is written to
  the frame buffer only. Nothing is written to Z.
* ZTE = 1 with ZTST = ALWAYS: no depth rejection.

The draw is a pure overlay on top of whatever is already in the frame buffer. GS ALPHA is
**not** changed by 4/1 and 5/1. It stays the screen list's ALPHA (§4.1 table, through
the same remap `{0,1,4,6,2,3,5}`). Corpus results:

* B-FULL, list 2: physical list 4, ALPHA 0x48, additive: `Cs·As + Cd`.
* F-FULL, list 6: physical list 5, ALPHA 0x06: `Cs − Cd·As`. With `c` ≈ 0x96…0xC8
  grey and As = 245/255, this is a near-negative flash: bright pixels go dark, dark
  pixels become grey.
* F-MULTI, list 2: additive with dest-alpha (above).

Texture filtering, fog and the RW Z-test flag are the list defaults. They do not matter
because of TEST.

### 13.6 Draw order [M]

Screen lists come from the **offset38 group** of the current owner table:
`TnTable = {indexed[7], offset1C[7], offset38[7]}`. `TnGetOffset1C` serves the 3D
effect lists and `TnGetOffset38` the screen lists, both with the same remap. The
tables are 0x54 bytes each, from 0x005DC7D0 (P4), and hold ordering-table (OT) nodes
that are flushed in increasing index order. The default model owner (0x005DC878, set at
boot by `mdlResetModelTable`) maps the 3D effect lists to OT nodes 16–22 and the screen
lists to **OT nodes 66–72**. Screen filters therefore draw **after all 3D effect lists**
(physical list 0..6 in order, each with the same blend callback as §4.1). Kind 26
models (queued in the 3D lists, §12) also draw before them. Several scene
owner tables use the same OT nodes for offset1C and offset38, for example field units
0x005DC920 (31–37 for both) and battle 0x005DC9C8 (162–168). There, a screen filter
shares the physical list of its blend mode with the 3D effects and is drawn in append
order among them. The ordering of the main model pass relative to these OT nodes was not
traced. P3F tables were not checked.

---

## 14. Kinds not present in the corpus

2/15 track trails (`effPolygonTrack.c`), 9 lens flare, 13 thunder
(`effPolygonThunder.c`), 21 camera shake/sequence, 23 screen sprite, 27 helper (model
children), 29 distortion particle, 30 line nova, 31 battle-only effect, 32 model
drawable (P4 only). They follow the same handler conventions (§4) but were not
specified. A loader should still parse their records (generic record header + opaque
parameter block + secondary resource) and skip drawing.

---

## 15. `0xF0F000E1` — AfPack weapon-trail ribbons [H unless noted]

Binding: `AfPackBindResource` (FUN_004B6030); runtime: `AfPackCreateAndStartInstance`
FUN_004B57A0 → `AfPackCreateInstance` FUN_004B6900, `AfPackStartSections` FUN_004B6BB0,
`effAfterRunCommands` FUN_004BAED0; per-tick `Vpad_RefreshSelection` FUN_004B5800,
`AfPackRunList` FUN_004B6E80 → `Vpad_UpdateRenderEntries` FUN_004B5950 /
`Vpad_DrawEntries` FUN_004B5C60; geometry `effAfterCreateRibbon` FUN_004B8350,
`effAfterRebuildTrailMesh` FUN_004B8F40, `effAfterQueuePush` FUN_004BAD70,
`effAfterQueueSectionOffsets` FUN_004BB1D0, `effAfterSectionFraction` FUN_004B7460;
colour/fade `effMiscProgress*` FUN_004BC4D0..FUN_004BC540. Boot: `AfPackInit`
FUN_004B6C90. P3F has the same code in `eff_afpack.c` / `Kosaka/k_vpad.c`
(FUN_001E7CE0..FUN_001EF500), §15.10.

Terminology used below: a pack has up to 16 **sections**; each section is one
independent ribbon between two helper points, **edge A** (helper `start`) and **edge B**
(helper `end`). Each section keeps a history **ring** of sample pairs (A_i, B_i); index
i = 0 is the newest sample, i = count−1 the oldest. A **segment** s joins samples s and
s+1.

### 15.1 Layout

Header (0x10):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | s32 | version: 0x64 or 0x65 (corpus: always 0x65). Anything else asserts |
| 0x04 | s32 | flags (1 or 3 in the corpus). Copied into the runtime pack, never read |
| 0x08 | u16 | `sectionCount` (≤ 16; the binder uses 16-entry stack arrays) |
| 0x0A | u16 | `paramCount` (≤ 16) |
| 0x0C | u16 | `materialCount` (≤ 16) |
| 0x0E | u16 | 0 |

Then `paramCount` parameter records (0x38 each). The names are the roles the fields
play in §15.6 (the earlier "width/alpha" names were wrong: these are tangent-length
coefficients, nothing here is a width or an alpha):

| Off | Type | Name | Meaning |
|---|---|---|---|
| 0x00 | u16 | id | sections refer to it |
| 0x02 | 2 bytes | | not read |
| 0x04 | f32 | `midAxis` | interior samples: weight of the in-plane side axis |
| 0x08 | f32 | `midNewer` | interior samples: weight of the direction to the newer neighbour |
| 0x0C | f32 | `midOlder` | interior samples: weight of the direction from the older neighbour |
| 0x10 | f32 | `headAxis` | newest end: weight of the side axis |
| 0x14 | f32 | `headOlder` | newest end: weight of the direction from the older neighbour |
| 0x18 | f32 | `tailAxis` | oldest end: weight of the side axis |
| 0x1C | f32 | `tailNewer` | oldest end: weight of the direction to the newer neighbour |
| 0x20 | f32 | `kLen[0]` | tangent length per unit of segment length, for the edge that moved **more** |
| 0x24 | f32 | `kTurn[0]` | tangent length per unit of turn factor, edge that moved more |
| 0x28 | f32 | `kLen[1]` | as 0x20, for the edge that moved **less** (or equally) |
| 0x2C | f32 | `kTurn[1]` | as 0x24, edge that moved less |
| 0x30 | f32 | `lengthScale` | multiplies segment lengths in the arc-length parameterisation; it cancels out (§15.7), only `≤ 0` matters |
| 0x34 | f32 | | copied, never read |

All 163 corpus payloads (both games) use the same parameter record:
`mid = (0.8, 1.0, 0.8)`, `head = (1.0, 0.1)`, `tail = (1.0, 0.5)`,
`kLen/kTurn = 0.3/60 (edge that moved more), 0.3/15 (other edge)`, `lengthScale = 1.0`,
`0x34 = 0`.

Then `materialCount` material records (28 bytes):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u16 | id |
| 0x02 | u16 | flags: bit 0 = textured with the global trail texture `effect/z0.tmx` (loaded at boot by `AfPackInit`); other bits are stored but never tested. Corpus: 0 or 1 |
| 0x04 | u16 | texture kind (must be 0; anything else asserts) |
| 0x06 | u16 | colour kind (must be 0; anything else asserts) |
| 0x08 | u32 | texture data (kind 0: skipped) |
| 0x0C | 4 × RGBA8 | colours, byte order R, G, B, A: **c0** = edge A at the newest end, **c1** = edge A at the oldest end, **c2** = edge B newest, **c3** = edge B oldest (mode 2 uses them differently, §15.8) |

Then `sectionCount` sections, each a section record followed by its command stream:

| Off | Type | Meaning (v0x65; v0x64 records are 0x18 bytes: no 0x14..0x1B, `commandBytes` at 0x14, mode = 0, width = 10.0) |
|---|---|---|
| 0x00 | u16 | section id (not read) |
| 0x02 | u16 | param id |
| 0x04 | u16 | material id |
| 0x06 | 2 bytes | not read |
| 0x08 | s32 | `start` — helper id of edge A (looked up like E0 helpers, §2.3) |
| 0x0C | s32 | `end` — helper id of edge B |
| 0x10 | u16 | `capacity` — ring size in samples (corpus 3..10) |
| 0x12 | u16 | `rows` (`n`) — number of interpolated rows in the mesh, independent of `capacity` (corpus 3..40). Must be ≥ 2 for mode 0 |
| 0x14 | u16 | `mode`: 0 = two-edge zig-zag strip, 1 = two-edge strip with a centre line, 2 = camera-facing strip along edge A. Corpus: 18 × mode 0, 258 × mode 1, no mode 2 |
| 0x16 | 2 bytes | not read |
| 0x18 | f32 | `width` — used by mode 2 only (corpus: always 10.0) |
| 0x1C | s32 | `commandBytes` |
| 0x20 | bytes | command stream |

The payload is consumed exactly (all 163 corpus payloads). Ids are resolved by linear
search; an unmatched param/material id yields index = count, i.e. the record after the
last one (garbage), so data must always match.

The binder copies everything into a freshly allocated runtime pack; a new copy is made
for **every** instance (§15.3) and freed with it.

### 15.2 Command stream

16-bit opcodes; operands are halfwords (u32 operands as two halfwords, low first, not
aligned). Each section has its own interpreter state: `cursor`, `flags` (bit 0 = a
`wait` is in progress, bit 1 = recording, bit 3 = frozen), `elapsed` (wait counter),
`delay` (s16), `fadeFrames` (u32).

| Opcode | Operand | Meaning |
|---|---|---|
| 0x0000 | | **end**: stop this run; the cursor stays on the 0, so every later run stops immediately. A section sitting on `end` counts as finished (§15.3) |
| 0x0001 | u32 n | **wait n**: see below |
| 0x0100 | | recording on |
| 0x0101 | | recording off |
| 0x0102 | | **clear**: ring `count = 0`, `writeIndex = 0`. Stored tangents, the fade state and the colour state are kept |
| 0x0200 | | **fadeStart**: if `delay == 0` and `fadeFrames != 0`, start the fade with duration `fadeFrames / 30` s (§15.8, colour helper). Otherwise nothing. The fade starts at the current fade clock, and the clock advances later in the same update, so with F = `fadeFrames` the colours are × (1 − 1/F) in that update, × (1 − 2/F) in the next, and so on down to 0 |
| 0x0201 | s16 | set `delay`. Its only effect: a non-zero value turns the next `fadeStart`s into no-ops. Corpus: always 0 |
| 0x0202 | u32 | set `fadeFrames` (corpus: 1, 2, 3, 5, 8, 10, 11) |
| 0x0400 | | **freeze**: set bit 3 (sampling and drawing switch to world space, §15.5). Never cleared except by a restart. Not in the corpus |

A *run* executes opcodes until one stops it (`end`, or a `wait` that is not yet over).
**wait n**: when the run first reaches it, it sets `elapsed = 0` and marks the wait as
active; on every later run that starts on it, `elapsed += 1`. In either case, if
`elapsed ≥ n` the wait is passed and the **same run continues** with the next opcode;
otherwise the run stops. So a wait reached in run r is passed in run r + n (`wait 0`
passes at once). There is no default case: an unknown opcode makes the original loop
forever, so treat it as `end`. `delay` and `fadeFrames` are not reset at restart (the
allocation is not cleared) — use 0.

Typical stream (P4 `WP003.RMD`): `wait 12; on; wait 4; setDelay 0; setFadeFrames 1;
fadeStart; wait 1; off; clear; end`. Every corpus stream has the shape
`wait a; on; wait b; setDelay 0; setFadeFrames f; fadeStart; wait c; off; clear; end`.

### 15.3 Instance lifecycle

* **Creation** [H]. When an animation is started on a layer, the layer's attachment
  table gets `nextSecondary = secondary[anim]` (none if the play flags contain 0x20) and
  `pendingSecondary = 1` — also when the same animation is restarted. On the next
  `mdlAdvanceModel` (whether or not `tick != 0`) the old instance is released and,
  if there is an AfPack for the new slot, `AfPackCreateAndStartInstance(payload, model)`
  binds a new runtime pack (owner = model), creates the instance, appends it to the
  global AfPack list and starts it: each section's interpreter is reset (cursor at the
  stream start, flags 0, fade off, fade clock 0, ring empty), and every section's stream
  is run once (**run 0**). Run 0 never samples.
* **Release** [H]. Releasing a slot's instance (slot switch, or the layer/model being
  destroyed) calls `AfPackReleaseByOwner(model)`, which immediately frees **every**
  AfPack instance owned by that model, including one belonging to the other layer.
  Model destruction also calls it.
* **Self-destruction** [H]. After each update pass, an instance whose every section
  cursor sits on `end` is unlinked and freed (pack copy, rings, geometry, atomic,
  material). This happens in the same frame the last section reaches `end`, before the
  draw pass, so that frame is not drawn. The slot keeps a stale handle; all calls
  through it check list membership first and become no-ops.
* **Looping** [H]. The loop callback calls a no-op for the AfPack: an instance is not
  restarted when the animation wraps. Once its stream has ended it stays gone until the
  next animation start on that layer.
* **Blend delay** [H]. While the layer's `delay` (cross-fade ticks, §2.4) is > 0 the
  instance is created but gets no updates; the first update happens on the first
  tick with `delay == 0`.

### 15.4 Per-frame order

Per model tick (`mdlAdvanceModel` with `tick != 0`, layers 0 and 1, slot `delay == 0`):
`Vpad_RefreshSelection`: for each section, `mdlCopyIndexedMatrix(model, start)` and
`(model, end)` (§2.3: helper offset × bone LTM, world space); only the **translation
rows** are kept: `selA`, `selB`. The instance is flagged "updated". (If a lookup fails the
temporary matrix is used as allocated — RW gives identity — i.e. the origin [M].)

Per drawn model (`mdlQueueModelForDraw`): the slot colour (the model colour, unless model
flag 0x80) is handed to the instance (§15.8), and the instance is flagged "draw".

Once per frame the AfPack list callback (`AfPackRunList`, registered on the draw list
at boot) does, in list order:

1. For every instance flagged "updated" (flag then cleared): **update** (below).
2. Free every instance whose sections have all ended (§15.3).
3. For every instance flagged "draw" (flag then cleared): **draw** (§15.9).

So an instance whose model was not ticked this frame is not updated, and one whose model
was not queued for drawing is not drawn.

**Update**, per section in order: let `running = (layer 0 playback speed of the owner
model ≠ 0)` (Model+0xF4).

1. If `running`: execute one command run (run 1, 2, …).
2. If `running` and the section is recording (bit 1): push a sample (§15.5).
3. If the slot colour changed at least once: copy it into the section's material
   colour (§15.8).
4. Fade clock: `clock += running ? dt : 0`, `dt` = the animation time step (gp−0x7FC0,
   1/30 s, §1.4).
5. Rebuild the mesh (§15.6–15.8).

Commands therefore run **before** sampling in the same update: the update in which
`on` executes already pushes a sample, and in the update in which `off`/`clear`
execute nothing is pushed. With speed 0 (paused) nothing advances, but the mesh is still
rebuilt (unchanged).

### 15.5 Sampling, spaces and freeze

Push (`effAfterQueuePush`): the pair is written at `writeIndex`;
`count = min(count + 1, capacity)`; `writeIndex = (writeIndex + 1) mod capacity`. When
full, the oldest sample is overwritten. Every push is a valid sample: there is no
minimum distance and duplicates are kept. Logical index i maps to ring slot
`slot(i) = writeIndex − 1 − i`, plus `count` if negative. After the push the stored
tangents are computed (§15.6): for segment 0 if `count == 2`; for segment 1 and then
segment 0 if `count ≥ 3`. Older segments keep the tangents computed when they were
segment 0/1.

Spaces [H]:

* **Not frozen** (normal case, whole corpus): `M` = the model's root world matrix
  (`mdlGetMatrix(model)`; the stored matrix at Model+0x90 when model flag 0x8000 is set).
  The pushed sample is `selA·M⁻¹`, `selB·M⁻¹` (full inverse, points), i.e. the history
  is stored in **model space**. At draw time the ribbon's frame is set to `M` (current
  frame), so the trail moves rigidly with the character's root; only bone motion
  relative to the root leaves a trail.
* **Frozen** (`0x0400`): the world positions are pushed unchanged and the ribbon is
  drawn with an identity frame, so the trail stays in the world when the character moves.
  Samples pushed before the freeze are still model-space and are then drawn as if they
  were world positions (the game does not convert them).

### 15.6 Stored tangents (`effAfterQueueSectionOffsets`) [H]

Each ring slot holds, for each edge E ∈ {A, B}, two offset vectors for the segment
starting at that sample: `T0_E[s]` (leaving sample s towards older) and `T1_E[s]`
(arriving at sample s+1). Notation: `nrm(v)` = v/|v| (a zero vector stays zero [M]);
`×` cross product; indices are logical (0 = newest).

Helpers for edge E (the other edge is E′), sample i:

* `dNewer_E(i) = nrm(E_{i−1} − E_i)`; zero for i = 0.
* `dOlder_E(i) = nrm(E_i − E_{i+1})`; zero for i = count−1.
  (Both point towards the newer end.)
* `len_E(s) = |E_{s+1} − E_s|`.
* `turn(s) = (1 − nrm(B_s − A_s)·nrm(B_{s+1} − A_{s+1})) / 2` — 0 if the blade keeps its
  orientation, 1 if it flips.
* Frame vector `F(i)`: let j = i+1 if i < count−1, else j = i−1. `e1 = nrm(B_i − A_i)`,
  `e2 = nrm(B_j − A_j)`, `n = nrm(e2 × e1)` if j = i+1, `nrm(e1 × e2)` if j = i−1.
  If `|A_i − A_j| > |B_i − B_j|` then `F = e1 × n`, else `F = n × e1`. For a blade
  sweeping around a pivot this is the in-plane direction of motion.
* Side axis `axis_E(i)`, with j as above:
  * edge A, j = i+1: `e = nrm(B_i − A_i)`, `f = nrm(A_j − A_i)`, `n = nrm(e × f)`,
    `v = e × n`. Edge A, j = i−1: same e, f, n; `v = n × e`.
  * edge B, j = i+1: `g = nrm(B_j − B_i)`, `h = nrm(A_i − B_i)`, `n = nrm(g × h)`,
    `v = n × h`. Edge B, j = i−1: same g, h, n; `v = h × n`.
  * `axis_E(i) = nrm(v · (v·F(i)))` — v, flipped if needed to agree with F (zero if
    v ⟂ F).

Coefficient choice for segment s: if `len_A(s) > len_B(s)` then edge A uses
`(kLen[0], kTurn[0])` and edge B `(kLen[1], kTurn[1])`; otherwise the other way round.
Magnitude: `m_E(s) = kLen·len_E(s) + kTurn·turn(s)` (same for T0 and T1).

Weights `(wAxis, wNewer, wOlder)`:

* for `T0_E[s]` (direction evaluated at sample s): `(headAxis, 0, headOlder)` if
  `count == 2` or `s == 0`; otherwise `(midAxis, midNewer, midOlder)`.
* for `T1_E[s]` (direction evaluated at sample s+1): `(tailAxis, tailNewer, 0)` if
  `count == 2`, or `count == 3` and `s == 1`; otherwise `(midAxis, midNewer, midOlder)`.

`T0_E[s] = m_E(s) · nrm(wAxis·axis_E(s) + wNewer·dNewer_E(s) + wOlder·dOlder_E(s))`;
`T1_E[s] = m_E(s) · nrm(wAxis·axis_E(s+1) + wNewer·dNewer_E(s+1) + wOlder·dOlder_E(s+1))`.

Note the tail/head coefficients apply only at the moment of computation: with
capacity > 3, the segment that ends at the oldest sample was computed with interior
weights when it was segment 1, and keeps them.

### 15.7 Curve evaluation

**Arc-length parameter** (`effAfterSectionFraction`) [H]. Segment weight
`L_s = (|A_{s+1} − A_s| + |B_{s+1} − B_s|) · lengthScale` for s = 0..count−2 (both edges
together, so A and B share one parameterisation); `total = Σ L_s`. A parameter
d ∈ [0, 1) maps to (s, u):

* if `total ≤ 0`: s = 0, u = 0;
* else s = the first segment with `d < Σ_{k≤s} L_k / total`;
  `u = (d − Σ_{k<s} L_k/total) / (L_s/total)`. (No such segment only happens through
  rounding; the original asserts there — clamp to the last segment, u = 1.)

`lengthScale` cancels; only its sign matters.

**Point on edge E** at (s, u): cubic Bézier with `P0 = E_s`, `P1 = E_s − T0_E[s]`,
`P2 = E_{s+1} + T1_E[s]`, `P3 = E_{s+1}`;
`p = (1−u)³P0 + 3u(1−u)²P1 + 3u²(1−u)P2 + u³P3`. The newest and oldest samples are
used directly (not evaluated) at d = 0 / d = 1.

If `count < 2` every vertex position is set to (0, 0, 0): nothing visible.

### 15.8 Mesh, colours and UVs

The geometry is an RW geometry with flags positions | prelit | modulate-material-colour
(| textured if material bit 0); no normals, not lit; bounding sphere radius 1e9 (never
culled). Vertex colours are rebuilt only when the fade is active or on the first rebuild
after a restart; positions every update.

**Colour helper** `col(key, t)`: key 0 = (c0 → c1), key 1 = (c2 → c3);
`channel = trunc(fade · ((1−t)·first + t·second))` per RGBA byte, where `fade = 1` when
no fade is active, else `fade = max(0, 1 − (clock − clockAtFadeStart) / duration)`
(duration 0: fade = 1 before the start time, else 0). Truncation toward zero. The fade scales RGB as well as alpha, so under the additive blend the visible intensity falls roughly with fade².

**Mode 0** — zig-zag strip, `2n+1` vertices, `2n−1` triangles [H]:

* Edge A vertices at even indices `2j`, j = 0..n: parameter `dA_0 = 0`,
  `dA_j = (j − 0.5)/(n − 1)` for 1 ≤ j ≤ n−1, and vertex `2n` = oldest A sample
  (dA = 1).
* Edge B vertices at odd indices `2j+1`, j = 0..n−1: `dB_j = j/(n − 1)` for j ≤ n−2,
  vertex `2n−1` = oldest B sample (dB = 1).
* Triangles: for j = 0..n−2: `(2j, 2j+1, 2j+2)` and `(2j+1, 2j+3, 2j+2)`; then
  `(2n−2, 2n−1, 2n)`.
* Colour: A vertex `col(0, dA)`, B vertex `col(1, dB)`.
* UV (textured): A `(dA, 0)`, B `(dB, 1)`.

**Mode 1** — strip with centre line, rows r = 0..n+1, vertices `A_r = 3r`,
`B_r = 3r+1`, `C_r = 3r+2` (`3n+6` vertices, `4n+4` triangles) [H]:

* Row parameters: `t_0 = 0` (newest sample pair), `t_r = (r − 0.5)/(n + 0.5)` for
  r = 1..n (curve points), `t_{n+1} = 1` (oldest sample pair).
* Positions: `A_r`, `B_r` on their edge curves at `t_r`. `C_0 = (A_0 + B_0)/2`,
  `C_{n+1} = (A_{n+1} + B_{n+1})/2`, and for r = 1..n
  `C_r = (A_r + B_r + A_{r+1} + B_{r+1})/4` (half a row further along).
* Triangles: `(A_0, A_1, C_0)`, `(C_0, B_1, B_0)`; for r = 0..n−1:
  `(C_r, A_{r+1}, C_{r+1})`, `(C_r, C_{r+1}, B_{r+1})`, `(A_{r+1}, A_{r+2}, C_{r+1})`,
  `(B_{r+1}, C_{r+1}, B_{r+2})`; then `(C_n, A_{n+1}, C_{n+1})`,
  `(C_n, C_{n+1}, B_{n+1})`.
* Colours: `A_r = col(0, t_r)`, `B_r = col(1, t_r)`; `C_0`, `C_{n+1}` = per-byte
  `(a + b) >> 1` of the row's A and B; `C_r` (1..n) = per-byte
  `(A_r + B_r + A_{r+1} + B_{r+1}) >> 2`.
* UV: `A_r = (t_r, 0)`, `B_r = (t_r, 1)`; `C_0 = (0, 0.5)`, `C_{n+1} = (1, 0.5)`,
  `C_r = (r/(n + 0.5), 0.5)` for r = 1..n.

**Mode 2** — camera-facing strip on edge A only (B is sampled but only steers the
tangents); same vertex/triangle/UV layout as mode 1. Not in the corpus [H code, L use]:

* `h = width/2`; camera position = translation of the current camera's frame
  (modelling matrix, world). Note the samples are in model space unless frozen while
  the camera is in world space; the game does not reconcile this.
* For r = 1..n: `p_r` = edge-A curve at `t_r`; `prev` = `p_{r−1}` (for r = 1, the newest
  A sample). `v = nrm(cam − p_r)`, `τ = nrm(prev − p_r)`, `side = nrm(v × τ)`,
  `w = h·(1 − |v·τ|³)`; `A_r = p_r + w·side`, `B_r = p_r − w·side`.
* Row n+1: the same with `p` = oldest A sample and `prev = p_n`.
* Row 0: `p` = newest A sample, `τ = nrm(p − p_1)`, same formula.
* Centres as in mode 1.
* Colours: `A_r`, `B_r` = `col(1, t_r)` (c2 → c3 on both outer edges); centre line
  `C_0 = col(0, 0)`, `C_{n+1} = col(0, 1)`, `C_r = col(0, r/(n + 0.5))` (c0 → c1 along
  the middle).

**Texture**: only if material bit 0; the single global texture `effect/z0.tmx`
(u along the trail, 0 = newest, v across, 0 = edge A). Untextured materials draw vertex
colour only.

**Material colour** [H path, M maths]: each section's RW material colour starts as
(255, 255, 255, 254); from the first time the slot colour differs it is the slot colour
(model colour) with alpha 255 replaced by 254. RW modulates the prelit vertex colour by
it (`vertex × material / 255` per channel [M]).

**Colour range** [M]: colours are ordinary 0..255 RW prelit colours (corpus alphas go
up to 250); the RW PS2 pipeline maps them to the GS range itself. Treat 255 as 1.0.

### 15.9 Draw state (`Vpad_DrawEntries`) [H]

Saved and restored around the pass: cull mode, src/dst blend, Z write, Z test, fog.
Set: texture filter = linear (RW state 9 = 2), cull none (20 = 1), Z write off,
Z test on, fog off, vertex alpha on (12 = 1). Per section: GS ALPHA = 0x48
(`Cs·As + Cd`, additive weighted by source alpha), GS TEST = 0x71801 (alpha test
"never" with fail action "framebuffer only" = no Z write, depth test on). Then the
atomic's frame is set to identity, and, unless that section is frozen, transformed by
the model's current root world matrix (§15.5), and the atomic is rendered. Equivalent
modern state: blend `src·srcAlpha + dst`, depth test on, depth write off, no culling,
bilinear sampling.

### 15.10 P4 vs P3F, 0x64 vs 0x65

* P3F (`Kosaka/k_vpad.c` FUN_001E7CE0..FUN_001EE230, `eff_afpack.c`) [H for the command
  interpreter, push, colour/fade, mesh sizes, playback-speed test at Model+0xF4, freeze
  handling and render states; M for the rest]: same behaviour. Only internal struct
  sizes differ (ring struct 0x4C instead of 0x3C).
* v0x64 differs only in the section record (no mode/width; mode 0, width 10.0).

### 15.11 Worked example: P4 `WP003.RMD`

Payload (dumped with `eplprobe/afpack.py`): version 0x65, flags 1, 1 section, the
standard parameter record (§15.1), material 0 textured, colours
c0 = (100, 100, 150, 150), c1 = (255, 255, 255, 0), c2 = (100, 100, 150, 200),
c3 = (255, 255, 255, 0). Section: start (edge A) = helper 100, end (edge B) = helper 101,
capacity 6, n = 10, mode 1 → 36 vertices, 44 triangles. Stream:
`wait 12; on; wait 4; setDelay 0; setFadeFrames 1; fadeStart; wait 1; off; clear; end`.

Timeline (runs counted from the creation run 0, one per ticked frame, speed 1):

| Run | Commands | Ring count | Visible result |
|---|---|---|---|
| 0 | `wait 12` starts | 0 | nothing |
| 1–11 | waiting | 0 | nothing (all vertices at the origin) |
| 12 | wait passes; `on`; `wait 4` starts. First sample pushed | 1 | nothing (count < 2) |
| 13 | sample | 2 | one segment: rows interpolate newest → oldest with head/tail tangents |
| 14 | sample | 3 | two segments |
| 15 | sample | 4 | three segments |
| 16 | wait passes; fade 1 frame starts (duration 1/30 s); `wait 1` starts. Sample pushed | 5 | colours × max(0, 1 − (1/30)/(1/30)) = 0: invisible |
| 17 | `off`; `clear`; `end` | 0 | instance freed before drawing |

The trail is therefore visible on three frames (13, 14, 15). On each of them the 12 rows
have these colours (vertex colour before the model colour; edge A / edge B):

| Row r | t_r | edge A RGBA | edge B RGBA |
|---|---|---|---|
| 0 | 0 | 100,100,150,150 | 100,100,150,200 |
| 1 | 0.048 | 107,107,155,142 | 107,107,155,190 |
| 4 | 0.333 | 151,151,185,100 | 151,151,185,133 |
| 7 | 0.619 | 195,195,215,57 | 195,195,215,76 |
| 10 | 0.905 | 240,240,245,14 | 240,240,245,19 |
| 11 | 1 | 255,255,255,0 | 255,255,255,0 |

So, drawn additively, the ribbon is a pale blue band from the current blade position
that brightens towards white while its alpha falls to 0 at the oldest sample. Edge B
(helper 101) is about a third stronger than edge A. Rows are spaced by combined arc
length, so a fast swing spreads them evenly over the swept area. With a blade advancing
about 30 units per tick and turning 20° per tick (turn = (1 − cos 20°)/2 ≈ 0.03), the
faster edge's tangents are 0.3·30 + 60·0.03 ≈ 10.8 units long, so each segment bulges
outward along the swing direction instead of cutting the corner.

---

## 16. Corpus validation

Local probe scripts (not in the repository; they read the P3 FES/P4 RMD corpus):

| Script | Purpose |
|---|---|
| `chunks.py` | RMD top-level walker that honours the E0 padding; `e0s()` header decoder |
| `epl.py` | EPL tree/record parser (§3) |
| `validate.py` | full validation: E0 header/alignment, tree, records (version, kind, param offset/size vs the ELF tables, alloc words), clones, shared secondaries, secondary resources (texture slot < 22, TMX magic and size, RW stream integrity), matrix tables, AfPack parse. `-v` lists attachment ids missing from the matrix table |
| `attach.py` | where each E0/E1 attaches (model list / layer slot) and whether its helper ids exist |
| `afpack.py` | full AfPack decoder incl. command streams (`-v` prints the first payloads) |
| `dumpparams.py <kind> [sub]` | dumps all distinct parameter blocks of a kind as a column table |
| `elf.py`, `names.py` | ELF virtual-address reader (with gp-relative constants) and address → decomp-function map used for §4 |

Results (`validate.py`, 604 files):

| | P3F | P4 | Total |
|---|---|---|---|
| `0xF0F000E0` chunks | 10 | 15 | 25 |
| … parsed without error | 10 | 15 | 25 |
| … with `flags` bit 0 | 0 | 1 | 1 |
| … attached model-wide / to a slot | 4 / 6 | 10 / 5 | 14 / 11 |
| tree nodes | 22 | 22 | 44 |
| … parsed without error | 22 | 22 | 44 |
| … clone nodes (sourceFlags bit 0) | 0 | 0 | 0 |
| … shared-secondary nodes (bit 1) | 1 | 1 | 2 |
| `0xF0F000E1` chunks (AfPack 0x65) | 108 | 55 | 163 |
| … consumed exactly | 108 | 55 | 163 |
| `0xF0F00006` matrix tables | 174 | 208 | 382 |

Nodes per kind / sub:

| kind | sub | P3F | P4 | names |
|---|---|---|---|---|
| 1 | 1 | 2 | 7 | P-SMOKE |
| 1 | 3 | 7 | 4 | P-SPIRL |
| 1 | 5 | 2 | 0 | P-HSPAK |
| 4 | 1 | 1 | 0 | B-FULL |
| 5 | 1 | 1 | 0 | F-FULL |
| 5 | 2 | 2 | 0 | F-MULTI |
| 7 | 1 | 0 | 2 | T-BOARD |
| 10 | 1 | 0 | 1 | S-RADI |
| 10 | 4 | 1 | 1 | S-RAD_T |
| 10 | 5 | 1 | 0 | S-SPK_T |
| 10 | 6 | 0 | 2 | S-RNG_T |
| 12 | 1 | 2 | 0 | R-NOMAL |
| 12 | 3 | 0 | 2 | R-CRCLE |
| 16 | 1 | 2 | 0 | N-SPIRL |
| 26 | 0 | 0 | 1 | MODEL |
| 28 | 1 | 1 | 0 | M-SMOKE |
| 28 | 8 | 0 | 2 | M-CSPAK |

Secondary resources: srcKind 4 (texture slot) 19 P3F / 12 P4; srcKind 1 (embedded TMX)
1 / 6; srcKind 3 (embedded RW stream) 1 / 3; shared 1 / 1. Record version 210 in 43
records, 200 in one (P3F).

Attachment ids: every E0 range and every AfPack start/end id exists in the file's
matrix table, except P4 `N5351_0.RMD` (E0 index 0, table ids {2,500,501,502}) and one
P4 AfPack. The E0 case falls back to the model matrix at runtime (§2.4). The AfPack case
has no such fallback: a failed helper lookup leaves that edge at the origin (§15.4). E0 chunks in files
without a matrix table (5 P3F, 2 P4) use the `per3HelperId` fallback.

Remaining failures: **none**. The "node ids that did not resolve" in earlier probes were
caused by (a) starting the payload at header+20 instead of header+20+pad and (b) not
advancing the chunk walker by the padding.

---

## 17. P3 FES versus P4

| Aspect | P3F | P4 | Evidence |
|---|---|---|---|
| E0/E1/06 chunk layouts and loader | identical | identical | P3F `mdlManager.c` aux reader (cases −0xF0FFF20/−0xF0FFF1F/−0xF0FFFFA), corpus |
| Tree, node, record layouts | identical | identical | P3F `mdlEffect.c` builder (0x90/0xC0/0x2C allocations, version 0xD2, same flag bits); corpus |
| Kind handler table | 32 kinds × 13 slots (0x34) at 0x0069BE20 | 33 kinds × 16 slots at 0x00713480 | ELF dumps |
| Kind 32 (model drawable), slots 13–15 (finished/active/update) | absent | present | same |
| Emitter sub-type sizes | same | same | P3F table after the kind table |
| Global textures | 22 × `effect/be_%04d.tmx` in `effpack.bin` | same | strings, loop bound |
| RNG | same algorithm and seed 0x12345678 | same | P3F `effMisc.c`, `mdlEffect.c` |
| Record version 200 | 1 record (S-SPK_T) | not seen | corpus |
| Kinds used by RMDs in the sample | 1, 4, 5, 10, 12, 16, 28 | 1, 7, 10, 12, 26, 28 | corpus |
| Position-only helper (index ≥ 1000) | not seen | used (weapons) | corpus; P3F code path not checked |

The P3F decomp of the per-kind code is mostly raw Ghidra output; per-kind behaviour was
not compared line by line. Parameter sizes match for every (kind, sub) present in both
games, so the P4 behaviour is assumed for P3F. [M]

---

## 18. Open questions and confidence

| # | Question | Status / confidence |
|---|---|---|
| 1 | Chunk layouts, padding, attachment rules, matrix table | H |
| 2 | Tree/record layouts and relocation | H |
| 3 | Exact meaning of state flag 0x04 (y forced to 5.0) — retail code literally replaces y with 5.0; no corpus node uses it | M (behaviour), purpose unknown |
| 4 | Camera-aim formula (acos/atan2 identification of func_0044B920/func_0044B950) | M |
| 5 | Node `attributes` (0x88): no reader found | M (unused at runtime) |
| 6 | PTank quads are screen-aligned billboards with world-space size (RW PTank default) | M |
| 7 | Trail Catmull-Rom details (control-point order, colour/alpha interpolation) | M |
| 8 | Draw-list render states come from `mainDraw.c` (battle/field may install different owner tables with the same structure) | M |
| 9 | Kinds 4/5 screen filters (§13): corpus variants fully read; framebuffer alpha content of the display raster, exact OT position relative to model rendering | H (maths, states); M (display-raster texel alpha, draw order vs models) |
| 10 | AfPack runtime (§15) | H unless marked in §15 (items marked [M] there) |
| 11 | Model-wide effect list handle creation time (model prepare) | M |
| 12 | Tick rate 30 Hz | M |
| 13 | Emitter sub-types 2, 4, 6, 7, 9, 10, 11 and kinds 2, 9, 13, 15, 21, 23, 27, 29–32 | not specified (not in corpus) |
| 14 | Wind subs 2/3, ring sub 2, flash subs 2/3/7 | not specified (not in corpus) |
| 15 | Standalone `.EPL`/`.BED` files use the same tree layout | M (offsets agree with older notes; not re-parsed here) |
| 16 | P3F per-kind behaviour identical to P4 | M |

Implementation order suggestion for NovelVM: (1) fix the RMD chunk walker for the E0
padding; (2) parse E0 trees, E1 AfPacks and the 0xF0F00006 table; (3) implement the tree
transform/colour/scale propagation and the 30 Hz tick; (4) kind 1 with emitter subs 1, 3,
5 and billboard rendering with the seven blend modes; (5) rings, flash quads, wind;
(6) kind 28/26 (need the embedded RW stream loader the RMD parser already has);
(7) AfPack trails; (8) screen filters last.
