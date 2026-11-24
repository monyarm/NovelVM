#include "common/formats/gmo.h"

#include "common/memstream.h"
#include "graphics/ps2texture.h"
#include "graphics/pixelformat.h"

namespace Common {

namespace {

// Vertex array format bit flags (see gmo.h notes; per maxscript GmoStructs).
enum ArrayFlag {
	kTexUByte = 1 << 0,
	kTexUShort = 2 << 0,
	kTexFloat = 3 << 0,
	kColor5650 = 4 << 2,
	kColor5551 = 5 << 2,
	kColor4444 = 6 << 2,
	kColor8888 = 7 << 2,
	kNormalByte = 1 << 5,
	kNormalShort = 2 << 5,
	kNormalFloat = 3 << 5,
	kVertexByte = 1 << 7,
	kVertexShort = 2 << 7,
	kVertexFloat = 3 << 7,
	kWeightUByte = 1 << 9,
	kWeightUShort = 2 << 9,
	kWeightFloat = 3 << 9,
	kWeightCountMask = 0xF << 14,  // weight-2..8 encoded as (count-1) << 14
	kColorMask = 0x3 << 2,
	kTexMask = 0x3 << 0,
	kNormalMask = 0x3 << 5,
	kVertexMask = 0x3 << 7,
	kWeightMask = 0x3 << 9,
};

uint32 weightCountFromFormat(uint32 f) {
	uint32 c = (f & kWeightCountMask) >> 14;
	if (c == 0) return 1;
	return c + 1; // 1<<14 = 2, ... 7<<14 = 8
}

float expandSigned(int raw, int bits) {
	if (bits == 32)
		return (float)(int32)raw;
	if (bits == 16)
		return (float)(int16)raw / 32768.0f;
	return (float)(int8)raw / 128.0f;
}

// IEEE 754 binary16 -> binary32 (used for FCurve FLOAT16 frames).
float halfToFloat(uint16 h) {
	uint32 sign = (uint32)(h & 0x8000) << 16;
	uint32 exp = (h >> 10) & 0x1F;
	uint32 man = h & 0x3FF;
	uint32 out;
	if (exp == 0) {
		if (man == 0) {
			out = sign; // +/- zero
		} else {
			// subnormal
			exp = 127 - 15 + 1;
			while ((man & 0x400) == 0) {
				man <<= 1;
				exp--;
			}
			man &= 0x3FF;
			out = sign | (exp << 23) | (man << 13);
		}
	} else if (exp == 0x1F) {
		// inf / nan
		out = sign | 0x7F800000 | (man << 13);
	} else {
		out = sign | ((exp - 15 + 127) << 23) | (man << 13);
	}
	float f;
	memcpy(&f, &out, 4);
	return f;
}

} // namespace

GMO::GMO(SeekableReadStream *stream) : _data(nullptr), _size(0), _pos(0) {
	if (!stream)
		return;

	_size = stream->size() - stream->pos();
	if (_size < 16)
		return;
	_data = new byte[_size];
	stream->read(_data, _size);

	// Signature (16 bytes): "OMG.00.1PSP" + 4 reserved zero bytes.
	if (_data[0] != 'O' || _data[1] != 'M' || _data[2] != 'G' || _data[3] != '.' ||
	    _data[4] != '0' || _data[5] != '0' || _data[6] != '.' || _data[7] != '1')
		return;

	_pos = 16;
	parseFile();
	_ok = true;
}

GMO::~GMO() {
	delete[] _data;
}

uint16 GMO::ru16() {
	if (_pos + 2 > _size)
		return 0;
	uint16 v = _data[_pos] | (_data[_pos + 1] << 8);
	_pos += 2;
	return v;
}

uint32 GMO::ru32() {
	if (_pos + 4 > _size)
		return 0;
	uint32 v = _data[_pos] | (_data[_pos + 1] << 8) | (_data[_pos + 2] << 16) | ((uint32)_data[_pos + 3] << 24);
	_pos += 4;
	return v;
}

float GMO::rf32() {
	uint32 u = ru32();
	float f;
	memcpy(&f, &u, 4);
	return f;
}

Common::String GMO::readNameLen(int len) {
	if (_pos + len > _size)
		len = (int)(_size - _pos);
	if (len <= 0)
		return Common::String();
	Common::String s((const char *)_data + _pos, len);
	_pos += len;
	return s;
}

GMO::Chunk GMO::readChunkAt(uint32 pos) {
	Chunk c;
	c.offset = pos;
	_pos = pos;
	if (pos + 8 > _size)
		return c;
	c.type = ru16();
	c.argsOffs = ru16();
	c.nextOffs = ru32();
	c.half = (c.type & 0x8000) != 0;
	if (!c.half) {
		if (pos + 16 > _size)
			return c;
		c.childOffs = ru32();
		c.dataOffs = ru32();
	}
	return c;
}

void GMO::parseFile() {
	// The first chunk at offset 16 is FILE (0x0002): a full chunk whose
	// children region holds the MODEL chunks. Walk FILE's children.
	_pos = 16;
	Chunk file = readChunkAt(16);
	if ((file.type & 0x7fff) != kFile || file.nextOffs == 0)
		return;
	uint32 pos = file.offset + file.childOffs;
	uint32 stop = file.offset + file.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk c = readChunkAt(pos);
		if (c.nextOffs == 0 && c.type == 0)
			break;
		if ((c.type & 0x7fff) == kModel) {
			parseModel(c);
			// A file has at most one MODEL; stop.
			return;
		}
		uint32 next = c.offset + c.nextOffs;
		if (next <= pos || next > _size + 8)
			break;
		pos = next;
	}
}

