#ifndef COMMON_FORMATS_RMD_H
#define COMMON_FORMATS_RMD_H

#include "common/array.h"
#include "common/stream.h"
#include "common/str.h"

#include "graphics/surface.h"

namespace Common {

/** Safe foundation for Persona RenderWare Model node streams. */
class RMD {
public:
	struct Vec3 { float x = 0, y = 0, z = 0; };
	struct Quaternion { float x = 0, y = 0, z = 0, w = 1; };
	struct Matrix {
		float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	};
	struct SkinInfluence {
		byte indices[4] = { 0, 0, 0, 0 };
		float weights[4] = { 0, 0, 0, 0 };
	};
	struct Skin {
		uint32 boneCount = 0;
		uint32 usedBoneCount = 0;
		uint32 maxWeightsPerVertex = 0;
		Common::Array<byte> usedBones;
		Common::Array<SkinInfluence> influences;
		Common::Array<Matrix> inverseBindMatrices;
		Common::Array<uint32> meshBoneRemap;
		Common::Array<uint32> rendererBoneFrames;
		uint32 boneLimit = 0;
		uint32 meshCount = 0;
		uint32 rleSize = 0;
		Common::Array<uint16> meshRLECounts;
		Common::Array<uint16> rle;
	};
	struct AnimationKey {
		float time = 0;
		Vec3 translation;
		Quaternion rotation;
		bool hasRotation = true;
	};
	struct AnimationTrack {
		uint32 bone = 0;
		bool compressed = false;
		Common::Array<AnimationKey> keys;
	};
	struct Animation {
		float duration = 0;
		uint32 keyframeCount = 0;
		bool compressed = false;
		bool supported = true;
		Common::String unsupportedReason;
		Common::Array<AnimationTrack> tracks;
	};
	struct OpaqueNode {
		uint32 type = 0;
		Common::Array<byte> payload;
	};
	struct BonePose {
		Common::Array<Matrix> localTransforms;
		Common::Array<Matrix> worldTransforms;
		Common::Array<Matrix> skinTransforms;
	};
	struct UV { float u = 0, v = 0; };
	struct Color { byte r = 0, g = 0, b = 0, a = 0; };
	struct Frame {
		float transform[12] = { 0 };
		int32 parent = -1;
		uint32 flags = 0;
		uint32 nameId = 0;
		bool hasHAnim = false; // nameId is only meaningful when the frame carries an HAnim extension
		Common::String name;
	};
	struct HierarchyNode {
		uint32 nodeId = 0;
		uint32 index = 0;
		uint32 flags = 0;
	};
	struct Material {
		Color color;
		Common::String textureName;
		Common::String maskName;
		uint32 flags = 0;
		uint32 blendSource = 0;
		uint32 blendDestination = 0;
		uint32 textureFlags = 0;
		float ambient = 0;
		float specular = 0;
		float diffuse = 0;
		bool alpha = false;
		Common::String uvAnimationName; ///< base-UV animation (UV anim plugin 0x135, slot 0), or empty
		Common::String name; ///< RW user data "name" (0x11F), targeted by material animations
	};
	struct Texture {
		Common::String name;
		Common::String maskName;
		Common::Array<byte> nativeData;
		byte horizontalAddressing = 1;
		byte verticalAddressing = 1;
	};
	struct Geometry {
		uint32 declaredTriangleCount = 0;
		uint32 declaredMaterialCount = 0;
		Common::Array<Vec3> positions;
		Common::Array<Vec3> normals;
		Common::Array<Color> colors;
		Common::Array<UV> uvs;
		Common::Array<Common::Array<UV> > uvChannels;
		Common::Array<uint32> indices;
		Common::Array<uint32> materialIndices;
		Common::Array<uint32> materialMap;
		bool triangleStrip = false;
		bool modulateMatColor = false;
		Vec3 boundsMin;
		Vec3 boundsMax;
		bool skinned = false;
		Skin skin;
	};
	struct Atomic { int32 frame = -1; int32 geometry = -1; uint32 flags = 0; };
	struct Clump {
		Common::Array<uint32> frames;
		Common::Array<uint32> geometries;
		Common::Array<uint32> atomics;
	};

