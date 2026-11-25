/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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

#ifndef COMMON_FORMATS_GMO_H
#define COMMON_FORMATS_GMO_H

#include "common/array.h"
#include "common/stream.h"
#include "common/str.h"

#include "graphics/surface.h"

namespace Common {

/**
 * Sony PSP GMO 3D model container.
 *
 * GMO files are a nested chunk stream with no fixed file header layout:
 * a 16-byte signature ("OMG.00.1PSP" on disk) is followed by a tree of
 * chunks, each describing its own layout. This parser reads the chunk
 * tree and exposes the standard MODEL child chunks: bones, parts, meshes,
 * arrays (vertex data), materials, texture references (embedded TIM2/
 * FILE_IMAGE payloads), and motions.
 *
 * Reference facts and byte layout cross-checked against, and taken from,
 * (facts only, no code copied):
 *   - FFRTT wiki "PSP/GMO Format" (MrAdults/Ukurere):
 *     https://wiki.ffrtt.ru/index.php/PSP/GMO_Format
 *   - PiosGmoLibrary (unlicensed, facts-only):
 *     https://github.com/pionome/PiosGmoLibrary
 *   - "GmoImport" 3ds Max importer maxscript (P3P-focused):
 *     https://drive.autodesk.com/de29160e9/shares/SH35dfcQT936092f0e436a2cd211144bb447
 *   - Real Persona 3 Portable umd0.cpk files (295 .GMO) — the spec.
 *
 * Chunk header (all little-endian, offsets relative to chunk start):
 *   full:   u16 type | u16 argsOffs | u32 nextOffs | u32 childOffs |
 *           u32 dataOffs
 *   half:   u16 type | u16 argsOffs | u32 nextOffs   (type & 0x8000)
 * Name: argsOffs-16 bytes immediately after the 16-byte header.
 * Children occupy [childOffs, nextOffs); siblings are at nextOffs.
 *
 * References (GmoRef) inside chunk args are 4 flat bytes:
 *   u8 index | u8 level | u8 type | u8 reserved
 * (validated against real P3P files; the 12/4/20-bit packing some
 * libraries use does not match real data).
 *
 * Vertex arrays (ARRAYS) layout:
 *   u32 Format | u32 VertexCount | u32 MorphCount | u32 MorphFormat
 *   then per vertex (in this order, presence per Format bit flags):
 *   optional morph UV offsets (MorphFormat>0: u16 * n / 32768),
 *   weights, texture coords, color, normal, position.
 *
 * Embedded textures (TEXTURE -> FILE_IMAGE) are Sony TIM2/TM2 images
 * (verified: all 295 real P3P GMOs embed a single 8-bit-indexed PSMT8
 * texture with a 256-entry 32-bit CSM1-swizzled CLUT). See Graphics::TM2.
 */
class GMO {
public:
	/**
	 * Parse a GMO from the given stream starting at the current position.
	 * The stream is read to its end (the FILE chunk spans the whole file).
	 */
	GMO(SeekableReadStream *stream);
	~GMO();

	/** True if a FILE/MODEL chunk tree was parsed. */
	bool ok() const { return _ok; }

	// Chunk type ids (low 15 bits; 0x8000 marks a half-chunk).
	enum ChunkType {
		kFile = 0x0002,
		kModel = 0x0003,
		kBone = 0x0004,
		kPart = 0x0005,
		kMesh = 0x0006,
		kArrays = 0x0007,
		kMaterial = 0x0008,
		kLayer = 0x0009,
		kTexture = 0x000A,
		kMotion = 0x000B,
		kFCurve = 0x000C,
		kFileName = 0x0012,
		kFileImage = 0x0013,
		kBoundingBox = 0x0014,
		kArrayOffset = 0x0015,
		kParentBone = 0x0041,
		kVisibility = 0x0042,
		kBlendBones = 0x0044,
		kBlendOffsets = 0x0045,
		kPivot = 0x0046,
		kTranslate = 0x0048,
		kRotateZYX = 0x0049,
		kRotateYXZ = 0x004A,
		kRotateQ = 0x004B,
		kScale = 0x004C,
		kDrawPart = 0x004E,
		kSetMaterial = 0x0061,
		kBlendSubset = 0x0062,
		kDrawArrays = 0x0066,
		kDiffuse = 0x0082,
		kSpecular = 0x0083,
		kEmission = 0x0084,
		kAmbient = 0x0085,
		kReflection = 0x0086,
		kRefraction = 0x0087,
		kBump = 0x0088,
		kSetTexture = 0x0091,
		kMapType = 0x0092,
		kMapFactor = 0x0093,
		kBlendFunc = 0x0094,
		kTexFunc = 0x0095,
		kTexFilter = 0x0096,
		kTexWrap = 0x0097,
		kTexCrop = 0x0098,
		kTexGen = 0x0099,
		kTexMatrix = 0x009A,
		kFrameLoop = 0x00B1,
		kFrameRate = 0x00B2,
		kAnimate = 0x00B3,
		kBlindData = 0x00F1,
	};