void GMO::parseModel(const Chunk &model) {
	if (model.childOffs >= model.nextOffs)
		return;
	int modelId = _bones.size(); // not used; MODEL is a grouping node
	(void)modelId;
	uint32 pos = model.offset + model.childOffs;
	uint32 stop = model.offset + model.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk c = readChunkAt(pos);
		if (c.nextOffs == 0 && c.type == 0)
			break;
		uint32 typeId = c.type & 0x7fff;
		// Named full chunks only (bones/parts/etc all carry a name).
		_pos = c.offset + 16;
		int nameLen = c.argsOffs >= 16 ? (int)c.argsOffs - 16 : 0;
		Common::String cname;
		if (nameLen > 0)
			cname = readNameLen(nameLen);

		switch (typeId) {
		case kBone: {
			Bone b;
			b.name = cname;
			parseBone(c, b);
			_bones.push_back(b);
			break;
		}
		case kPart: {
			Part p;
			p.name = cname;
			parsePart(c, p);
			_parts.push_back(p);
			break;
		}
		case kMaterial: {
			Material m;
			m.name = cname;
			parseMaterial(c, m);
			_materials.push_back(m);
			break;
		}
		case kTexture: {
			Texture t;
			t.name = cname;
			parseTexture(c, t);
			_textures.push_back(t);
			break;
		}
		case kMotion: {
			Motion m;
			m.name = cname;
			parseMotion(c, m);
			_motions.push_back(m);
			break;
		}
		case kArrayOffset: {
			// [offX offY offZ scaleX scaleY scaleZ] -- vertex space -> bone
			// space. Real P3P files store this as a HALF chunk (0x8015), so
			// its data starts right after the 8-byte header.
			_pos = c.offset + (c.half ? 8 : 16);
			if (!c.half && nameLen > 0)
				_pos += nameLen;
			uint32 fmt = ru32();
			if (fmt == (3 << 7)) { // VERTEX_FLOAT
				for (int i = 0; i < 6; i++)
					_vertexOffset[i] = rf32();
				_hasVertexOffset = true;
			} else if (fmt == (3 << 0)) { // TEXTURE_FLOAT
				for (int i = 0; i < 4; i++)
					_textureOffset[i] = rf32();
				_hasTextureOffset = true;
			}
			break;
		}
		case kBlindData: {
			// BLIND_DATA is an opaque per-model blob whose children carry
			// Atlus extensions; the important one for mesh layout is
			// ARRAY_OFFSET (vertex space -> bone space).
			uint32 bp = c.offset + c.childOffs;
			uint32 bs = c.offset + c.nextOffs;
			for (uint32 _g = 0; _g < 32 && bp + 8 <= bs && bp + 8 <= _size; _g++) {
				Chunk bch = readChunkAt(bp);
				uint32 bt = bch.type & 0x7fff;
				if (bt == kArrayOffset) {
					_pos = bch.offset + (bch.half ? 8 : 16);
					if (!bch.half && bch.argsOffs > 16)
						_pos += bch.argsOffs - 16;
					uint32 fmt2 = ru32();
					if (fmt2 == (3 << 7)) {
						for (int i = 0; i < 6; i++)
							_vertexOffset[i] = rf32();
						_hasVertexOffset = true;
					} else if (fmt2 == (3 << 0)) {
						for (int i = 0; i < 4; i++)
							_textureOffset[i] = rf32();
						_hasTextureOffset = true;
					}
				}
				uint32 bn = bch.offset + bch.nextOffs;
				if (bn <= bp || bn > bs + 8)
					break;
				bp = bn;
			}
			break;
		}
		default:
			break; // BOUNDING_BOX / BLIND_DATA ignored
		}

		uint32 next = c.offset + c.nextOffs;
		if (next <= pos || next > _size + 8)
			break;
		pos = next;
	}
}