	RMD(SeekableReadStream *stream);

	bool ok() const { return _ok; }
	uint32 nodeCount() const { return _nodes.size(); }
	const Common::Array<Frame> &frames() const { return _frames; }
	const Common::Array<Geometry> &geometries() const { return _geometries; }
	const Common::Array<Material> &materials() const { return _materials; }
	const Common::Array<Texture> &textures() const { return _textures; }
	Graphics::Surface *getTextureSurface(uint32 textureIndex) const;
	const Common::Array<Atomic> &atomics() const { return _atomics; }
	const Common::Array<Clump> &clumps() const { return _clumps; }
	const Common::Array<Animation> &animations() const { return _animations; }
	const Common::Array<OpaqueNode> &opaqueNodes() const { return _opaqueNodes; }
	/** 0xF0F00006 helper matrix: an offset matrix in the space of the bone with HAnim id frameId. */
	struct HelperMatrix {
		uint32 id = 0;
		uint32 frameId = 0;
		Matrix offset;
	};
	const Common::Array<HelperMatrix> &helperMatrices() const { return _helperMatrices; }
	/**
	 * 0xF0F000E0: an EPL effect tree (see Common::EPL), instanced once per helper id in
	 * first..last. Blocks before the first clump belong to the whole model (slot -1), later
	 * ones to the animation slot they appear in.
	 */
	struct EffectBlock {
		uint16 first = 0, last = 0;
		uint32 flags = 0; ///< bit 0: restart when the slot's animation loops
		int32 slot = -1;
		Common::Array<byte> payload;
	};
	const Common::Array<EffectBlock> &effectBlocks() const { return _effectBlocks; }
	/** 0xF0F000E1: a weapon-trail (AfPack) payload for an animation slot, kept raw. */
	struct TrailPack {
		int32 slot = -1;
		Common::Array<byte> payload;
	};
	const Common::Array<TrailPack> &trailPacks() const { return _trailPacks; }
	/**
	 * One Persona animation slot: the nodes up to a terminator (0xF0F00004). Games select
	 * animations by slot index; placeholder slots are empty and instance slots reuse another.
	 */
	struct AnimationSlot {
		int32 animation = -1;  ///< index into animations(), or -1
		int32 instanceOf = -1; ///< slot reused through an instance node, or -1
		Common::Array<uint32> extras; ///< opaqueNodes() indices: transform override, material, UV, particle animation
		Common::Array<uint32> uvAnimations; ///< uvAnimations() indices from this slot's UV animation dictionary
		Common::Array<uint32> materialAnimations; ///< materialAnimations() indices
		/// 0xF0F00005 RwMatrix: replaces the root frame's local transform while this slot plays.
		/// In the corpus always a diagonal scale (per-NPC size variants, ~0.89-1.1).
		bool hasTransformOverride = false;
		Matrix transformOverride;
	};
	/** RenderWare UV animation (param keyframes, type 0x1C1), base UV channel only. */
	struct UVAnimationKey {
		float time = 0;
		float theta = 0, scaleU = 1, scaleV = 1, skew = 0, x = 0, y = 0;
	};
	struct UVAnimation {
		Common::String name;
		float duration = 0;
		Common::Array<UVAnimationKey> keys;
	};
	const Common::Array<UVAnimation> &uvAnimations() const { return _uvAnimations; }
	/**
	 * Persona material animation (0xF0F00080-83) for the materials whose RW user data "name"
	 * matches. Channel 0 (0xF0F00080) is transparency: material alpha = 1 - value. Channel 1
	 * (0xF0F00081) is the material RGB. The game has no effect for 0xF0F00082/83.
	 */
	struct MaterialAnimation {
		Common::String name;
		uint32 channel = 0;
		float duration = 0;
		Common::Array<float> times;
		Common::Array<float> values; ///< one per key (transparency) or three (r, g, b in 0..1)
	};
	const Common::Array<MaterialAnimation> &materialAnimations() const { return _materialAnimations; }
	/** Per-material animation state for one slot at one time. */
	struct MaterialPose {
		/// six floats per material: u' = m[0]*u + m[2]*v + m[4], v' = m[1]*u + m[3]*v + m[5]
		Common::Array<float> uvTransforms;
		/// opacity per material, 1 when not animated
		Common::Array<float> opacity;
		/// material RGB (three floats in 0..1 per material): the material's own colour unless animated
		Common::Array<float> color;
	};
	/**
	 * Evaluate @p slot's UV and material animations at @p time (seconds, each looping over its
	 * own duration). Materials a slot does not animate keep their own UVs, colour and opacity.
	 */
	void evaluateSlotMaterials(uint32 slot, float time, MaterialPose &out) const;
	const Common::Array<AnimationSlot> &animationSlots() const { return _animationSlots; }
	/** Skeletal animation played by @p slot, following instance references; -1 if none. */
	int32 animationForSlot(uint32 slot) const;
	bool evaluateAnimation(uint32 animationIndex, float time, BonePose &pose) const;
	/** Frame index for an HAnim hierarchy index (animation track bone), or -1. */
	int32 frameForHierarchy(uint32 index) const;

private:
	struct Node {
		uint32 type;
		uint32 size;
		uint32 version;
		uint32 payloadOffset;
		uint32 payloadEnd;
	};
	struct RecoveryRange {
		uint32 start;
		uint32 end;
	};