	struct Ref {
		int index = 0;  // sibling index this reference points at
		byte level = 0;
		uint16 type = 0;   // low 16 bits of the target chunk type
	};

	// ---- bones ----
	struct Bone {
		Common::String name;
		int parentIndex = -1;
		float translate[3] = {0, 0, 0};
		float rotate[4] = {0, 0, 0, 1}; // euler xyz or quat wxyz per rotateType
		float scale[3] = {1, 1, 1};
		float pivot[3] = {0, 0, 0};
		byte rotateType = 0; // 0=ZYX, 1=YXZ, 2=quat
		bool hasRotate = false, hasTranslate = false, hasScale = false;
		uint32 visibility = 1;
		Common::Array<Ref> drawParts;      // parts to draw under this bone
		Common::Array<Ref> blendBones;     // skinning bone indices
		Common::Array<float> blendOffsets; // 16 floats per matrix
		// BLIND_DATA: Atlus extension blob. "per3Helper" carries a helper id
		// plus 9 floats (3 vec3s) of tool metadata; not render-critical.
		Common::String blindName;
		uint32 blindId = 0;
		float blindData[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
	};

	// ---- parts / meshes / arrays ----
	struct Arrays {
		Common::String name;
		uint32 format = 0;
		uint32 vertexCount = 0, morphCount = 0, morphFormat = 0;
		Common::Array<float> positions;   // xyz triplets
		Common::Array<float> normals;     // xyz triplets
		Common::Array<float> texcoords;   // uv pairs (raw, pre-offset)
		Common::Array<float> colors;      // rgba quadruplets (0..1)
		Common::Array<float> weights;     // per-vertex rows (blend weight count)
		Common::Array<uint16> uvOffsets;  // raw u16 offsets if morphFormat>0
		uint32 drawMode = 0;              // mode of first DrawArrays
	};

	struct DrawInfo {
		Ref arrays;
		uint32 mode = 0, vertexCount = 0, primitiveCount = 0;
		uint32 startVertex = 0;
		// Non-sequential draws (mode & 0x100 == 0) carry an explicit u16
		// index list: primitiveCount groups of vertexCount indices.
		Common::Array<uint16> indices;
		bool sequential() const { return (mode & 0x100) != 0; }
	};

	struct Mesh {
		Common::String name;
		Ref material;
		Common::Array<DrawInfo> draws;
		Common::Array<uint32> blendSubset;
	};

	struct Part {
		Common::String name;
		Common::Array<Mesh> meshes;
		Common::Array<Arrays> arrays;
	};

	// ---- materials ----
	struct Layer {
		Common::String name;
		Ref texture;
		float texCrop[4] = {0, 0, 1, 1};
		float texMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
		uint32 texWrapU = 0, texWrapV = 0;
		uint32 texGen = 0;
		uint32 mapType = 0;      // MAP_TYPE: texture coordinate generation mode
		float mapFactor = 1.0f;  // MAP_FACTOR: texture/diffuse blend weight
		uint32 blendMode = 0, blendSrc = 0, blendDst = 0; // BLEND_FUNC
		bool hasBlendFunc = false;
		uint32 texFunc = 0, texComp = 0;                  // TEX_FUNC
		uint32 texFilterMag = 0, texFilterMin = 0;        // TEX_FILTER
	};

	struct Material {
		Common::String name;
		float diffuse[4] = {1, 1, 1, 1};
		float specular[4] = {1, 1, 1, 1};
		float emission[4] = {0, 0, 0, 1};
		float ambient[4] = {1, 1, 1, 1};
		float specularPower = 0.0f; // SPECULAR's trailing float
		float reflection = 0.0f;    // REFLECTION
		float refraction = 0.0f;    // REFRACTION
		float bump = 0.0f;          // BUMP
		uint32 transAlgo = 0;       // BLIND_DATA "transAlgo"
		bool hasDiffuse = false, hasSpecular = false, hasEmission = false, hasAmbient = false;
		Common::Array<Layer> layers;
	};

	// ---- textures ----
	struct Texture {
		Common::String name;
		Common::String fileName;
		Common::Array<byte> image; // RAW sub-stream of FILE_IMAGE (a TIM2 image)
	};

	// ---- motions ----
	struct FCurve {
		Common::String name;
		uint32 type = 0, valueCount = 0, frameCount = 0;
		Common::Array<float> times;    // one per frame
		Common::Array<float> values;   // valueCount floats per frame
	};

	struct Animate {
		Ref block;         // target chunk (BONE/MATERIAL/etc), index into its array
		uint32 type = 0;   // animated property chunk type
		uint32 index = 0;
		Ref fcurve;
	};

	struct Motion {
		Common::String name;
		float frameRate = 0;
		float frameLoopStart = 0, frameLoopEnd = 0;
		Common::Array<Animate> animates;
		Common::Array<FCurve> fcurves;
	};