void GMO::parseBone(const Chunk &c, Bone &bone) {
	uint32 pos = c.offset + c.childOffs;
	uint32 stop = c.offset + c.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk ch = readChunkAt(pos);
		uint32 typeId = ch.type & 0x7fff;
		if (ch.half)
			_pos = ch.offset + 8;
		else
			_pos = ch.offset + 16;
		switch (typeId) {
		case kTranslate: {
			for (int i = 0; i < 3; i++)
				bone.translate[i] = rf32();
			bone.hasTranslate = true;
			break;
		}
		case kRotateZYX:
		case kRotateYXZ: {
			for (int i = 0; i < 3; i++)
				bone.rotate[i] = rf32();
			bone.rotate[3] = 1;
			bone.rotateType = (typeId == kRotateZYX) ? 0 : 1;
			bone.hasRotate = true;
			break;
		}
		case kRotateQ: {
			for (int i = 0; i < 4; i++)
				bone.rotate[i] = rf32();
			bone.rotateType = 2;
			bone.hasRotate = true;
			break;
		}
		case kScale: {
			for (int i = 0; i < 3; i++)
				bone.scale[i] = rf32();
			bone.hasScale = true;
			break;
		}
		case kPivot: {
			for (int i = 0; i < 3; i++)
				bone.pivot[i] = rf32();
			break;
		}
		case kVisibility:
			bone.visibility = ru32();
			break;
		case kParentBone: {
			bone.parentIndex = (_data[_pos] & 0xFF);
			_pos += 4;
			break;
		}
		case kDrawPart: {
			Ref r;
			r.index = _data[_pos];
			r.level = _data[_pos + 1];
			r.type = _data[_pos + 2];
			r.index -= 0; // real P3P uses 0-based index bytes
			bone.drawParts.push_back(r);
			_pos += 4;
			break;
		}
		case kBlendBones: {
			int count = (int)ru32();
			for (int i = 0; i < count; i++) {
				Ref r;
				r.index = _data[_pos];
				_pos += 4;
				bone.blendBones.push_back(r);
			}
			break;
		}
		case kBlendOffsets: {
			int count = (int)ru32();
			for (int i = 0; i < count * 16; i++)
				bone.blendOffsets.push_back(rf32());
			break;
		}
		case kBlindData: {
			// 12-byte data name, then (for "per3Helper") a u32 id and 9
			// floats (three vec3s) of editor metadata (maxscript reference).
			bone.blindName = readNameLen(MIN(12, (int)(stop + 8 - _pos)));
			if (bone.blindName.hasPrefix("per3Helper")) {
				bone.blindId = ru32();
				for (int i = 0; i < 9; i++)
					bone.blindData[i] = rf32();
			}
			break;
		}
		default:
			break;
		}
		uint32 next = ch.offset + ch.nextOffs;
		if (next <= pos || next > stop + 8)
			break;
		pos = next;
	}
}