	bool _ok = false;
	Common::Array<byte> _data;
	Common::Array<Node> _nodes;
	Common::Array<Frame> _frames;
	Common::Array<HierarchyNode> _hierarchy;
	uint32 _nextHAnimFrame = 0;
	uint32 _currentHAnimFrame = 0;
	bool _parsingFrameExtensions = false;
	Common::Array<Geometry> _geometries;
	Common::Array<Material> _materials;
	Common::Array<Texture> _textures;
	Common::Array<Atomic> _atomics;
	Common::Array<Clump> _clumps;
	Common::Array<Animation> _animations;
	Common::Array<OpaqueNode> _opaqueNodes;
	Common::Array<AnimationSlot> _animationSlots;
	Common::Array<HelperMatrix> _helperMatrices;
	Common::Array<EffectBlock> _effectBlocks;
	Common::Array<TrailPack> _trailPacks;
	bool _seenClump = false;
	uint32 _terminators = 0; ///< animation slot counter: number of 0xF0F00004 chunks so far
	Common::Array<UVAnimation> _uvAnimations;
	Common::Array<MaterialAnimation> _materialAnimations;
	void resolveSlotInstances();
	bool _inMaterial = false;
	void parseUVAnimationDictionary(const Node &node, AnimationSlot &slot);
	bool _slotOpen = false;
	Common::Array<RecoveryRange> _recoveryRanges;

	static bool validVersion(uint32 version);
	bool readNode(uint32 offset, uint32 limit, Node &node) const;
	uint32 nextSiblingOffset(const Node &node, uint32 end) const;
	bool parsePersonaAnimationSet(uint32 begin, uint32 end);
	bool readSiblings(uint32 begin, uint32 end);
	bool validateChildren(uint32 begin, uint32 end, uint32 depth);
	bool validateUVDictionary(const Node &node) const;
	bool recoverOwnedTail(uint32 offset, uint32 end, uint32 &next);
	void parseContent(uint32 begin, uint32 end);
	void parseNode(const Node &node);
	void parseSkin(const Node &node);
	void parseAnimation(const Node &node);
	void parsePersonaAnimation(const Node &node);
	bool decodeTexture(const Texture &texture, Graphics::Surface *&surface) const;
	bool validateFrames() const;
	bool validateReferences() const;
	void clearModel();
};

} // namespace Common

#endif // COMMON_FORMATS_RMD_H
