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
	bool evaluateAnimation(uint32 animationIndex, float time, BonePose &pose) const;

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
	Common::Array<RecoveryRange> _recoveryRanges;
	Common::Array<float> _frameMinAngleToBind;

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
	int32 frameForHierarchy(uint32 index) const;
	void computeFrameMinAngleToBind();
};

} // namespace Common

#endif // COMMON_FORMATS_RMD_H