void GMO::parsePart(const Chunk &c, Part &part) {
	uint32 pos = c.offset + c.childOffs;
	uint32 stop = c.offset + c.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk ch = readChunkAt(pos);
		uint32 typeId = ch.type & 0x7fff;
		_pos = ch.offset + 16;
		int nameLen = ch.argsOffs >= 16 ? (int)ch.argsOffs - 16 : 0;
		Common::String cname = nameLen > 0 ? readNameLen(nameLen) : Common::String();

		if (typeId == kMesh) {
			Mesh mesh;
			mesh.name = cname;
			// Mesh children.
			uint32 mp = ch.offset + ch.childOffs;
			uint32 ms = ch.offset + ch.nextOffs;
			for (uint32 _guardM = 0; _guardM < 100000 && mp + 8 <= ms && mp + 8 <= _size; _guardM++) {
				Chunk mh = readChunkAt(mp);
				uint32 mt = mh.type & 0x7fff;
				_pos = mh.offset + 8;
				switch (mt) {
				case kSetMaterial: {
					mesh.material.index = _data[_pos];
					mesh.material.level = _data[_pos + 1];
					mesh.material.type = _data[_pos + 2];
					_pos += 4;
					break;
				}
				case kBlendSubset: {
					int n = (int)ru32();
					for (int i = 0; i < n; i++)
						mesh.blendSubset.push_back(ru32());
					break;
				}
				case kDrawArrays: {
					DrawInfo d;
					d.arrays.index = _data[_pos];
					d.arrays.level = _data[_pos + 1];
					d.arrays.type = _data[_pos + 2];
					_pos += 4;
					d.mode = ru32();
					d.vertexCount = ru32();
					d.primitiveCount = ru32();
					// Sequential primitives carry a single start index; non-sequential
					// draws carry an explicit u16 index list (primitiveCount
					// groups of vertexCount indices).
					if (d.mode & 0x100) {
						d.startVertex = ru16();
					} else {
						uint32 n = d.primitiveCount * d.vertexCount;
						if (n > 100000)
							n = 0; // guard against corrupt counts
						for (uint32 i = 0; i < n; i++)
							d.indices.push_back(ru16());
					}
					mesh.draws.push_back(d);
					break;
				}
				default:
					break;
				}
				uint32 mn = mh.offset + mh.nextOffs;
				if (mn <= mp)
					break;
				mp = mn;
			}
			part.meshes.push_back(mesh);
		} else if (typeId == kArrays) {
			Arrays a;
			a.name = cname;
			a.format = ru32();
			a.vertexCount = ru32();
			a.morphCount = ru32();
			a.morphFormat = ru32();

			// Determine stride (bytes per vertex) purely from flags.
			uint32 f = a.format;
			int texBpp = 0;
			switch (f & kTexMask) {
			case (1 << 0): texBpp = 1; break;
			case (2 << 0): texBpp = 2; break;
			case (3 << 0): texBpp = 4; break;
			default: texBpp = 0;
			}
			int colorBpp = 0;
			switch (f & kColorMask) {
			case (4 << 2): colorBpp = 2; break; // 5650
			case (5 << 2): colorBpp = 2; break; // 5551
			case (6 << 2): colorBpp = 2; break; // 4444
			case (7 << 2): colorBpp = 4; break; // 8888
			default: colorBpp = 0;
			}
			int normalBpp = 0;
			switch (f & kNormalMask) {
			case (1 << 5): normalBpp = 3; break;
			case (2 << 5): normalBpp = 6; break;
			case (3 << 5): normalBpp = 12; break;
			default: normalBpp = 0;
			}
			int vertexBpp = 0;
			switch (f & kVertexMask) {
			case (1 << 7): vertexBpp = 3; break;
			case (2 << 7): vertexBpp = 6; break;
			case (3 << 7): vertexBpp = 12; break;
			default: vertexBpp = 0;
			}
			int weightBpp = 0;
			int wc = weightCountFromFormat(f);
			switch (f & kWeightMask) {
			case (1 << 9): weightBpp = wc; break;
			case (2 << 9): weightBpp = wc * 2; break;
			case (3 << 9): weightBpp = wc * 4; break;
			default: weightBpp = 0;
			}
			int morphPairs = a.morphFormat > 0 ? (a.morphFormat > 1 ? 4 : 2) : 0;

			int stride = texBpp == 2 ? 4 : texBpp; // texture coords are 2 components
			if (texBpp) stride = texBpp * 2;
			if (texBpp == 0) stride = 0;
			stride += colorBpp + normalBpp + vertexBpp + weightBpp + morphPairs * 2;

			// Parse per-vertex if a positional store exists.
			if (vertexBpp == 0)
				a.vertexCount = 0;
			else
				a.vertexCount = MIN(a.vertexCount, (uint32)((_size - _pos) / (stride ? stride : 4)));
			if (a.vertexCount > 65536)
				a.vertexCount = 0;

			for (uint32 v = 0; v < a.vertexCount; v++) {
				uint32 vStart = _pos;
				// morph offets
				for (int i = 0; i < morphPairs; i++)
					a.uvOffsets.push_back(ru16());

				// weight
				if (weightBpp > 0) {
					uint32 wf = f & kWeightMask;
					if (wf == (1 << 9)) {
						for (int i = 0; i < wc; i++)
							a.weights.push_back((float)_data[_pos + i] / 128.0f);
						_pos += wc;
						if (wc & 1) _pos++; // pad to even
					} else if (wf == (2 << 9)) {
						for (int i = 0; i < wc; i++)
							a.weights.push_back((float)ru16() / 32768.0f);
					} else if (wf == (3 << 9)) {
						for (int i = 0; i < wc; i++)
							a.weights.push_back(rf32());
					}
				}

				// texture
				if (texBpp) {
					float u = 0, v = 0;
					if (texBpp == 1) { u = _data[_pos] / 128.0f; v = _data[_pos + 1] / 128.0f; _pos += 2; }
					else if (texBpp == 2) { u = (float)ru16() / 32768.0f; v = (float)ru16() / 32768.0f; }
					else { u = rf32(); v = rf32(); }
					a.texcoords.push_back(u);
					a.texcoords.push_back(v);
				}

				// color
				if (colorBpp) {
					uint16 raw = ru16();
					byte r = 0, g = 0, b = 0, al = 255;
					switch (f & kColorMask) {
					case (4 << 2): // 5650 (little-endian bit order used by P3P)
						r = (byte)(((raw >> 11) & 0x1F) << 3);
						g = (byte)(((raw >> 5) & 0x3F) << 2);
						b = (byte)((raw & 0x1F) << 3);
						break;
					case (5 << 2): // 5551
					{
						uint16 v = (uint16)((raw >> 8) | (raw << 8)); // byte-swap to match PS2
						r = (byte)(((v >> 10) & 0x1F) << 3);
						g = (byte)(((v >> 5) & 0x1F) << 3);
						b = (byte)((v & 0x1F) << 3);
						al = (v & 0x8000) ? 255 : 0;
						break;
					}
					case (6 << 2): // 4444
					{
						uint16 v = (uint16)((raw >> 8) | (raw << 8));
						r = (byte)(((v >> 12) & 0xF) * 17);
						g = (byte)(((v >> 8) & 0xF) * 17);
						b = (byte)(((v >> 4) & 0xF) * 17);
						al = (byte)((v & 0xF) * 17);
						break;
					}
					case (7 << 2): // 8888
						r = raw & 0xFF;
						g = (raw >> 8) & 0xFF;
						{
							byte b2 = _data[_pos]; byte a2 = _data[_pos + 1];
							b = b2; al = a2;
							_pos += 2;
						}
						break;
					default: break;
					}
					a.colors.push_back(r / 255.0f);
					a.colors.push_back(g / 255.0f);
					a.colors.push_back(b / 255.0f);
					a.colors.push_back(al / 255.0f);
				}

				// normal
				if (normalBpp) {
					uint32 nf = f & kNormalMask;
					if (nf == (1 << 5)) {
						a.normals.push_back((float)(int8)_data[_pos] / 128.0f);
						a.normals.push_back((float)(int8)_data[_pos + 1] / 128.0f);
						a.normals.push_back((float)(int8)_data[_pos + 2] / 128.0f);
						_pos += 3;
					} else if (nf == (2 << 5)) {
						for (int i = 0; i < 3; i++)
							a.normals.push_back((float)(int16)ru16() / 32768.0f);
					} else if (nf == (3 << 5)) {
						for (int i = 0; i < 3; i++)
							a.normals.push_back(rf32());
					}
				}

				// position
				if (vertexBpp) {
					uint32 vf = f & kVertexMask;
					if (vf == (1 << 7)) {
						a.positions.push_back((float)(int8)_data[_pos] / 128.0f);
						a.positions.push_back((float)(int8)_data[_pos + 1] / 128.0f);
						a.positions.push_back((float)(int8)_data[_pos + 2] / 128.0f);
						_pos += 3;
					} else if (vf == (2 << 7)) {
						for (int i = 0; i < 3; i++)
							a.positions.push_back((float)(int16)ru16() / 32768.0f);
					} else if (vf == (3 << 7)) {
						for (int i = 0; i < 3; i++)
							a.positions.push_back(rf32());
					}
				}
				(void)vStart;
			}

			part.arrays.push_back(a);
		}
		uint32 next = ch.offset + ch.nextOffs;
		if (next <= pos || next > stop + 8)
			break;
		pos = next;
	}
}

