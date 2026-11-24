#include "common/formats/rmd.h"

#include <cstring>
#include <cmath>

#include "common/endian.h"
#include "graphics/ps2texture.h"

namespace Common {

namespace {

const uint64 kMaxInputSize = 256 * 1024 * 1024;
// Real RenderWare nesting is shallow; keep hostile input from exhausting the stack.
const uint32 kMaxNodeDepth = 64;
const uint32 kMaxGeometryTexCoordChannels = 8;
const uint32 kMaxGeometryMorphTargets = 64;

enum {
	kStruct = 1, kString = 2, kExtension = 3, kTextureReference = 6,
	kMaterial = 7, kMaterialList = 8, kFrameList = 14, kGeometry = 15,
	kClump = 16, kAtomic = 20, kTextureNative = 21, kTextureDictionary = 22,
	kGeometryList = 26, kMeshList = 0x50E, kHAnimFrameExtension = 0x11E,
	kHAnimOpaqueExtension = 0x100,
	kAnimation = 0x108, kSkin = 0x116, kPersonaAnimation = 0x1B,
	kUVAnimationDictionary = 0x2B, kPersonaAnimationTerminator = 0xF0F00004,
	kPersonaAnimationOverride = 0xF0F00005, kPersonaParticleList = 0xF0F000E0,
	kPersonaAnimationPlaceholder = 0xF0F00001, kPersonaAnimationInstance = 0xF0F00003,
	kPersonaVisibilityAnimation = 0xF0F00080, kPersonaParticleAnimation = 0xF0F000E1,
};

uint32 readUint32LE(const Common::Array<byte> &data, uint32 offset) {
	return READ_LE_UINT32(data.data() + offset);
}

uint16 readUint16LE(const Common::Array<byte> &data, uint32 offset) {
	return READ_LE_UINT16(data.data() + offset);
}

uint64 readUint64LE(const Common::Array<byte> &data, uint32 offset) {
	return READ_LE_UINT64(data.data() + offset);
}

float readFloatLE(const Common::Array<byte> &data, uint32 offset) {
	uint32 bits = readUint32LE(data, offset);
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

float decodePersonaPackedFloat(uint16 value) {
	uint32 bits = (uint32)(value & 0x8000) << 16;
	if (value & 0x7FFF) {
		bits |= (uint32)(value & 0x7800) << 12;
		bits |= 0x38000000;
		bits |= (uint32)(value & 0x07FF) << 12;
	}
	float result;
	std::memcpy(&result, &bits, sizeof(result));
	return result;
}

RMD::Matrix identityMatrix() {
	return RMD::Matrix();
}

RMD::Matrix multiply(const RMD::Matrix &a, const RMD::Matrix &b) {
	RMD::Matrix out = {};
	for (uint32 i = 0; i < 16; ++i)
		out.m[i] = 0;
	for (uint32 row = 0; row < 4; ++row)
		for (uint32 column = 0; column < 4; ++column)
			for (uint32 i = 0; i < 4; ++i)
				out.m[row * 4 + column] += a.m[row * 4 + i] * b.m[i * 4 + column];
	return out;
}

RMD::Matrix frameMatrix(const RMD::Frame &frame) {
	RMD::Matrix out = identityMatrix();
	for (uint32 row = 0; row < 3; ++row)
		for (uint32 column = 0; column < 3; ++column)
			out.m[row * 4 + column] = frame.transform[row * 3 + column];
	out.m[12] = frame.transform[9];
	out.m[13] = frame.transform[10];
	out.m[14] = frame.transform[11];
	return out;
}

RMD::Matrix poseMatrix(const RMD::AnimationKey &key) {
	const float x = key.rotation.x, y = key.rotation.y, z = key.rotation.z, w = key.rotation.w;
	RMD::Matrix out = identityMatrix();
	out.m[0] = 1 - 2 * (y * y + z * z); out.m[1] = 2 * (x * y + z * w); out.m[2] = 2 * (x * z - y * w);
	out.m[4] = 2 * (x * y - z * w); out.m[5] = 1 - 2 * (x * x + z * z); out.m[6] = 2 * (y * z + x * w);
	out.m[8] = 2 * (x * z + y * w); out.m[9] = 2 * (y * z - x * w); out.m[10] = 1 - 2 * (x * x + y * y);
	out.m[12] = key.translation.x; out.m[13] = key.translation.y; out.m[14] = key.translation.z;
	return out;
}

RMD::Quaternion slerp(const RMD::Quaternion &a, const RMD::Quaternion &b, float alpha) {
	RMD::Quaternion end = b;
	float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (dot < 0) { dot = -dot; end.x = -end.x; end.y = -end.y; end.z = -end.z; end.w = -end.w; }
	if (dot > 0.9995f) {
		RMD::Quaternion out = { a.x + alpha * (end.x - a.x), a.y + alpha * (end.y - a.y), a.z + alpha * (end.z - a.z), a.w + alpha * (end.w - a.w) };
		const float length = sqrtf(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
		if (length > 0) { out.x /= length; out.y /= length; out.z /= length; out.w /= length; }
		return out;
	}
	const float theta = acosf(dot), scale = sinf(theta);
	const float aScale = sinf((1 - alpha) * theta) / scale, bScale = sinf(alpha * theta) / scale;
	return { a.x * aScale + end.x * bScale, a.y * aScale + end.y * bScale, a.z * aScale + end.z * bScale, a.w * aScale + end.w * bScale };
}

bool canRead(uint32 offset, uint64 count, uint32 end) {
	return offset <= end && count <= end - offset;
}

struct NativeNode {
	uint32 type;
	uint32 size;
	uint32 payloadOffset;
	uint32 payloadEnd;
};

bool validRenderWareVersion(uint32 version) {
	return version == 0x1C02002D || version == 0x1C020037 || version == 0x40000000;
}

bool readNativeNode(const Common::Array<byte> &data, uint32 offset, uint32 limit, NativeNode &node) {
	if (offset > limit || limit - offset < 12)
		return false;
	node.type = readUint32LE(data, offset);
	node.size = readUint32LE(data, offset + 4);
	if (!validRenderWareVersion(readUint32LE(data, offset + 8)) || node.size > limit - offset - 12)
		return false;
	node.payloadOffset = offset + 12;
	node.payloadEnd = node.payloadOffset + node.size;
	return true;
}

} // namespace

RMD::RMD(SeekableReadStream *stream) {
	if (!stream) {
		_ok = false;
		return;
	}

	const int64 start = stream->pos();
	const int64 end = stream->size();
	if (start < 0 || end < start || (uint64)(end - start) > kMaxInputSize) {
		_ok = false;
		return;
	}

	const uint32 size = (uint32)(end - start);
	if (size != 0) {
		_data.resize(size);
		if (stream->read(_data.data(), size) != size) {
			_ok = false;
			return;
		}
	}

	if (size == 0)
		_ok = false;
	_ok = size != 0 && readSiblings(0, size);
	bool personaSet = false;
	if (size >= 12 && readUint32LE(_data, 0) == 0xF0F000F0 &&
			readUint32LE(_data, 8) == 0x40000000) {
		Node first;
		if (readNode(12, size, first)) {
			clearModel();
			_ok = true;
			_ok = parsePersonaAnimationSet(0, size);
			personaSet = true;
		}
	}
	if (_ok) {
		if (!personaSet)
			parseContent(0, size);
		if (_ok && !validateFrames())
			_ok = false;
		if (_ok && !validateReferences())
			_ok = false;
	}
	if (!_ok)
		clearModel();
}

void RMD::clearModel() {
	_nodes.clear();
	_frames.clear();
	_geometries.clear();
	_materials.clear();
	_textures.clear();
	_atomics.clear();
	_clumps.clear();
	_animations.clear();
	_opaqueNodes.clear();
	_animationSlots.clear();
	_slotOpen = false;
	_recoveryRanges.clear();
}

void RMD::parseContent(uint32 begin, uint32 end) {
	uint32 offset = begin;
	while (offset < end) {
		Node node;
		if (!readNode(offset, end, node)) {
			bool recovered = false;
			for (uint32 i = 0; i < _recoveryRanges.size(); ++i) {
				if (_recoveryRanges[i].start == offset && _recoveryRanges[i].end <= end) {
					offset = _recoveryRanges[i].end;
					recovered = true;
					break;
				}
			}
			if (recovered)
				continue;
			if (offset >= end)
				return;
			_ok = false;
			return;
		}
		parseNode(node);
		if (node.payloadEnd <= offset) {
			_ok = false;
			return;
		}
		offset = nextSiblingOffset(node, end);
	}
}

bool RMD::parsePersonaAnimationSet(uint32 begin, uint32 end) {
	if (end - begin < 12 || readUint32LE(_data, begin) != 0xF0F000F0)
		return false;
	const uint32 count = readUint32LE(_data, begin + 4);
	if (count == 0 || count > 4096)
		return false;
	uint32 offset = begin + 12;
	uint32 animationCount = 0;
	bool terminated = false;
	while (offset < end) {
		Node node;
		if (!readNode(offset, end, node) ||
				(node.type != kPersonaParticleList && !validateChildren(node.payloadOffset, node.payloadEnd, 0)))
			{
			_ok = false;
			return false;
		}
		if (node.type == kPersonaAnimationTerminator) {
			terminated = true;
			parseNode(node);
			offset = nextSiblingOffset(node, end);
			continue;
		}
		if (terminated && node.type != kPersonaAnimationOverride && node.type != kPersonaParticleList)
			return false;
		if (node.type == kPersonaAnimation)
			++animationCount;
		parseNode(node);
		if (!_ok)
			return false;
		offset = nextSiblingOffset(node, end);
	}
	return terminated && animationCount > 0;
}

// Node types that make up a Persona animation slot (Amicitia's RmdScene animation set).
static bool isAnimationSlotNode(uint32 type) {
	return type == kPersonaAnimation || type == kUVAnimationDictionary || type == kPersonaAnimationPlaceholder ||
		type == kPersonaAnimationInstance || type == kPersonaAnimationOverride ||
		type == kPersonaVisibilityAnimation || type == kPersonaParticleAnimation;
}

int32 RMD::animationForSlot(uint32 slot) const {
	// Instances point at another slot; bound the walk in case of a reference cycle.
	for (uint32 hops = 0; slot < _animationSlots.size() && hops <= _animationSlots.size(); ++hops) {
		const AnimationSlot &s = _animationSlots[slot];
		if (s.animation >= 0 || s.instanceOf < 0)
			return s.animation;
		slot = (uint32)s.instanceOf;
	}
	return -1;
}

void RMD::parseNode(const Node &node) {
	if (node.type == kPersonaAnimationTerminator) {
		if (!_slotOpen)
			_animationSlots.push_back(AnimationSlot());
		_slotOpen = false;
		return;
	}
	if (isAnimationSlotNode(node.type)) {
		if (!_slotOpen) {
			_animationSlots.push_back(AnimationSlot());
			_slotOpen = true;
		}
		AnimationSlot &slot = _animationSlots.back();
		if (node.type == kPersonaAnimation) {
			const uint32 before = _animations.size();
			parsePersonaAnimation(node);
			if (_animations.size() > before)
				_animationSlots.back().animation = (int32)_animations.size() - 1;
			return;
		}
		if (node.type == kPersonaAnimationInstance) {
			if (node.size >= 2)
				slot.instanceOf = (int16)readUint16LE(_data, node.payloadOffset);
			return;
		}
		if (node.type == kPersonaAnimationPlaceholder)
			return;
		// Transform override, visibility, UV and particle animation: kept as raw payloads.
		OpaqueNode opaque;
		opaque.type = node.type;
		opaque.payload.resize(node.size);
		for (uint32 i = 0; i < node.size; ++i)
			opaque.payload[i] = _data[node.payloadOffset + i];
		slot.extras.push_back(_opaqueNodes.size());
		_opaqueNodes.push_back(opaque);
		return;
	}
	if (node.type == kFrameList) {
		_nextHAnimFrame = 0;
		_currentHAnimFrame = 0;
		_hierarchy.clear();
		uint32 p = node.payloadOffset;
		Node s;
		if (!readNode(p, node.payloadEnd, s) || s.type != kStruct || s.size < 4) {
			_ok = false;
			return;
		}
		uint32 count = readUint32LE(_data, s.payloadOffset);
		p = s.payloadOffset + 4;
		if (!canRead(p, (uint64)count * 56, s.payloadEnd)) {
			_ok = false;
			return;
		}
		if (s.payloadEnd - p != count * 56) {
			_ok = false;
			return;
		}
		for (uint32 i = 0; i < count; ++i, p += 56) {
			Frame frame;
			for (uint32 j = 0; j < 12; ++j)
				frame.transform[j] = readFloatLE(_data, p + j * 4);
			for (uint32 j = 0; j < 12; ++j)
				if (!std::isfinite(frame.transform[j])) {
					_ok = false;
					return;
				}
			frame.parent = (int32)readUint32LE(_data, p + 48);
			frame.flags = readUint32LE(_data, p + 52);
			_frames.push_back(frame);
		}
		_parsingFrameExtensions = true;
		parseContent(s.payloadEnd, node.payloadEnd);
		_parsingFrameExtensions = false;
		return;
	}

	if (node.type == kGeometryList) {
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kGeometry) {
		uint32 p = node.payloadOffset;
		Node s;
		if (readNode(p, node.payloadEnd, s) && s.type == kStruct && s.size >= 16) {
			const uint32 flags = readUint16LE(_data, s.payloadOffset);
			const uint32 texCount = _data[s.payloadOffset + 2];
			const uint32 triangleCount = readUint32LE(_data, s.payloadOffset + 4);
			const uint32 vertexCount = readUint32LE(_data, s.payloadOffset + 8);
			const uint32 morphCount = readUint32LE(_data, s.payloadOffset + 12);
			Geometry geometry;
			bool valid = true;
			geometry.triangleStrip = (flags & 0x1) != 0;
			geometry.modulateMatColor = (flags & 0x40) != 0;
			geometry.declaredTriangleCount = triangleCount;
			geometry.boundsMin.x = geometry.boundsMin.y = geometry.boundsMin.z = 1e30f;
			geometry.boundsMax.x = geometry.boundsMax.y = geometry.boundsMax.z = -1e30f;
			p = s.payloadOffset + 16;
			if (texCount > kMaxGeometryTexCoordChannels || morphCount > kMaxGeometryMorphTargets)
				valid = false;
			if (flags & 0x8 && canRead(p, (uint64)vertexCount * 4, s.payloadEnd)) {
				for (uint32 i = 0; i < vertexCount; ++i) {
					Color c = { _data[p + i * 4], _data[p + i * 4 + 1], _data[p + i * 4 + 2], _data[p + i * 4 + 3] };
					geometry.colors.push_back(c);
				}
				p += (uint32)((uint64)vertexCount * 4);
			} else if (flags & 0x8) {
				valid = false;
			}
			if (texCount > 0 && !(flags & (0x4 | 0x80)))
				valid = false;
			for (uint32 channelIndex = 0; valid && channelIndex < texCount; ++channelIndex) {
				if (!canRead(p, (uint64)vertexCount * 8, s.payloadEnd)) {
					valid = false;
					break;
				}
				Common::Array<UV> channel;
				for (uint32 i = 0; i < vertexCount; ++i)
					channel.push_back({ readFloatLE(_data, p + i * 8), readFloatLE(_data, p + i * 8 + 4) });
				if (channelIndex == 0)
					geometry.uvs = channel;
				geometry.uvChannels.push_back(channel);
				p += (uint32)((uint64)vertexCount * 8);
			}
			if (canRead(p, (uint64)triangleCount * 8, s.payloadEnd)) {
				uint32 tri = p;
				for (uint32 i = 0; i < triangleCount; ++i, tri += 8) {
					geometry.indices.push_back(readUint16LE(_data, tri));
					geometry.indices.push_back(readUint16LE(_data, tri + 2));
					geometry.indices.push_back(readUint16LE(_data, tri + 6));
					geometry.materialIndices.push_back(readUint16LE(_data, tri + 4));
				}
				p += (uint32)((uint64)triangleCount * 8);
			} else {
				valid = false;
			}
			for (uint32 morph = 0; valid && morph < morphCount; ++morph) {
				if (!canRead(p, 24, s.payloadEnd)) {
					valid = false;
					break;
				}
				p += 24;
				if ((flags & 0x2) && canRead(p, (uint64)vertexCount * 12, s.payloadEnd)) {
					if (morph == 0) {
						for (uint32 i = 0; i < vertexCount; ++i) {
							Vec3 v = { readFloatLE(_data, p + i * 12), readFloatLE(_data, p + i * 12 + 4), readFloatLE(_data, p + i * 12 + 8) };
							geometry.positions.push_back(v);
							if (v.x < geometry.boundsMin.x) geometry.boundsMin.x = v.x;
							if (v.y < geometry.boundsMin.y) geometry.boundsMin.y = v.y;
							if (v.z < geometry.boundsMin.z) geometry.boundsMin.z = v.z;
							if (v.x > geometry.boundsMax.x) geometry.boundsMax.x = v.x;
							if (v.y > geometry.boundsMax.y) geometry.boundsMax.y = v.y;
							if (v.z > geometry.boundsMax.z) geometry.boundsMax.z = v.z;
						}
					}
					p += (uint32)((uint64)vertexCount * 12);
				} else if (flags & 0x2) {
					valid = false;
				}
				if ((flags & 0x10) && canRead(p, (uint64)vertexCount * 12, s.payloadEnd)) {
					if (morph == 0) {
						for (uint32 i = 0; i < vertexCount; ++i)
							geometry.normals.push_back({ readFloatLE(_data, p + i * 12), readFloatLE(_data, p + i * 12 + 4), readFloatLE(_data, p + i * 12 + 8) });
					}
					p += (uint32)((uint64)vertexCount * 12);
				} else if (flags & 0x10) {
					valid = false;
				}
			}
			if (vertexCount && geometry.positions.size() != vertexCount)
				valid = false;
			if (valid)
				_geometries.push_back(geometry);
			else
				_ok = false;
		} else {
			_ok = false;
		}
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kSkin) {
		parseSkin(node);
		return;
	}

	if (node.type == kAnimation) {
		parseAnimation(node);
		return;
	}

	if (node.type == kPersonaParticleList) {
		OpaqueNode opaque;
		opaque.type = node.type;
		opaque.payload.resize(node.size);
		for (uint32 i = 0; i < node.size; ++i)
			opaque.payload[i] = _data[node.payloadOffset + i];
		_opaqueNodes.push_back(opaque);
		return;
	}

	if (node.type == kMaterial) {
		Material material;
		Node s;
		if (readNode(node.payloadOffset, node.payloadEnd, s) && s.type == kStruct && s.size >= 8) {
			material.color = { _data[s.payloadOffset + 6], _data[s.payloadOffset + 5], _data[s.payloadOffset + 4], _data[s.payloadOffset + 7] };
			material.flags = readUint32LE(_data, s.payloadOffset);
			if (s.size >= 28) {
				material.ambient = readFloatLE(_data, s.payloadOffset + 16);
				material.specular = readFloatLE(_data, s.payloadOffset + 20);
				material.diffuse = readFloatLE(_data, s.payloadOffset + 24);
			}
			material.alpha = material.color.a != 255;
		}
		if (!readNode(node.payloadOffset, node.payloadEnd, s) || s.type != kStruct || s.size < 8) {
			_ok = false;
			return;
		}
		_materials.push_back(material);
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kString) {
		return;
	}

	if (node.type == kTextureReference) {
		if (_materials.empty()) {
			_ok = false;
			return;
		}
		Material &material = _materials.back();
		uint32 p = node.payloadOffset;
		Node child;
		if (!readNode(p, node.payloadEnd, child) || child.type != kStruct || child.size < 4) {
			_ok = false;
			return;
		}
		if (child.type == kStruct && child.size >= 4)
			material.textureFlags = readUint32LE(_data, child.payloadOffset);
		uint32 stringIndex = 0;
		while (p < node.payloadEnd) {
			if (!readNode(p, node.payloadEnd, child)) {
				_ok = false;
				break;
			}
		if (child.type == kString) {
				Common::String value;
				for (uint32 i = child.payloadOffset; i < child.payloadEnd && _data[i]; ++i)
					value += (char)_data[i];
				if (stringIndex++ == 0)
					material.textureName = value;
				else if (stringIndex == 2)
					material.maskName = value;
			}
			p = child.payloadEnd;
		}
		return;
	}

	if (node.type == kExtension) {
		if (_parsingFrameExtensions) {
			if (_nextHAnimFrame >= _frames.size()) {
				_ok = false;
				return;
			}
			_currentHAnimFrame = _nextHAnimFrame++;
		}
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kHAnimFrameExtension) {
		if (node.size == 0 && (node.version == 0x09 || node.version == 0x10))
			return;
		if (_frames.empty() || node.size < 12 || _currentHAnimFrame >= _frames.size()) {
			_ok = false;
			return;
		}
		Frame &frame = _frames[_currentHAnimFrame];
		if (readUint32LE(_data, node.payloadOffset) != 0x100) {
			_ok = false;
			return;
		}
		frame.nameId = readUint32LE(_data, node.payloadOffset + 4);
		frame.hasHAnim = true;
		const uint32 count = readUint32LE(_data, node.payloadOffset + 8);
		if (count == 0) {
			if (node.size != 12) {
				_ok = false;
				return;
			}
			return;
		}
		const uint32 records = node.payloadOffset + 20;
		if (node.size != 20 + (uint64)count * 12 || !canRead(records, (uint64)count * 12, node.payloadEnd)) {
			_ok = false;
			return;
		}
		_hierarchy.clear();
		for (uint32 i = 0; i < count; ++i) {
			HierarchyNode hierarchy;
			hierarchy.nodeId = readUint32LE(_data, records + i * 12);
			hierarchy.index = readUint32LE(_data, records + i * 12 + 4);
			hierarchy.flags = readUint32LE(_data, records + i * 12 + 8);
			_hierarchy.push_back(hierarchy);
		}
		return;
	}

	if (node.type == kTextureDictionary) {
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kMaterialList) {
		Node s;
		if (_geometries.empty() || !readNode(node.payloadOffset, node.payloadEnd, s) || s.type != kStruct || s.size < 4) {
			_ok = false;
			return;
		}
		if (s.type == kStruct && s.size >= 4) {
			const uint32 count = readUint32LE(_data, s.payloadOffset);
			if (canRead(s.payloadOffset + 4, (uint64)count * 4, s.payloadEnd)) {
				Geometry &geometry = _geometries.back();
				const uint32 materialBase = _materials.size();
				geometry.declaredMaterialCount = count;
				geometry.materialMap.clear();
				for (uint32 i = 0; i < count; ++i) {
					const uint32 reference = readUint32LE(_data, s.payloadOffset + 4 + i * 4);
					geometry.materialMap.push_back(reference == 0xFFFFFFFF ? materialBase + i : reference);
				}
			} else {
				_ok = false;
			}
		}
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kMeshList) {
		uint32 p = node.payloadOffset;
		if (_geometries.empty() || !canRead(p, 12, node.payloadEnd)) {
			_ok = false;
			return;
		}
		Geometry &geometry = _geometries.back();
		const uint32 serialNumber = readUint32LE(_data, p);
		if (serialNumber <= 1)
			geometry.triangleStrip = serialNumber == 1;
		const uint32 splitCount = readUint32LE(_data, p + 4);
		p += 12;
		geometry.indices.clear();
		geometry.materialIndices.clear();
		for (uint32 split = 0; split < splitCount; ++split) {
			if (!canRead(p, 8, node.payloadEnd)) {
				_ok = false;
				return;
			}
			const uint32 indexCount = readUint32LE(_data, p);
			const uint32 splitMaterial = readUint32LE(_data, p + 4);
			const uint32 mappedMaterial = splitMaterial < geometry.materialMap.size() ? geometry.materialMap[splitMaterial] : splitMaterial;
			const uint32 material = mappedMaterial == 0xFFFFFFFF ? splitMaterial : mappedMaterial;
			p += 8;
			if (!canRead(p, (uint64)indexCount * 4, node.payloadEnd)) {
				_ok = false;
				return;
			}
			if (geometry.triangleStrip) {
				for (uint32 i = 2; i < indexCount; ++i) {
					const uint32 a = readUint32LE(_data, p + (i - 2) * 4);
					const uint32 b = readUint32LE(_data, p + (i - 1) * 4);
					const uint32 c = readUint32LE(_data, p + i * 4);
					if (i & 1) { geometry.indices.push_back(b); geometry.indices.push_back(a); }
					else { geometry.indices.push_back(a); geometry.indices.push_back(b); }
					geometry.indices.push_back(c);
					geometry.materialIndices.push_back(material);
				}
			} else {
				for (uint32 i = 0; i + 2 < indexCount; i += 3) {
					geometry.indices.push_back(readUint32LE(_data, p + i * 4));
					geometry.indices.push_back(readUint32LE(_data, p + (i + 1) * 4));
					geometry.indices.push_back(readUint32LE(_data, p + (i + 2) * 4));
					geometry.materialIndices.push_back(material);
				}
			}
			p += (uint32)((uint64)indexCount * 4);
		}
		if (p != node.payloadEnd)
			_ok = false;
		return;
	}

	if (node.type == kTextureNative) {
		Texture texture;
		_textures.push_back(texture);
		texture.nativeData.resize(node.size);
		for (uint32 i = 0; i < node.size; ++i)
			texture.nativeData[i] = _data[node.payloadOffset + i];
		uint32 p = node.payloadOffset;
		uint32 stringIndex = 0;
		while (p < node.payloadEnd) {
			Node child;
			if (!readNode(p, node.payloadEnd, child)) {
				_ok = false;
				break;
			}
			if (p == node.payloadOffset && child.type == kStruct && child.size >= 8) {
				const uint32 flags = readUint32LE(_data, child.payloadOffset + 4);
				_textures.back().horizontalAddressing = (byte)((flags >> 8) & 0xF);
				_textures.back().verticalAddressing = (byte)((flags >> 12) & 0xF);
			} else if (child.type == kString) {
				Common::String value;
				for (uint32 i = child.payloadOffset; i < child.payloadEnd && _data[i]; ++i)
					value += (char)_data[i];
				if (stringIndex++ == 0)
					_textures.back().name = value;
				else if (stringIndex == 2)
					_textures.back().maskName = value;
			}
			p = child.payloadEnd;
		}
		_textures.back().nativeData = texture.nativeData;
		return;
	}

	if (node.type == kAtomic) {
		Atomic atomic;
		Node s;
		if (readNode(node.payloadOffset, node.payloadEnd, s) && s.type == kStruct && s.size >= 12) {
			atomic.frame = (int32)readUint32LE(_data, s.payloadOffset);
			atomic.geometry = (int32)readUint32LE(_data, s.payloadOffset + 4);
			atomic.flags = readUint32LE(_data, s.payloadOffset + 8);
		}
		if (!readNode(node.payloadOffset, node.payloadEnd, s) || s.type != kStruct || s.size < 12) {
			_ok = false;
			return;
		}
		_atomics.push_back(atomic);
		parseContent(node.payloadOffset, node.payloadEnd);
		return;
	}

	if (node.type == kClump) {
		Clump clump;
		const uint32 frameStart = _frames.size();
		const uint32 geometryStart = _geometries.size();
		const uint32 atomicStart = _atomics.size();
		parseContent(node.payloadOffset, node.payloadEnd);
		for (uint32 i = frameStart; i < _frames.size(); ++i) clump.frames.push_back(i);
		for (uint32 i = geometryStart; i < _geometries.size(); ++i) clump.geometries.push_back(i);
		for (uint32 i = atomicStart; i < _atomics.size(); ++i) clump.atomics.push_back(i);
		_clumps.push_back(clump);
	}
}

void RMD::parseSkin(const Node &node) {
	if (_geometries.empty()) {
		_ok = false;
		return;
	}
	if (node.size < 4) {
		_ok = false;
		return;
	}
	Geometry &geometry = _geometries.back();
	const uint32 vertexCount = geometry.positions.size();
	const uint32 p = node.payloadOffset;
	const uint32 numBones = _data[p];
	const uint32 usedCount = _data[p + 1];
	const uint32 maxWeights = _data[p + 2];
	if (numBones == 0 || numBones > _frames.size() || usedCount > numBones || maxWeights == 0 || maxWeights > 4) {
		_ok = false;
		return;
	}
	uint64 offset = p + 4;
	const uint64 indicesBytes = (uint64)vertexCount * 4;
	const uint64 weightsBytes = (uint64)vertexCount * 16;
	const uint64 matricesBytes = (uint64)numBones * 64;
	if (!canRead((uint32)offset, (uint64)(usedCount ? usedCount : numBones * 4) + indicesBytes + weightsBytes + matricesBytes, node.payloadEnd)) {
		_ok = false;
		return;
	}
	Skin skin;
	skin.boneCount = numBones;
	skin.usedBoneCount = usedCount;
	skin.maxWeightsPerVertex = maxWeights;
	if (usedCount) {
		for (uint32 i = 0; i < usedCount; ++i)
			skin.usedBones.push_back(_data[offset++]);
	} else {
		for (uint32 i = 0; i < numBones; ++i) {
			if (readUint32LE(_data, (uint32)offset) != 0xDEADDEAD) {
				_ok = false;
				return;
			}
			offset += 4;
			skin.usedBones.push_back(i);
		}
	}
	for (uint32 i = 0; i < usedCount; ++i)
		if (skin.usedBones[i] >= _frames.size()) {
			_ok = false;
			return;
		}
	for (uint32 vertex = 0; vertex < vertexCount; ++vertex) {
		SkinInfluence influence;
		for (uint32 i = 0; i < 4; ++i)
			influence.indices[i] = _data[offset++];
		skin.influences.push_back(influence);
	}
	for (uint32 vertex = 0; vertex < vertexCount; ++vertex) {
		SkinInfluence &influence = skin.influences[vertex];
		uint32 weightBits[4];
		bool integerWeights = true;
		for (uint32 i = 0; i < 4; ++i) {
			weightBits[i] = readUint32LE(_data, (uint32)offset + i * 4);
			integerWeights = integerWeights && weightBits[i] <= 255;
		}
		for (uint32 i = 0; i < 4; ++i)
			influence.weights[i] = integerWeights ? (float)weightBits[i] : readFloatLE(_data, (uint32)offset + i * 4);
		float sum = influence.weights[0] + influence.weights[1] + influence.weights[2] + influence.weights[3];
		if (!std::isfinite(sum) || sum <= 0.0f) {
			_ok = false;
			return;
		}
		for (uint32 i = 0; i < 4; ++i) {
			if (!std::isfinite(influence.weights[i]) || influence.weights[i] < 0.0f ||
					(influence.weights[i] > 0.0f && influence.indices[i] >= numBones)) {
				_ok = false;
				return;
			}
			influence.weights[i] /= sum;
		}
		offset += 16;
	}
	for (uint32 bone = 0; bone < numBones; ++bone) {
		Matrix matrix = {};
		for (uint32 row = 0; row < 4; ++row) {
			for (uint32 column = 0; column < 3; ++column) {
				const float value = readFloatLE(_data, (uint32)offset + (row * 4 + column) * 4);
				if (!std::isfinite(value)) {
					_ok = false;
					return;
				}
				if (row < 3)
					matrix.m[row * 4 + column] = value;
				else
					matrix.m[12 + column] = value;
			}
			const float padding = readFloatLE(_data, (uint32)offset + (row * 4 + 3) * 4);
			(void)padding;
		}
		skin.inverseBindMatrices.push_back(matrix);
		offset += 64;
	}
	for (uint32 i = 0; i < numBones; ++i) {
		const int32 frame = frameForHierarchy(i);
		if (frame < 0) {
			_ok = false;
			return;
		}
		skin.rendererBoneFrames.push_back((uint32)frame);
	}
	const uint64 trailerBytes = node.payloadEnd - offset;
	if (trailerBytes != 0 || usedCount) {
		if (trailerBytes < 12) {
			_ok = false;
			return;
		}
		skin.boneLimit = readUint32LE(_data, (uint32)offset);
		skin.meshCount = readUint32LE(_data, (uint32)offset + 4);
		skin.rleSize = readUint32LE(_data, (uint32)offset + 8);
		if ((skin.meshCount && (skin.boneLimit == 0 || skin.boneLimit > numBones)) || (!skin.meshCount && skin.rleSize) || skin.meshCount > 4096 || skin.rleSize > 65535) {
			_ok = false;
			return;
		}
		offset += 12;
		const uint64 splitBytes = skin.meshCount ? (uint64)numBones + 2 * ((uint64)skin.meshCount + skin.rleSize) : 0;
		if (!canRead((uint32)offset, splitBytes, node.payloadEnd) || offset + splitBytes != node.payloadEnd) {
			_ok = false;
			return;
		}
		for (uint32 i = 0; i < (skin.meshCount ? numBones : 0); ++i)
			skin.meshBoneRemap.push_back(_data[offset++]);
		for (uint32 i = 0; i < skin.meshCount; ++i) {
			const byte start = _data[offset++];
			const byte count = _data[offset++];
			if (!count || (uint32)start + count > skin.rleSize || count > skin.boneLimit) {
				_ok = false;
				return;
			}
			skin.meshRLECounts.push_back((uint16)((start << 8) | count));
		}
		for (uint32 i = 0; i < skin.rleSize; ++i) {
			const byte start = _data[offset++];
			const byte count = _data[offset++];
			if (!count || (uint32)start + count > numBones || count > skin.boneLimit) {
				_ok = false;
				return;
			}
			skin.rle.push_back((uint16)((start << 8) | count));
		}
		for (uint32 i = 0; i < skin.meshBoneRemap.size(); ++i)
			if (skin.meshBoneRemap[i] != 0xFF && skin.meshBoneRemap[i] >= numBones) {
				_ok = false;
				return;
			}
		for (uint32 mesh = 0; mesh < skin.meshRLECounts.size(); ++mesh) {
			const uint32 start = skin.meshRLECounts[mesh] >> 8;
			const uint32 count = skin.meshRLECounts[mesh] & 0xFF;
			uint32 expanded = 0;
			for (uint32 i = 0; i < count; ++i)
				expanded += skin.rle[start + i] & 0xFF;
			if (!expanded || expanded > skin.boneLimit) {
				_ok = false;
				return;
			}
		}
	}
	geometry.skin = skin;
	geometry.skinned = true;
}

void RMD::parsePersonaAnimation(const Node &node) {
	if (node.size >= 20 && readUint32LE(_data, node.payloadOffset) == 0x100 &&
			readUint32LE(_data, node.payloadOffset + 8) > 0 &&
			node.size == 20 + (uint64)readUint32LE(_data, node.payloadOffset + 8) * 52) {
		const uint32 p = node.payloadOffset;
		const uint32 keyCount = readUint32LE(_data, p + 8);
		const float duration = readFloatLE(_data, p + 16);
		if (readUint32LE(_data, p + 4) != 0x44A07195 || readUint32LE(_data, p + 12) != 0 ||
				keyCount > 100000 || !std::isfinite(duration)) {
			_ok = false;
			return;
		}

		Animation animation;
		animation.keyframeCount = keyCount;
		animation.duration = duration;
		Common::Array<AnimationKey> keys;
		Common::Array<uint32> previous(keyCount, 0xFFFFFFFF);
		Common::Array<uint32> roots;
		bool haveRootSentinel = false;
		uint32 rootSentinel = 0;
		bool valid = true;
		for (uint32 i = 0; i < keyCount && valid; ++i) {
			const uint32 offset = p + 20 + i * 52;
			AnimationKey key;
			key.time = readFloatLE(_data, offset);
			// P3 FES uses this exact non-finite quartet for translation-only keys.
			const bool noRotation = readUint32LE(_data, offset + 4) == 0xFFC00000 &&
					readUint32LE(_data, offset + 8) == 0xFFC00000 &&
					readUint32LE(_data, offset + 12) == 0x5F1CC471 &&
					readUint32LE(_data, offset + 16) == 0xFFC00000;
			if (noRotation) {
				// This sentinel means "no rotation change from this bone's own bind pose",
				// not "reset to a world-identity rotation" -- the caller substitutes the
				// bone's bind rotation for these keys (see hasRotation in evaluateAnimation).
				key.rotation.x = 0;
				key.rotation.y = 0;
				key.rotation.z = 0;
				key.rotation.w = 1;
				key.hasRotation = false;
			} else {
				key.rotation.x = readFloatLE(_data, offset + 4);
				key.rotation.y = readFloatLE(_data, offset + 8);
				key.rotation.z = readFloatLE(_data, offset + 12);
				key.rotation.w = readFloatLE(_data, offset + 16);
			}
			key.translation.x = readFloatLE(_data, offset + 20);
			key.translation.y = readFloatLE(_data, offset + 24);
			key.translation.z = readFloatLE(_data, offset + 28);
			// The four words at +32 are present in every verified record but are not
			// part of the current pose API, so keep them opaque.
			const uint32 prev = readUint32LE(_data, offset + 48);
			const bool hasPrevious = prev % 52 == 0 && prev < keyCount * 52;
			if (!std::isfinite(key.time) || key.time < 0 || key.time > duration ||
					!std::isfinite(key.rotation.x) || !std::isfinite(key.rotation.y) ||
					!std::isfinite(key.rotation.z) || !std::isfinite(key.rotation.w) ||
					!std::isfinite(key.translation.x) || !std::isfinite(key.translation.y) ||
					!std::isfinite(key.translation.z)) {
				valid = false;
				break;
			}
			if (hasPrevious) {
				previous[i] = prev / 52;
				if (previous[i] >= i) {
					valid = false;
					break;
				}
			} else {
				if (key.time != 0.0f || (haveRootSentinel && rootSentinel != prev)) {
					valid = false;
					break;
				}
				if (!haveRootSentinel) {
					rootSentinel = prev;
					haveRootSentinel = true;
				}
				roots.push_back(i);
			}
			keys.push_back(key);
		}
		if (!valid || roots.empty() || roots.size() > _frames.size()) {
			_ok = false;
			return;
		}
		for (uint32 i = 0; i < keyCount && valid; ++i) {
			uint32 root = i;
			Common::Array<byte> seen(keyCount, 0);
			while (previous[root] != 0xFFFFFFFF) {
				if (seen[root]) {
					valid = false;
					break;
				}
				seen[root] = 1;
				root = previous[root];
			}
			uint32 track = 0;
			while (track < roots.size() && roots[track] != root)
				++track;
			if (!valid || track == roots.size()) {
				valid = false;
				break;
			}
			if (animation.tracks.size() <= track) {
				animation.tracks.resize(track + 1);
				animation.tracks[track].bone = track;
			}
			animation.tracks[track].keys.push_back(keys[i]);
		}
		if (!valid) {
			_ok = false;
			return;
		}
		for (uint32 i = 0; i < animation.tracks.size(); ++i)
			for (uint32 a = 0; a + 1 < animation.tracks[i].keys.size(); ++a)
				for (uint32 b = a + 1; b < animation.tracks[i].keys.size(); ++b)
					if (animation.tracks[i].keys[b].time < animation.tracks[i].keys[a].time) {
						AnimationKey key = animation.tracks[i].keys[a];
						animation.tracks[i].keys[a] = animation.tracks[i].keys[b];
						animation.tracks[i].keys[b] = key;
					}
		_animations.push_back(animation);
		return;
	}
	if (node.size < 44) {
		_ok = false;
		return;
	}
	const uint32 p = node.payloadOffset;
	const uint32 trackCount = readUint32LE(_data, p + 4);
	const uint32 keyCount = readUint32LE(_data, p + 8);
	Animation animation;
	animation.keyframeCount = keyCount;
	animation.duration = readFloatLE(_data, p + 16);
	if (readUint32LE(_data, p) != 0x100 || trackCount == 0 || (trackCount != 2 && trackCount > _frames.size()) ||
			keyCount == 0 || keyCount > 100000 || !std::isfinite(animation.duration)) {
		_ok = false;
		return;
	}

	if (readUint32LE(_data, p + 4) == 2) {
		animation.compressed = true;
		const uint32 keyOffset = p + 20;
		const uint64 packedBytes = (uint64)keyCount * 22;
		const uint32 metadataOffset = keyOffset + (uint32)packedBytes;
		bool valid = canRead(keyOffset, packedBytes + 24, node.payloadEnd) &&
			metadataOffset + 24 == node.payloadEnd;
		Common::Array<AnimationKey> keys;
		Common::Array<uint32> previous;
		if (valid) {
			const RMD::Vec3 offset = { readFloatLE(_data, metadataOffset), readFloatLE(_data, metadataOffset + 4), readFloatLE(_data, metadataOffset + 8) };
			const RMD::Vec3 scalar = { readFloatLE(_data, metadataOffset + 12), readFloatLE(_data, metadataOffset + 16), readFloatLE(_data, metadataOffset + 20) };
			for (uint32 i = 0; i < keyCount && valid; ++i) {
				const uint32 keyData = keyOffset + i * 22;
				AnimationKey key;
				key.time = readFloatLE(_data, keyData);
				key.rotation.x = decodePersonaPackedFloat(readUint16LE(_data, keyData + 4));
				key.rotation.y = decodePersonaPackedFloat(readUint16LE(_data, keyData + 6));
				key.rotation.z = decodePersonaPackedFloat(readUint16LE(_data, keyData + 8));
				key.rotation.w = decodePersonaPackedFloat(readUint16LE(_data, keyData + 10));
				const float tx = decodePersonaPackedFloat(readUint16LE(_data, keyData + 12));
				const float ty = decodePersonaPackedFloat(readUint16LE(_data, keyData + 14));
				const float tz = decodePersonaPackedFloat(readUint16LE(_data, keyData + 16));
				key.translation.x = tx * scalar.x + offset.x;
				key.translation.y = ty * scalar.y + offset.y;
				key.translation.z = tz * scalar.z + offset.z;
				const uint32 prev = readUint32LE(_data, keyData + 18);
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z) ||
						!std::isfinite(scalar.x) || !std::isfinite(scalar.y) || !std::isfinite(scalar.z) ||
						fabsf(offset.x) > 1000000.0f || fabsf(offset.y) > 1000000.0f || fabsf(offset.z) > 1000000.0f ||
						fabsf(scalar.x) > 1000000.0f || fabsf(scalar.y) > 1000000.0f || fabsf(scalar.z) > 1000000.0f ||
						!std::isfinite(key.time) || key.time < 0 || key.time > animation.duration ||
						!std::isfinite(key.rotation.x) || !std::isfinite(key.rotation.y) ||
						!std::isfinite(key.rotation.z) || !std::isfinite(key.rotation.w) ||
						fabsf(key.rotation.x) > 1.0f || fabsf(key.rotation.y) > 1.0f ||
						fabsf(key.rotation.z) > 1.0f || fabsf(key.rotation.w) > 1.0f ||
						// No +-1 bound on tx/ty/tz: an axis with scalar 0 stores filler (decodes to -2/-1).
						!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
						!std::isfinite(key.translation.x) || !std::isfinite(key.translation.y) ||
						!std::isfinite(key.translation.z) || fabsf(key.translation.x) > 1000000.0f ||
						fabsf(key.translation.y) > 1000000.0f || fabsf(key.translation.z) > 1000000.0f) {
					valid = false;
					break;
				}
				keys.push_back(key);
				previous.push_back(prev);
			}
		}
		if (valid) {
			Common::Array<uint32> roots;
			for (uint32 i = 0; i < keyCount && valid; ++i) {
				uint32 root = i;
				Common::Array<byte> seen(keyCount, 0);
				while (keys[root].time != 0.0f) {
					if (seen[root] || previous[root] % 24 || previous[root] / 24 >= i || previous[root] / 24 >= keyCount) {
						valid = false;
						break;
					}
					seen[root] = 1;
					root = previous[root] / 24;
				}
				if (!valid)
					break;
				uint32 track = 0;
				while (track < roots.size() && roots[track] != root)
					++track;
				if (track == roots.size()) {
					if (roots.size() >= _frames.size()) {
						valid = false;
						break;
					}
					roots.push_back(root);
					animation.tracks.push_back(AnimationTrack());
					animation.tracks.back().bone = track;
				}
				animation.tracks[track].keys.push_back(keys[i]);
			}
		}
		if (valid && !animation.tracks.empty()) {
			_animations.push_back(animation);
			return;
		}
		animation.supported = false;
		animation.unsupportedReason = "Persona 0x1B packed animation variant is unsupported";
		_animations.push_back(animation);
		return;
	}