	// Sampled animation state. Every bone/property is optional; the renderer
	// falls back to the static value for anything not set.
	struct BonePose {
		int boneCount = 0;
		Common::Array<bool> hasTranslate, hasRotate, hasScale;
		Common::Array<float> translate; // 3 per bone
		Common::Array<float> rotate;    // 4 per bone (quat x,y,z,w)
		Common::Array<byte> rotateType; // 0=ZYX, 1=YXZ, 2=quat
		Common::Array<float> scale;     // 3 per bone
	};

	struct MaterialPose {
		int materialCount = 0;
		Common::Array<bool> hasDiffuse, hasEmission, hasAmbient, hasSpecular, hasTexCrop;
		Common::Array<float> diffuse, emission, ambient, specular; // 4 per material
		Common::Array<float> texCrop; // 4 per material, replaces its layer's TEXCROP (UV scrolling)
	};

	/**
	 * Sample one real MOTION at `frame` (in frame units, as stored in the
	 * FCURVE `times`) into pose overrides. Values are linearly interpolated
	 * between the two surrounding keys; ANIMATE refs target either a BONE
	 * (TRANSLATE, ROTATE_Q/ZYX/YXZ, SCALE) or a MATERIAL (DIFFUSE, EMISSION,
	 * AMBIENT, SPECULAR, TEXCROP). Returns false when the motion has no usable tracks.
	 */
	bool evaluateMotion(uint motionIndex, float frame, BonePose &bonePose, MaterialPose &materialPose) const;

	// Accessors.
	const Common::Array<Bone> &bones() const { return _bones; }
	const Common::Array<Part> &parts() const { return _parts; }
	const Common::Array<Material> &materials() const { return _materials; }
	const Common::Array<Texture> &textures() const { return _textures; }
	const Common::Array<Motion> &motions() const { return _motions; }
	const Common::String &name() const { return _name; }

	/** Decode one embedded texture (at _textures[i]) into an RGBA surface. */
	Graphics::Surface *getTextureSurface(int i) const;

	/**
	 * Model-level ARRAY_OFFSET (vertex space -> bone space), applied as
	 * pos = pos * scale + offset. Returns false when absent (turn off).
	 */
	bool getVertexOffset(float off[3], float scale[3]) const {
		if (!_hasVertexOffset)
			return false;
		off[0] = _vertexOffset[0]; off[1] = _vertexOffset[1]; off[2] = _vertexOffset[2];
		scale[0] = _vertexOffset[3]; scale[1] = _vertexOffset[4]; scale[2] = _vertexOffset[5];
		return true;
	}

	/**
	 * Model-level ARRAY_OFFSET texture variant (TEXTURE_FLOAT), applied to
	 * each texcoord as uv = uv * scale + offset. Returns false when absent.
	 */
	bool getTextureOffset(float off[2], float scale[2]) const {
		if (!_hasTextureOffset)
			return false;
		off[0] = _textureOffset[0]; off[1] = _textureOffset[1];
		scale[0] = _textureOffset[2]; scale[1] = _textureOffset[3];
		return true;
	}

private:
	struct Chunk {
		uint32 offset = 0;
		uint32 nextOffs = 0;
		uint32 childOffs = 0;
		uint32 dataOffs = 0;
		uint16 type = 0;
		uint16 argsOffs = 0;
		bool half = false;
	};

	bool _ok = false;
	Common::String _name;

	// Model-level ARRAY_OFFSET: [offX, offY, offZ, scaleX, scaleY, scaleZ]
	// applied to every vertex position (v = v*scale + offset). Real P3P
	// files store vertex data normalized to ~[-1,1] and scale it into bone
	// space via this chunk; stored atomically so a single getter is safe.
	float _vertexOffset[6] = {0, 0, 0, 1, 1, 1};
	bool _hasVertexOffset = false;

	// Model-level ARRAY_OFFSET texture variant: [offU, offV, scaleU,
	// scaleV] applied to every texcoord (uv = uv*scale + offset).
	float _textureOffset[4] = {0, 0, 1, 1};
	bool _hasTextureOffset = false;

	Common::Array<Bone> _bones;
	Common::Array<Part> _parts;
	Common::Array<Material> _materials;
	Common::Array<Texture> _textures;
	Common::Array<Motion> _motions;

	// Parsing uses a plain byte-array copy of the file.
	byte *_data;
	uint32 _size;
	uint32 _pos;

	uint16 ru16();
	uint32 ru32();
	float rf32();
	Common::String readNameLen(int len);
	Chunk readChunkAt(uint32 pos);
	void skip(uint32 n) { _pos += n; }

	void parseFile();
	void parseModel(const Chunk &model);
	void parseBone(const Chunk &c, Bone &bone);
	void parsePart(const Chunk &c, Part &part);
	void parseMaterial(const Chunk &c, Material &mat);
	void parseTexture(const Chunk &c, Texture &tex);
	void parseMotion(const Chunk &c, Motion &motion);
};

} // namespace Common

#endif // COMMON_FORMATS_GMO_H