void GMO::parseMaterial(const Chunk &c, Material &mat) {
	uint32 pos = c.offset + c.childOffs;
	uint32 stop = c.offset + c.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk ch = readChunkAt(pos);
		uint32 typeId = ch.type & 0x7fff;
		_pos = ch.offset + 8;

		auto readColor = [&](float *c4) {
			for (int i = 0; i < 4; i++)
				c4[i] = rf32();
		};

		switch (typeId) {
		case kDiffuse: readColor(mat.diffuse); mat.hasDiffuse = true; break;
		case kSpecular: readColor(mat.specular); mat.specularPower = rf32(); mat.hasSpecular = true; break;
		case kEmission: readColor(mat.emission); mat.hasEmission = true; break;
		case kAmbient: readColor(mat.ambient); mat.hasAmbient = true; break;
		case kReflection: mat.reflection = rf32(); break;
		case kRefraction: mat.refraction = rf32(); break;
		case kBump: mat.bump = rf32(); break;
		case kBlindData: {
			Common::String bname = readNameLen(MIN(12, (int)(stop + 8 - _pos)));
			if (bname.hasPrefix("transAlgo"))
				mat.transAlgo = ru32();
			break;
		}
		case kLayer: {
			Layer layer;
			int nameLen = ch.argsOffs >= 16 ? (int)ch.argsOffs - 16 : 0;
			if (nameLen > 0) layer.name = readNameLen(nameLen);
			// Layer children (SetTexture/TexWrap/TexGen/TexMatrix/TexCrop).
			uint32 lp = ch.offset + ch.childOffs;
			uint32 ls = ch.offset + ch.nextOffs;
			for (uint32 _guardL = 0; _guardL < 100000 && lp + 8 <= ls && lp + 8 <= _size; _guardL++) {
				Chunk lh = readChunkAt(lp);
				uint32 lt = lh.type & 0x7fff;
				_pos = lh.offset + 8;
				switch (lt) {
				case kSetTexture: {
					layer.texture.index = _data[_pos];
					layer.texture.level = _data[_pos + 1];
					layer.texture.type = _data[_pos + 2];
					_pos += 4;
					break;
				}
				case kMapType:
					layer.mapType = ru32();
					break;
				case kMapFactor:
					layer.mapFactor = rf32();
					break;
				case kBlendFunc:
					layer.blendMode = ru32();
					layer.blendSrc = ru32();
					layer.blendDst = ru32();
					layer.hasBlendFunc = true;
					break;
				case kTexFunc:
					layer.texFunc = ru32();
					layer.texComp = ru32();
					break;
				case kTexFilter:
					layer.texFilterMag = ru32();
					layer.texFilterMin = ru32();
					break;
				case kTexWrap:
					layer.texWrapU = ru32();
					layer.texWrapV = ru32();
					break;
				case kTexGen:
					layer.texGen = ru32();
					break;
				case kTexCrop:
					for (int i = 0; i < 4; i++)
						layer.texCrop[i] = rf32();
					break;
				case kTexMatrix:
					for (int i = 0; i < 16; i++)
						layer.texMatrix[i] = rf32();
					break;
				default:
					break;
				}
				uint32 ln = lh.offset + lh.nextOffs;
				if (ln <= lp)
					break;
				lp = ln;
			}
			mat.layers.push_back(layer);
			break;
		}
		default:
			break;
		}
		uint32 next = ch.offset + ch.nextOffs;
		if (next <= pos || next > stop + 8)
			break;
		pos = next;
	}
}