	const uint32 compactKeyOffset = p + 20;
	const uint32 keyOffset = node.payloadEnd - compactKeyOffset == (uint64)keyCount * 36 ? compactKeyOffset : p + 44;
	const uint64 remaining = node.payloadEnd - keyOffset;
	if (remaining == (uint64)keyCount * 22) {
		// Persona's packed records need quantization metadata not represented by
		// the public pose API. Keep the animation visible without guessing values.
		animation.compressed = true;
		animation.supported = false;
		animation.unsupportedReason = "Persona 0x1B packed 22-byte keys are unsupported";
		_animations.push_back(animation);
		return;
	}
	if (remaining != (uint64)keyCount * 36) {
		_ok = false;
		return;
	}

	bool implicitBone = trackCount == 1;
	if (implicitBone) {
		implicitBone = false;
		for (uint32 i = 0; i < keyCount; ++i)
			if (readUint32LE(_data, keyOffset + i * 36) != 0) {
				implicitBone = true;
				break;
			}
	}
	for (uint32 i = 0; i < keyCount; ++i) {
		const uint32 offset = keyOffset + i * 36;
		AnimationKey key;
		const uint32 bone = implicitBone ? 0 : readUint32LE(_data, offset);
		const uint32 base = implicitBone ? offset : offset + 4;
		key.time = readFloatLE(_data, base);
		key.translation.x = readFloatLE(_data, base + 4);
		key.translation.y = readFloatLE(_data, base + 8);
		key.translation.z = readFloatLE(_data, base + 12);
		key.rotation.x = readFloatLE(_data, base + 16);
		key.rotation.y = readFloatLE(_data, base + 20);
		key.rotation.z = readFloatLE(_data, base + 24);
		key.rotation.w = readFloatLE(_data, base + 28);
		if (bone >= trackCount || !std::isfinite(key.time) || !std::isfinite(key.translation.x) ||
				!std::isfinite(key.translation.y) || !std::isfinite(key.translation.z) ||
				!std::isfinite(key.rotation.x) || !std::isfinite(key.rotation.y) ||
				!std::isfinite(key.rotation.z) || !std::isfinite(key.rotation.w)) {
			_ok = false;
			return;
		}
		while (animation.tracks.size() <= bone)
			animation.tracks.push_back(AnimationTrack());
		animation.tracks[bone].bone = bone;
		animation.tracks[bone].keys.push_back(key);
	}
	if (animation.tracks.size() != trackCount) {
		_ok = false;
		return;
	}
	_animations.push_back(animation);
}

void RMD::parseAnimation(const Node &node) {
	if (node.size < 12) {
		_ok = false;
		return;
	}
	uint32 offset = node.payloadOffset;
	Animation animation;
	// RtAnim's standard HAnim stream starts with type, frame count, flags and duration.
	if (node.size >= 16 && readUint32LE(_data, offset) == 1 &&
			readUint32LE(_data, offset + 4) <= 100000 &&
			canRead(offset + 16, (uint64)readUint32LE(_data, offset + 4) * 40, node.payloadEnd)) {
		const uint32 frameCount = readUint32LE(_data, offset + 4);
		animation.keyframeCount = frameCount;
		animation.duration = readFloatLE(_data, offset + 12);
		offset += 16;
		if (frameCount == 0 || frameCount > 100000 || !std::isfinite(animation.duration) ||
				!canRead(offset, (uint64)frameCount * 40, node.payloadEnd) ||
				offset + (uint64)frameCount * 40 != node.payloadEnd) {
			_ok = false;
			return;
		}
		Common::Array<int32> previous;
		Common::Array<uint32> roots;
		previous.resize(frameCount);
		for (uint32 i = 0; i < frameCount; ++i) {
			AnimationKey key;
			key.time = readFloatLE(_data, offset);
			key.rotation.x = readFloatLE(_data, offset + 4); key.rotation.y = readFloatLE(_data, offset + 8);
			key.rotation.z = readFloatLE(_data, offset + 12); key.rotation.w = readFloatLE(_data, offset + 16);
			key.translation.x = readFloatLE(_data, offset + 20); key.translation.y = readFloatLE(_data, offset + 24);
			key.translation.z = readFloatLE(_data, offset + 28);
			const int32 prev = (int32)readUint32LE(_data, offset + 32);
			if (!std::isfinite(key.time) || !std::isfinite(key.rotation.x) || !std::isfinite(key.rotation.y) ||
					!std::isfinite(key.rotation.z) || !std::isfinite(key.rotation.w) || !std::isfinite(key.translation.x) ||
					!std::isfinite(key.translation.y) || !std::isfinite(key.translation.z) ||
					(prev != -1 && (prev < 0 || (uint32)prev % 40 != 0 || (uint32)prev >= frameCount * 40)))
			{
				_ok = false;
				return;
			}
			previous[i] = prev == -1 ? -1 : prev / 40;
			offset += 40;
		}
		for (uint32 i = 0; i < frameCount; ++i) {
			uint32 root = i;
			Common::Array<byte> seen(frameCount, 0);
			while (previous[root] >= 0) {
				if (seen[root]) {
					_ok = false;
					return;
				}
				seen[root] = 1;
				root = previous[root];
			}
			uint32 trackIndex = 0;
			while (trackIndex < roots.size() && roots[trackIndex] != root) ++trackIndex;
			if (trackIndex == roots.size()) {
				if (roots.size() >= _frames.size()) {
					_ok = false;
					return;
				}
				roots.push_back(root);
				animation.tracks.push_back(AnimationTrack());
				animation.tracks.back().bone = trackIndex;
			}
			AnimationKey key;
			const uint32 keyOffset = offset - (frameCount - i) * 40;
			key.time = readFloatLE(_data, keyOffset);
			key.rotation.x = readFloatLE(_data, keyOffset + 4); key.rotation.y = readFloatLE(_data, keyOffset + 8);
			key.rotation.z = readFloatLE(_data, keyOffset + 12); key.rotation.w = readFloatLE(_data, keyOffset + 16);
			key.translation.x = readFloatLE(_data, keyOffset + 20); key.translation.y = readFloatLE(_data, keyOffset + 24); key.translation.z = readFloatLE(_data, keyOffset + 28);
			animation.tracks[trackIndex].keys.push_back(key);
		}
		for (uint32 i = 0; i < animation.tracks.size(); ++i)
			for (uint32 a = 0; a + 1 < animation.tracks[i].keys.size(); ++a)
				for (uint32 b = a + 1; b < animation.tracks[i].keys.size(); ++b)
					if (animation.tracks[i].keys[b].time < animation.tracks[i].keys[a].time) {
						AnimationKey key = animation.tracks[i].keys[a]; animation.tracks[i].keys[a] = animation.tracks[i].keys[b]; animation.tracks[i].keys[b] = key;
					}
		_animations.push_back(animation);
		return;
	}
	const uint32 boneCount = readUint32LE(_data, offset);
	animation.keyframeCount = readUint32LE(_data, offset + 4);
	animation.duration = readFloatLE(_data, offset + 8);
	offset += 12;
	if (boneCount == 0 || boneCount > 4096 || animation.keyframeCount > 100000 || !std::isfinite(animation.duration)) {
		_ok = false;
		return;
	}
	for (uint32 trackIndex = 0; trackIndex < boneCount && offset < node.payloadEnd; ++trackIndex) {
		if (!canRead(offset, 8, node.payloadEnd)) {
			_ok = false;
			return;
		}
		AnimationTrack track;
		track.bone = readUint32LE(_data, offset);
		const uint32 keyCount = readUint32LE(_data, offset + 4);
		offset += 8;
		const bool compressed = !canRead(offset, (uint64)keyCount * 32, node.payloadEnd) && canRead(offset, (uint64)keyCount * 20, node.payloadEnd);
		if (track.bone >= _frames.size() || keyCount > animation.keyframeCount ||
				(!canRead(offset, (uint64)keyCount * 32, node.payloadEnd) && !compressed)) {
			_ok = false;
			return;
		}
		animation.compressed |= compressed;
		track.compressed = compressed;
		for (uint32 keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
			AnimationKey key;
			key.time = readFloatLE(_data, offset);
			if (compressed) {
				key.translation.x = (int16)readUint16LE(_data, offset + 4) / 32767.0f;
				key.translation.y = (int16)readUint16LE(_data, offset + 6) / 32767.0f;
				key.translation.z = (int16)readUint16LE(_data, offset + 8) / 32767.0f;
				key.rotation.x = (int16)readUint16LE(_data, offset + 10) / 32767.0f;
				key.rotation.y = (int16)readUint16LE(_data, offset + 12) / 32767.0f;
				key.rotation.z = (int16)readUint16LE(_data, offset + 14) / 32767.0f;
				key.rotation.w = (int16)readUint16LE(_data, offset + 16) / 32767.0f;
				offset += 20;
			} else {
				key.translation.x = readFloatLE(_data, offset + 4); key.translation.y = readFloatLE(_data, offset + 8); key.translation.z = readFloatLE(_data, offset + 12);
				key.rotation.x = readFloatLE(_data, offset + 16); key.rotation.y = readFloatLE(_data, offset + 20);
				key.rotation.z = readFloatLE(_data, offset + 24); key.rotation.w = readFloatLE(_data, offset + 28);
				offset += 32;
			}
			if (!std::isfinite(key.time) || !std::isfinite(key.translation.x) || !std::isfinite(key.translation.y) ||
					!std::isfinite(key.translation.z) || !std::isfinite(key.rotation.x) || !std::isfinite(key.rotation.y) ||
					!std::isfinite(key.rotation.z) || !std::isfinite(key.rotation.w))
			{
				_ok = false;
				return;
			}
			track.keys.push_back(key);
		}
		animation.tracks.push_back(track);
	}
	if (animation.tracks.size() != boneCount || offset != node.payloadEnd)
		_ok = false;
	else
		_animations.push_back(animation);
}

bool RMD::evaluateAnimation(uint32 animationIndex, float time, BonePose &pose) const {
	if (animationIndex >= _animations.size())
		return false;
	const Animation &animation = _animations[animationIndex];
	if (!animation.supported)
		return false;
	const uint32 count = _frames.size();
	pose.localTransforms.clear(); pose.worldTransforms.clear(); pose.skinTransforms.clear();
	pose.localTransforms.resize(count); pose.worldTransforms.resize(count); pose.skinTransforms.resize(count);
	for (uint32 i = 0; i < count; ++i)
		pose.localTransforms[i] = frameMatrix(_frames[i]);
	const float sampleTime = CLIP(time, 0.0f, animation.duration);
	for (uint32 i = 0; i < animation.tracks.size(); ++i) {
		const AnimationTrack &track = animation.tracks[i];
		if (track.keys.empty() || track.bone >= count)
			continue;
		const AnimationKey *first = &track.keys[0], *last = &track.keys[track.keys.size() - 1];
		AnimationKey sample = *first;
		if (sampleTime >= last->time) sample = *last;
		else if (sampleTime > first->time) {
			for (uint32 k = 0; k + 1 < track.keys.size(); ++k) {
				const AnimationKey &a = track.keys[k], &b = track.keys[k + 1];
				if (sampleTime <= b.time) {
					const float alpha = b.time > a.time ? (sampleTime - a.time) / (b.time - a.time) : 0;
					sample.translation.x = a.translation.x + alpha * (b.translation.x - a.translation.x);
					sample.translation.y = a.translation.y + alpha * (b.translation.y - a.translation.y);
					sample.translation.z = a.translation.z + alpha * (b.translation.z - a.translation.z);
					// A "no rotation" key means "keep this bone's existing rotation", not
					// "this bone's rotation is literally identity" -- when only one side of
					// the interpolation carries real rotation data, use that side's rotation
					// outright rather than slerping it against a meaningless placeholder.
					if (a.hasRotation && b.hasRotation) sample.rotation = slerp(a.rotation, b.rotation, alpha);
					else if (a.hasRotation) sample.rotation = a.rotation;
					else if (b.hasRotation) sample.rotation = b.rotation;
					sample.hasRotation = a.hasRotation || b.hasRotation;
					break;
				}
			}
		}
		const int32 frame = frameForHierarchy(track.bone);
		if (frame < 0)
			continue;
		const Matrix bindLocalForFrame = frameMatrix(_frames[frame]);
		// Some bind poses carry a deliberate (possibly non-uniform, but always axis-aligned
		// -- confirmed the three rows stay mutually orthogonal, just with different lengths)
		// scale on a bone specifically to compensate an ancestor's own bind-pose scale
		// (confirmed: one file's root bone scales by 0.96 per axis, and a descendant bone's
		// bind scales by exactly 1/0.96 to cancel it back out by the time that subtree is
		// reached; another file's compensation bone scales each axis by a different amount).
		// poseMatrix() builds a pure unit-scale rotation from the track's quaternion, so
		// animating a bone that carries this kind of compensating scale silently drops it --
		// every bone in that subtree then inherits the ancestor's uncompensated scale,
		// shrinking or growing the whole limb rigidly while still rotating correctly
		// (confirmed: bone-to-parent world distance changes by exactly the dropped scale
		// factor, and reapplying each row's own bind length, not a single shared scalar,
		// restores the original length exactly).
		Matrix animLocal;
		if (!sample.hasRotation) {
			// "No rotation" means "unchanged from this bone's own bind rotation", not a
			// world-identity rotation -- keep the bind rotation (and whatever scale it
			// carries) and only move the translation.
			animLocal = bindLocalForFrame;
			animLocal.m[12] = sample.translation.x;
			animLocal.m[13] = sample.translation.y;
			animLocal.m[14] = sample.translation.z;
		} else {
			animLocal = poseMatrix(sample);
			const float rowScale[3] = {
				sqrtf(bindLocalForFrame.m[0] * bindLocalForFrame.m[0] + bindLocalForFrame.m[1] * bindLocalForFrame.m[1] + bindLocalForFrame.m[2] * bindLocalForFrame.m[2]),
				sqrtf(bindLocalForFrame.m[4] * bindLocalForFrame.m[4] + bindLocalForFrame.m[5] * bindLocalForFrame.m[5] + bindLocalForFrame.m[6] * bindLocalForFrame.m[6]),
				sqrtf(bindLocalForFrame.m[8] * bindLocalForFrame.m[8] + bindLocalForFrame.m[9] * bindLocalForFrame.m[9] + bindLocalForFrame.m[10] * bindLocalForFrame.m[10])
			};
			for (uint32 row = 0; row < 3; ++row)
				if (rowScale[row] > 0.0001f && fabsf(rowScale[row] - 1.0f) > 0.0001f)
					for (uint32 k = 0; k < 3; ++k) animLocal.m[row * 4 + k] *= rowScale[row];
		}
		pose.localTransforms[frame] = animLocal;
	}
	Common::Array<byte> state(count, 0);
	for (uint32 i = 0; i < count; ++i) {
		if (state[i]) continue;
		Common::Array<uint32> path;
		uint32 current = i;
		while (!state[current]) {
			state[current] = 1;
			path.push_back(current);
			const int32 parent = _frames[current].parent;
			if (parent < 0) break;
			current = (uint32)parent;
		}
		while (!path.empty()) {
			const uint32 bone = path[path.size() - 1];
			const int32 parent = _frames[bone].parent;
			pose.worldTransforms[bone] = parent < 0 ? pose.localTransforms[bone] : multiply(pose.localTransforms[bone], pose.worldTransforms[parent]);
			state[bone] = 2;
			path.remove_at(path.size() - 1);
		}
	}
	for (uint32 geometry = 0; geometry < _geometries.size(); ++geometry) {
		if (!_geometries[geometry].skinned) continue;
		const Skin &skin = _geometries[geometry].skin;
		for (uint32 bone = 0; bone < skin.inverseBindMatrices.size(); ++bone) {
			const int32 frame = bone < skin.rendererBoneFrames.size() ? skin.rendererBoneFrames[bone] : frameForHierarchy(bone);
			if (frame >= 0 && (uint32)frame < count)
				pose.skinTransforms[frame] = multiply(pose.worldTransforms[frame], skin.inverseBindMatrices[bone]);
		}
		break;
	}
	return true;
}

int32 RMD::frameForHierarchy(uint32 index) const {
	if (_hierarchy.empty())
		return index < _frames.size() ? (int32)index : -1;
	uint32 nodeId = 0;
	bool found = false;
	for (uint32 i = 0; i < _hierarchy.size(); ++i) {
		if (_hierarchy[i].index == index) {
			nodeId = _hierarchy[i].nodeId;
			found = true;
			break;
		}
	}
	if (!found)
		return -1;
	for (uint32 i = 0; i < _frames.size(); ++i)
		if (_frames[i].hasHAnim && _frames[i].nameId == nodeId)
			return (int32)i;
	return -1;
}

bool RMD::validateFrames() const {
	for (uint32 i = 0; i < _frames.size(); ++i) {
		if (_frames[i].parent < -1 || (_frames[i].parent >= 0 && (uint32)_frames[i].parent >= _frames.size()) || _frames[i].parent == (int32)i)
			return false;
		Common::Array<byte> seen(_frames.size(), 0);
		for (int32 parent = _frames[i].parent; parent >= 0; parent = _frames[parent].parent) {
			if (seen[parent]) return false;
			seen[parent] = 1;
		}
	}
	return true;
}

bool RMD::validateReferences() const {
	for (uint32 i = 0; i < _geometries.size(); ++i) {
		const Geometry &geometry = _geometries[i];
		if (geometry.indices.size() % 3 || geometry.materialIndices.size() < geometry.indices.size() / 3)
			return false;
		for (uint32 index = 0; index < geometry.indices.size(); ++index)
			if (geometry.indices[index] >= geometry.positions.size())
				return false;
		for (uint32 material = 0; material < geometry.materialMap.size(); ++material)
			if (geometry.materialMap[material] != 0xFFFFFFFF && geometry.materialMap[material] >= _materials.size())
				return false;
		if (_materials.size() > 0)
			for (uint32 triangle = 0; triangle < geometry.materialIndices.size(); ++triangle) {
				uint32 material = geometry.materialIndices[triangle];
				if (material >= _materials.size())
					return false;
			}
		if (geometry.skinned) {
			const Skin &skin = geometry.skin;
		const uint32 usedBoneEntries = skin.usedBoneCount ? skin.usedBoneCount : skin.boneCount;
		if (skin.influences.size() != geometry.positions.size() || skin.usedBones.size() != usedBoneEntries ||
					skin.rendererBoneFrames.size() != skin.boneCount)
				return false;
			for (uint32 palette = 0; palette < skin.usedBones.size(); ++palette)
				if (skin.usedBones[palette] >= _frames.size())
					return false;
			for (uint32 vertex = 0; vertex < skin.influences.size(); ++vertex) {
				float sum = 0;
				for (uint32 weight = 0; weight < 4; ++weight) {
					const SkinInfluence &influence = skin.influences[vertex];
					if (!std::isfinite(influence.weights[weight]) || influence.weights[weight] < 0 ||
							(influence.weights[weight] > 0 && influence.indices[weight] >= skin.boneCount))
						return false;
					sum += influence.weights[weight];
				}
				if (!std::isfinite(sum) || sum <= 0)
					return false;
			}
		}
	}
	for (uint32 i = 0; i < _atomics.size(); ++i)
		if (_atomics[i].frame < -1 || (_atomics[i].frame >= 0 && (uint32)_atomics[i].frame >= _frames.size()) ||
				_atomics[i].geometry < 0 || (uint32)_atomics[i].geometry >= _geometries.size())
			return false;
	for (uint32 i = 0; i < _clumps.size(); ++i) {
		for (uint32 j = 0; j < _clumps[i].frames.size(); ++j)
			if (_clumps[i].frames[j] >= _frames.size()) return false;
		for (uint32 j = 0; j < _clumps[i].geometries.size(); ++j)
			if (_clumps[i].geometries[j] >= _geometries.size()) return false;
		for (uint32 j = 0; j < _clumps[i].atomics.size(); ++j)
			if (_clumps[i].atomics[j] >= _atomics.size()) return false;
	}
	for (uint32 i = 0; i < _animations.size(); ++i) {
		if (!std::isfinite(_animations[i].duration) || _animations[i].duration < 0)
			return false;
		for (uint32 j = 0; j < _animations[i].tracks.size(); ++j)
			if (_animations[i].tracks[j].bone >= _frames.size() || _animations[i].tracks[j].keys.empty())
				return false;
	}
	return true;
}

Graphics::Surface *RMD::getTextureSurface(uint32 textureIndex) const {
	if (textureIndex >= _textures.size())
		return nullptr;

	Graphics::Surface *surface = nullptr;
	return decodeTexture(_textures[textureIndex], surface) ? surface : nullptr;
}

bool RMD::decodeTexture(const Texture &texture, Graphics::Surface *&surface) const {
	surface = nullptr;
	const Common::Array<byte> &native = texture.nativeData;
	NativeNode info = {};
	NativeNode data = {};
	uint32 offset = 0;
	while (offset < native.size()) {
		NativeNode candidate;
		if (!readNativeNode(native, offset, native.size(), candidate))
			return false;
		uint32 childOffset = candidate.payloadOffset;
		NativeNode first;
		if (candidate.type == 1 && candidate.size > 0x40 &&
				readNativeNode(native, childOffset, candidate.payloadEnd, first) && first.type == 1 && first.size == 0x40) {
			uint32 next = first.payloadEnd;
			NativeNode second;
			if (readNativeNode(native, next, candidate.payloadEnd, second) && second.type == 1) {
				info = first;
				data = second;
				break;
			}
		}
		offset = candidate.payloadEnd;
	}
	if (data.payloadEnd == 0 || info.size != 0x40)
		return false;

	const uint32 width = readUint32LE(native, info.payloadOffset);
	const uint32 height = readUint32LE(native, info.payloadOffset + 4);
	const Graphics::PS2GsTex::Register tex0(readUint64LE(native, info.payloadOffset + 16));
	const Graphics::PS2PixelFormat fmt = tex0.psm();
	const uint32 tex0RowWidth = (uint32)tex0.tbw() * 64;
	const uint32 declaredTexelLength = readUint32LE(native, info.payloadOffset + 48);
	const uint32 declaredPaletteLength = readUint32LE(native, info.payloadOffset + 52);
	const int colorCount = Graphics::ps2ClutColorCount(fmt);
	const uint64 pixelCount = (uint64)width * height;
	const uint64 paletteBytes = colorCount > 0 ? (uint64)colorCount * 4 : 0;
	const bool indexed = fmt == Graphics::kPSMT4 || fmt == Graphics::kPSMT8;
	if (width == 0 || height == 0 || width > 4096 || height > 4096 || pixelCount > 64 * 1024 * 1024 ||
			(indexed && (pixelCount & 1) && fmt == Graphics::kPSMT4) ||
			(indexed && (tex0RowWidth < width || tex0RowWidth == 0)) ||
			(!indexed && (readUint32LE(native, info.payloadOffset + 12) & 0x10000)) ||
			declaredTexelLength < 16 ||
			declaredTexelLength < 16 + 64 + (uint64)Graphics::ps2TexelDataSize(fmt, (int)width, (int)height) ||
			!canRead(data.payloadOffset, declaredTexelLength, data.payloadEnd))
		return false;

	// TEX0.TBW is VRAM row padding, while the native raster payload and its
	// swizzle are encoded at the logical raster width used by Amicitia.
	const uint32 storageWidth = width;
	const uint64 storageTexelCount = (uint64)storageWidth * height;
	if (indexed && (declaredPaletteLength < 16 + 64 + paletteBytes ||
			!canRead(data.payloadOffset + declaredTexelLength, declaredPaletteLength, data.payloadEnd)))
		return false;
	if (!indexed && fmt != Graphics::kPSMCT16 && fmt != Graphics::kPSMCT16S &&
			fmt != Graphics::kPSMCT24 && fmt != Graphics::kPSMCT32)
		return false;

	if (!indexed) {
		// Direct-color rasters carry the same 16-byte local header + 64-byte
		// reserved block (a sceGsLoadImage/PS2StandardImageHeader, 0x50
		// bytes total) as the indexed path below, before the actual texel
		// payload -- confirmed against a real byte-exact AmicitiaLibrary
		// mismatch (P3F PS092.RMD's p_sraim_t01) that traced back to this
		// path reading 64 bytes too early, landing inside that header.
		byte *rasterData = const_cast<byte *>(native.data() + data.payloadOffset + 16 + 64);
		surface = Graphics::ps2CreateColorSurface(fmt, rasterData, (int)width, (int)height);
		return surface != nullptr;
	}

	// Indexed rasters carry the same 16-byte local header + 64-byte reserved
	// block as the palette section below before the actual texel payload.
	byte *rasterData = const_cast<byte *>(native.data() + data.payloadOffset + 16 + 64);
	Common::Array<byte> swizzled;
	swizzled.resize((uint32)storageTexelCount);
	if (fmt == Graphics::kPSMT4) {
		for (uint64 i = 0; i < storageTexelCount; i += 2) {
			swizzled[(uint32)i] = rasterData[i / 2] & 0x0F;
			swizzled[(uint32)i + 1] = rasterData[i / 2] >> 4;
		}
	} else {
		for (uint32 i = 0; i < storageTexelCount; ++i)
			swizzled[i] = rasterData[i];
	}
	Common::Array<byte> indices;
	indices.resize((uint32)pixelCount);
	if (!Graphics::ps2UnswizzleIndices(swizzled.data(), swizzled.size(), indices.data(), width, height, storageWidth))
		return false;
	Common::Array<byte> packedIndices;
	if (fmt == Graphics::kPSMT4) {
		packedIndices.resize((uint32)(pixelCount / 2));
		for (uint32 i = 0; i < pixelCount; i += 2)
			packedIndices[i / 2] = (byte)((indices[i] & 0x0F) | ((indices[i + 1] & 0x0F) << 4));
	} else {
		packedIndices = indices;
	}
	byte *palette = const_cast<byte *>(native.data() + data.payloadOffset + declaredTexelLength + 16 + 64);
	surface = Graphics::ps2CreateIndexedSurface(Graphics::kPSMCT32, palette, colorCount,
		packedIndices.data(), fmt, (int)width, (int)height, fmt == Graphics::kPSMT8, true);
	return surface != nullptr;
}

bool RMD::validVersion(uint32 version) {
	return validRenderWareVersion(version);
}

bool RMD::readNode(uint32 offset, uint32 limit, Node &node) const {
	if (offset > limit || limit - offset < 12)
		return false;

	node.type = readUint32LE(_data, offset);
	node.size = readUint32LE(_data, offset + 4);
	node.version = readUint32LE(_data, offset + 8);
	const bool opaqueHAnimExtension = node.type == kHAnimOpaqueExtension && node.size == 0 &&
			(node.version == 0x09 || node.version == 0x10);
	if (!validVersion(node.version) && !opaqueHAnimExtension)
		return false;

	node.payloadOffset = offset + 12;
	// Subtraction keeps a corrupt uint32 size from wrapping the end offset.
	if (node.size > limit - node.payloadOffset)
		return false;
	node.payloadEnd = node.payloadOffset + node.size;
	return true;
}

uint32 RMD::nextSiblingOffset(const Node &node, uint32 end) const {
	uint32 offset = node.payloadEnd;
	if (node.type != kPersonaParticleList)
		return offset;
	bool variantMarker = false;
	while (offset < end) {
		Node next;
		if (readNode(offset, end, next))
			break;
		if (_data[offset] == 0 || (!variantMarker && offset == node.payloadEnd && _data[offset] == 0xBD)) {
			variantMarker = variantMarker || _data[offset] == 0xBD;
			++offset;
			continue;
		}
		break;
	}
	return offset;
}

bool RMD::readSiblings(uint32 begin, uint32 end) {
	uint32 offset = begin;
	while (offset < end) {
		Node node;
		if (!readNode(offset, end, node)) {
			uint32 recovered = offset;
			if (recoverOwnedTail(offset, end, recovered)) {
				offset = recovered;
				continue;
			}
			_ok = false;
			return false;
		}
		if (node.type != kPersonaParticleList && !validateChildren(node.payloadOffset, node.payloadEnd, 0))
		{
			_ok = false;
			return false;
		}
		_nodes.push_back(node);
		if (node.payloadEnd <= offset)
			return false;
		offset = nextSiblingOffset(node, end);
	}
	return offset == end;
}

bool RMD::validateUVDictionary(const Node &node) const {
	if (node.type != kUVAnimationDictionary ||
			(node.version != 0x1C02002D && node.version != 0x1C020037))
		return false;
	Node structure;
	if (!readNode(node.payloadOffset, node.payloadEnd, structure) || structure.type != kStruct || structure.size < 4)
		return false;
	const uint32 count = readUint32LE(_data, structure.payloadOffset);
	if (count > 4096)
		return false;
	uint32 offset = structure.payloadOffset + 4;
	for (uint32 i = 0; i < count; ++i) {
		Node animation;
		if (!readNode(offset, node.payloadEnd, animation) || animation.type != kPersonaAnimation)
			return false;
		offset = animation.payloadEnd;
	}
	return offset == node.payloadEnd;
}

bool RMD::recoverOwnedTail(uint32 offset, uint32 end, uint32 &next) {
	const uint32 scanEnd = end - offset > 64 ? offset + 64 : end;
	if (offset + 14 <= end && _data[offset] == 0xAF && _data[offset + 1] == 0x9F &&
			_data[offset + 2] == 0x10 && _data[offset + 3] == 0x00 &&
			_data[offset + 4] == 0x00 && _data[offset + 5] == 0x00 &&
			_data[offset + 6] == 0xA5 && _data[offset + 7] == 0x01 &&
			_data[offset + 8] == 0x00 && _data[offset + 9] == 0x00 &&
			_data[offset + 10] == 0x37 && _data[offset + 11] == 0x00 &&
			_data[offset + 12] == 0x02 && _data[offset + 13] == 0x1C) {
		Node particle;
		if (readNode(offset + 14, end, particle)) {
			next = offset + 14;
			_recoveryRanges.push_back({offset, next});
			return true;
		}
	}
	for (uint32 candidate = offset; candidate + 12 <= end && candidate <= offset + 16; ++candidate) {
		Node transform;
		if (!readNode(candidate, end, transform) || transform.type != kPersonaAnimationOverride ||
				transform.size != 64 || transform.version != 0x40000000)
			continue;
		bool fill = true;
		for (uint32 p = offset; p < candidate; ++p)
			fill = fill && (_data[p] == 0 || _data[p] == 0xCD || _data[p] == 0xFF ||
					_data[p] == 0xDE || _data[p] == 0xED || _data[p] == 0xDC);
		bool observedPs0ca = candidate - offset == 15 && _data[offset + 1] == 0x80 &&
			_data[offset + 2] == 0x3F && _data[offset + 3] == 0xEA && _data[offset + 4] == 0x51 &&
			_data[offset + 5] == 0x38 && _data[offset + 6] == 0x3D && _data[offset + 7] == 0x00 &&
			_data[offset + 8] == 0x00 && _data[offset + 9] == 0x80 && _data[offset + 10] == 0x3F &&
			_data[offset + 11] == 0x00 && _data[offset + 12] == 0x00 && _data[offset + 13] == 0x00 &&
			_data[offset + 14] == 0x00;
		bool observedWeapon = candidate - offset == 8 && _data[offset] == 0 && _data[offset + 1] == 0 &&
			_data[offset + 2] == 0 && _data[offset + 3] == 0 && _data[offset + 4] == 0 &&
			_data[offset + 5] == 0 && _data[offset + 6] == 0 && _data[offset + 7] == 0;
		if (fill || observedPs0ca || observedWeapon) {
			next = transform.payloadEnd;
			_recoveryRanges.push_back({offset, next});
			return true;
		}
	}
	for (uint32 candidate = offset; candidate + 76 <= end && candidate <= offset + 16; ++candidate) {
		if (readUint32LE(_data, candidate) != kPersonaAnimationOverride ||
				readUint32LE(_data, candidate + 4) != 64 || readUint32LE(_data, candidate + 8) != 0x40000000)
			continue;
		bool fill = true;
		for (uint32 p = offset; p < candidate; ++p)
			fill = fill && (_data[p] == 0 || _data[p] == 0xCD || _data[p] == 0xFF);
		if (fill || candidate - offset == 3 || candidate - offset == 8 || candidate - offset == 14 ||
				candidate - offset == 15) {
			next = candidate + 76;
			_recoveryRanges.push_back({offset, next});
			return true;
		}
	}
	if (offset + 2 <= end && _data[offset] == 0x54 && _data[offset + 1] == 0x3C) {
		Node particle;
		if (readNode(offset + 2, end, particle)) {
			next = offset + 2;
			_recoveryRanges.push_back({offset, next});
			return true;
		}
	}

	for (uint32 candidate = offset; candidate + 12 <= scanEnd; ++candidate) {
		Node node;
		if (!readNode(candidate, end, node))
			continue;
		if (node.type == kUVAnimationDictionary && !(candidate & 15) &&
				candidate - offset <= 32 && validateUVDictionary(node)) {
			bool fill = true;
			for (uint32 p = offset; p < candidate; ++p)
				fill = fill && (_data[p] == 0 || _data[p] == 0xCD || _data[p] == 0xFF ||
					_data[p] == 0xDE || _data[p] == 0xED || _data[p] == 0xDC);
			bool observedN01601 = candidate - offset == 12 && _data[offset] == 0x98 &&
				_data[offset + 1] == 0x99 && _data[offset + 2] == 0x99 && _data[offset + 3] == 0x3E;
			bool observedM039 = candidate - offset == 13 && _data[offset] == 0x3E &&
				_data[offset + 1] == 0x15 && _data[offset + 2] == 0x5C && _data[offset + 3] == 0x8F &&
				_data[offset + 4] == 0x3D && _data[offset + 5] == 0xCB && _data[offset + 6] == 0xCC &&
				_data[offset + 7] == 0x4C && _data[offset + 8] == 0x3E;
			if (fill || observedN01601 || observedM039) {
				next = candidate;
				_recoveryRanges.push_back({offset, next});
				return true;
			}
		}
		if (node.type != kPersonaAnimationOverride || node.size != 64 || node.version != 0x40000000 ||
				candidate - offset > 16)
			continue;
		bool fill = true;
		for (uint32 p = offset; p < candidate; ++p)
			fill = fill && (_data[p] == 0 || _data[p] == 0xCD || _data[p] == 0xFF);
		bool weaponSuffix = candidate - offset == 11 &&
			_data[offset] == 0 && _data[offset + 1] == 0 && _data[offset + 2] == 0 && _data[offset + 3] == 0 &&
			_data[offset + 4] == 0 && _data[offset + 5] == 0 && _data[offset + 6] == 0 &&
			_data[offset + 7] == 0x40 && _data[offset + 8] == 0 && _data[offset + 9] == 0 && _data[offset + 10] == 0;
		if (fill || weaponSuffix) {
			next = node.payloadEnd;
			_recoveryRanges.push_back({offset, next});
			return true;
		}
	}
	return false;
}

bool RMD::validateChildren(uint32 begin, uint32 end, uint32 depth) {
	const uint32 version = end - begin >= 12 ? readUint32LE(_data, begin + 8) : 0;
	if (end - begin < 12 ||
			!validVersion(version))
		return true;
	if (depth >= kMaxNodeDepth) {
		_ok = false;
		return false;
	}

	uint32 offset = begin;
	while (offset < end) {
		Node child;
		if (!readNode(offset, end, child) || child.payloadEnd <= offset) {
			_ok = false;
			return false;
		}
		const bool knownContainer = child.type == kStruct || child.type == kFrameList ||
				child.type == kGeometry || child.type == kGeometryList || child.type == kClump ||
				child.type == kAtomic || child.type == kTextureNative || child.type == kTextureDictionary ||
				child.type == kMaterial || child.type == kMaterialList || child.type == kTextureReference ||
				child.type == kUVAnimationDictionary;
		if (knownContainer && !validateChildren(child.payloadOffset, child.payloadEnd, depth + 1))
			return false;
		offset = nextSiblingOffset(child, end);
	}
	return offset == end;
}

} // namespace Common