void GMO::parseTexture(const Chunk &c, Texture &tex) {
	uint32 pos = c.offset + c.childOffs;
	uint32 stop = c.offset + c.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk ch = readChunkAt(pos);
		uint32 typeId = ch.type & 0x7fff;
		_pos = ch.offset + 8;
		switch (typeId) {
		case kFileName: {
			int len = (ch.nextOffs > 8) ? (int)ch.nextOffs - 8 : 0;
			if (len > 0)
				tex.fileName = readNameLen(len);
			break;
		}
		case kFileImage: {
			if (ch.half) {
				uint32 size = ru32();
				if (size > 0 && _pos + size <= _size) {
					tex.image.resize(size);
					memcpy(tex.image.data(), _data + _pos, size);
				}
				_pos += size;
			}
			break;
		}
		default:
			break;
		}
		uint32 next = ch.offset + ch.nextOffs;
		if (next <= pos || next > stop + 8)
			break;
		pos = next;
	}
}

void GMO::parseMotion(const Chunk &c, Motion &motion) {
	uint32 pos = c.offset + c.childOffs;
	uint32 stop = c.offset + c.nextOffs;
	for (uint32 _guard = 0; _guard < 100000 && pos + 8 <= stop && pos < _size; _guard++) {
		Chunk ch = readChunkAt(pos);
		uint32 typeId = ch.type & 0x7fff;
		if (ch.half)
			_pos = ch.offset + 8;
		else
			_pos = ch.offset + ch.argsOffs;

		switch (typeId) {
		case kFrameRate:
			motion.frameRate = rf32();
			break;
		case kFrameLoop:
			motion.frameLoopStart = rf32();
			motion.frameLoopEnd = rf32();
			break;
		case kAnimate: {
			Animate a;
			a.block.index = _data[_pos];
			a.block.level = _data[_pos + 1];
			a.block.type = _data[_pos + 2];
			_pos += 4;
			a.type = ru32();
			a.index = ru32();
			a.fcurve.index = _data[_pos];
			a.fcurve.level = _data[_pos + 1];
			a.fcurve.type = _data[_pos + 2];
			_pos += 4;
			motion.animates.push_back(a);
			break;
		}
		case kFCurve: {
			FCurve fc;
			fc.type = ru32();
			fc.valueCount = ru32();
			fc.frameCount = ru32();
			ru32(); // reserved
			if (fc.frameCount > 100000)
				fc.frameCount = 100000;
			if (fc.valueCount > 16)
				fc.valueCount = 0;
			bool hfloat = (fc.type & 0x80) != 0;
			// Each frame: time (half or float), then valueCount values.
			// A time whose raw half value is 0xFFFF marks a dummy/placeholder
			// frame (reference importer: consumed but not kept).
			for (uint32 i = 0; i < fc.frameCount; i++) {
				float time = 0;
				bool dummy = false;
				if (hfloat) {
					uint16 bits = ru16();
					if (bits == 0xFFFF)
						dummy = true;
					time = halfToFloat(bits);
				} else {
					time = rf32();
				}
				Common::Array<float> vals;
				for (uint32 v = 0; v < fc.valueCount; v++) {
					if (hfloat)
						vals.push_back(halfToFloat(ru16()));
					else
						vals.push_back(rf32());
				}
				if (!dummy) {
					fc.times.push_back(time);
					for (uint32 v = 0; v < vals.size(); v++)
						fc.values.push_back(vals[v]);
				}
			}
			motion.fcurves.push_back(fc);
			break;
		}
		default:
			break;
		}
		uint32 next = ch.offset + ch.nextOffs;
		if (next <= pos || next > stop + 8)
			break;
		pos = next;
	}
}

bool GMO::evaluateMotion(uint motionIndex, float frame, BonePose &bonePose, MaterialPose &materialPose) const {
	if (motionIndex >= _motions.size())
		return false;
	const Motion &motion = _motions[motionIndex];
	const int nb = (int)_bones.size();
	const int nm = (int)_materials.size();

	bonePose.boneCount = nb;
	bonePose.hasTranslate.clear(); bonePose.hasRotate.clear(); bonePose.hasScale.clear();
	bonePose.translate.clear(); bonePose.rotate.clear(); bonePose.scale.clear();
	bonePose.rotateType.clear();
	bonePose.hasTranslate.resize(nb, false);
	bonePose.hasRotate.resize(nb, false);
	bonePose.hasScale.resize(nb, false);
	bonePose.translate.resize(nb * 3, 0.0f);
	bonePose.rotate.resize(nb * 4, 0.0f);
	bonePose.scale.resize(nb * 3, 1.0f);
	bonePose.rotateType.resize(nb, 0);
	materialPose.materialCount = nm;
	materialPose.hasDiffuse.clear(); materialPose.hasEmission.clear();
	materialPose.hasAmbient.clear(); materialPose.hasSpecular.clear();
	materialPose.diffuse.clear(); materialPose.emission.clear();
	materialPose.ambient.clear(); materialPose.specular.clear();
	materialPose.hasDiffuse.resize(nm, false);
	materialPose.hasEmission.resize(nm, false);
	materialPose.hasAmbient.resize(nm, false);
	materialPose.hasSpecular.resize(nm, false);
	materialPose.diffuse.resize(nm * 4, 1.0f);
	materialPose.emission.resize(nm * 4, 0.0f);
	materialPose.ambient.resize(nm * 4, 0.0f);
	materialPose.specular.resize(nm * 4, 1.0f);

	bool any = false;
	for (uint ai = 0; ai < motion.animates.size(); ai++) {
		const Animate &a = motion.animates[ai];
		if (a.fcurve.index < 0 || a.fcurve.index >= (int)motion.fcurves.size())
			continue;
		const FCurve &fc = motion.fcurves[a.fcurve.index];
		if (fc.frameCount == 0 || fc.valueCount == 0 || fc.times.empty())
			continue;

		// Sample the curve at `frame`. The low nibble of the FCURVE type is
		// the interpolation mode (Sony SCEGMO_FCURVE): 0 constant (hold),
		// 1 linear, 2 hermite, 3 cubic, 4 spherical (slerp). 2/3 are
		// approximated by linear since no tangents are stored.
		const uint vc = fc.valueCount;
		const int n = (int)fc.times.size();
		const uint interp = fc.type & 0x0F;
		float vals[4] = {0, 0, 0, 0};
		auto keyVal = [&](int i, uint k) -> float {
			uint idx = (uint)i * vc + k;
			return idx < fc.values.size() ? fc.values[idx] : 0.0f;
		};
		if (n == 1 || frame <= fc.times[0]) {
			for (uint k = 0; k < vc && k < 4; k++) vals[k] = keyVal(0, k);
		} else if (frame >= fc.times[n - 1]) {
			for (uint k = 0; k < vc && k < 4; k++) vals[k] = keyVal(n - 1, k);
		} else {
			int i = 0;
			while (i + 1 < n && fc.times[i + 1] < frame)
				i++;
			if (interp == 0) {
				// Constant/hold: keep the previous key's value.
				for (uint k = 0; k < vc && k < 4; k++) vals[k] = keyVal(i, k);
			} else if (interp == 4 && vc == 4) {
				// Spherical: shortest-arc quaternion slerp.
				float q0[4], q1[4];
				for (int k = 0; k < 4; k++) { q0[k] = keyVal(i, k); q1[k] = keyVal(i + 1, k); }
				float dot = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
				if (dot < 0.0f) {
					for (int k = 0; k < 4; k++) q1[k] = -q1[k];
					dot = -dot;
				}
				float t0 = fc.times[i], t1 = fc.times[i + 1];
				float alpha = (t1 > t0) ? (frame - t0) / (t1 - t0) : 0.0f;
				if (dot > 0.9995f) {
					for (int k = 0; k < 4; k++) vals[k] = q0[k] * (1.0f - alpha) + q1[k] * alpha;
				} else {
					float theta = acosf(CLIP(dot, -1.0f, 1.0f));
					float sinTheta = sinf(theta);
					float w0 = sinf((1.0f - alpha) * theta) / sinTheta;
					float w1 = sinf(alpha * theta) / sinTheta;
					for (int k = 0; k < 4; k++) vals[k] = q0[k] * w0 + q1[k] * w1;
				}
			} else {
				float t0 = fc.times[i], t1 = fc.times[i + 1];
				float alpha = (t1 > t0) ? (frame - t0) / (t1 - t0) : 0.0f;
				for (uint k = 0; k < vc && k < 4; k++)
					vals[k] = keyVal(i, k) * (1.0f - alpha) + keyVal(i + 1, k) * alpha;
			}
		}

		if (a.block.type == kBone) {
			int bi = a.block.index;
			if (bi < 0 || bi >= nb)
				continue;
			switch (a.type) {
			case kTranslate:
				bonePose.hasTranslate[bi] = true;
				for (int k = 0; k < 3; k++) bonePose.translate[bi * 3 + k] = vals[k];
				any = true;
				break;
			case kScale:
				bonePose.hasScale[bi] = true;
				for (int k = 0; k < 3; k++) bonePose.scale[bi * 3 + k] = vals[k];
				any = true;
				break;
			case kRotateQ: {
				// Normalise the (linearly interpolated) quaternion: a non-unit
				// quaternion makes Quaternion::toMatrix() include a scale
				// factor, which scales child geometry and flings parts away.
				float q0 = vals[0], q1 = vals[1], q2 = vals[2], q3 = vals[3];
				float len = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
				if (len > 0.000001f) {
					q0 /= len; q1 /= len; q2 /= len; q3 /= len;
				}
				bonePose.hasRotate[bi] = true;
				bonePose.rotateType[bi] = 2;
				bonePose.rotate[bi * 4 + 0] = q0;
				bonePose.rotate[bi * 4 + 1] = q1;
				bonePose.rotate[bi * 4 + 2] = q2;
				bonePose.rotate[bi * 4 + 3] = q3;
				any = true;
				break;
			}
			case kRotateZYX:
			case kRotateYXZ:
				bonePose.hasRotate[bi] = true;
				bonePose.rotateType[bi] = (a.type == kRotateZYX) ? 0 : 1;
				for (int k = 0; k < 3; k++) bonePose.rotate[bi * 4 + k] = vals[k];
				any = true;
				break;
			default:
				break;
			}
		} else if (a.block.type == kMaterial) {
			int mi = a.block.index;
			if (mi < 0 || mi >= nm)
				continue;
			float *dst = nullptr;
			bool *flag = nullptr;
			switch (a.type) {
			case kDiffuse: dst = &materialPose.diffuse[mi * 4]; flag = &materialPose.hasDiffuse[mi]; break;
			case kEmission: dst = &materialPose.emission[mi * 4]; flag = &materialPose.hasEmission[mi]; break;
			case kAmbient: dst = &materialPose.ambient[mi * 4]; flag = &materialPose.hasAmbient[mi]; break;
			case kSpecular: dst = &materialPose.specular[mi * 4]; flag = &materialPose.hasSpecular[mi]; break;
			default: break;
			}
			if (dst) {
				for (int k = 0; k < 4; k++) dst[k] = vals[k];
				*flag = true;
				any = true;
			}
		}
	}
	return any;
}

Graphics::Surface *GMO::getTextureSurface(int i) const {	if (i < 0 || i >= (int)_textures.size() || _textures[i].image.empty())
		return nullptr;
	MemoryReadStream ms(_textures[i].image.data(), _textures[i].image.size(), DisposeAfterUse::NO);
	return Graphics::TM2(&ms).getSurface(0);
}

} // namespace Common
