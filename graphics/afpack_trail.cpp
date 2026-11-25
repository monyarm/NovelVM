#include "graphics/afpack_trail.h"

#include <cmath>

#include "common/endian.h"
#include "common/util.h"

namespace Graphics {

namespace {

const float kTick = 1.0f / 30;

float readFloat(const byte *p) {
	const uint32 bits = READ_LE_UINT32(p);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

void sub(const float a[3], const float b[3], float out[3]) {
	for (int k = 0; k < 3; ++k)
		out[k] = a[k] - b[k];
}

float dot(const float a[3], const float b[3]) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float length(const float v[3]) {
	return sqrtf(dot(v, v));
}

void cross(const float a[3], const float b[3], float out[3]) {
	const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
	out[0] = x;
	out[1] = y;
	out[2] = z;
}

/** v / |v|; a zero vector stays zero. */
void normalize(float v[3]) {
	const float l = length(v);
	if (l > 0)
		for (int k = 0; k < 3; ++k)
			v[k] /= l;
}

/** nrm(a - b). */
void direction(const float a[3], const float b[3], float out[3]) {
	sub(a, b, out);
	normalize(out);
}

} // End of anonymous namespace

AfPackTrail::AfPackTrail(const byte *data, uint32 size) {
	_ok = data && parse(data, size);
	if (!_ok)
		_sections.clear();
	else
		restart();
}

bool AfPackTrail::parse(const byte *data, uint32 size) {
	if (size < 0x10)
		return false;
	const int32 version = (int32)READ_LE_UINT32(data);
	if (version != 0x64 && version != 0x65)
		return false;
	const uint16 sectionCount = READ_LE_UINT16(data + 0x08), paramCount = READ_LE_UINT16(data + 0x0A),
		materialCount = READ_LE_UINT16(data + 0x0C);
	if (sectionCount > 16 || paramCount > 16 || materialCount > 16)
		return false;
	uint32 p = 0x10;
	if ((uint64)p + paramCount * 0x38 + materialCount * 28 > size)
		return false;
	for (uint i = 0; i < paramCount; ++i, p += 0x38) {
		Param param;
		const byte *r = data + p;
		param.id = READ_LE_UINT16(r);
		param.midAxis = readFloat(r + 0x04);
		param.midNewer = readFloat(r + 0x08);
		param.midOlder = readFloat(r + 0x0C);
		param.headAxis = readFloat(r + 0x10);
		param.headOlder = readFloat(r + 0x14);
		param.tailAxis = readFloat(r + 0x18);
		param.tailNewer = readFloat(r + 0x1C);
		param.kLen[0] = readFloat(r + 0x20);
		param.kTurn[0] = readFloat(r + 0x24);
		param.kLen[1] = readFloat(r + 0x28);
		param.kTurn[1] = readFloat(r + 0x2C);
		param.lengthScale = readFloat(r + 0x30);
		_params.push_back(param);
	}
	for (uint i = 0; i < materialCount; ++i, p += 28) {
		Material material;
		const byte *r = data + p;
		material.id = READ_LE_UINT16(r);
		material.textured = (READ_LE_UINT16(r + 2) & 1) != 0;
		if (READ_LE_UINT16(r + 4) != 0 || READ_LE_UINT16(r + 6) != 0)
			return false; // only plain colours and the global trail texture exist
		memcpy(material.colors, r + 0x0C, 16);
		_materials.push_back(material);
	}
	const uint32 recordSize = version == 0x65 ? 0x20 : 0x18;
	for (uint i = 0; i < sectionCount; ++i) {
		if ((uint64)p + recordSize > size)
			return false;
		const byte *r = data + p;
		Section section;
		const uint16 paramId = READ_LE_UINT16(r + 2), materialId = READ_LE_UINT16(r + 4);
		// Unmatched ids would read past the tables in the game; reject them.
		section.param = section.material = UINT_MAX;
		for (uint k = 0; k < _params.size(); ++k)
			if (_params[k].id == paramId && section.param == UINT_MAX)
				section.param = k;
		for (uint k = 0; k < _materials.size(); ++k)
			if (_materials[k].id == materialId && section.material == UINT_MAX)
				section.material = k;
		if (section.param == UINT_MAX || section.material == UINT_MAX)
			return false;
		section.start = (int32)READ_LE_UINT32(r + 0x08);
		section.end = (int32)READ_LE_UINT32(r + 0x0C);
		section.capacity = READ_LE_UINT16(r + 0x10);
		section.rows = READ_LE_UINT16(r + 0x12);
		section.mode = version == 0x65 ? READ_LE_UINT16(r + 0x14) : 0;
		const uint32 commandBytes = READ_LE_UINT32(r + recordSize - 4);
		p += recordSize;
		if (commandBytes > size - p || section.capacity < 2 || section.rows < 2)
			return false;
		for (uint32 k = 0; k + 2 <= commandBytes; k += 2)
			section.commands.push_back(READ_LE_UINT16(data + p + k));
		section.commands.push_back(0); // a stream running off its end stops there
		p += commandBytes;
		section.ring.resize(section.capacity);
		_sections.push_back(section);
	}
	return p == size;
}

bool AfPackTrail::finished() const {
	for (uint i = 0; i < _sections.size(); ++i)
		if (_sections[i].commands[_sections[i].cursor] != 0)
			return false;
	return true;
}

void AfPackTrail::restart() {
	for (uint i = 0; i < _sections.size(); ++i) {
		Section &s = _sections[i];
		s.cursor = 0;
		s.waiting = s.recording = s.fading = false;
		s.elapsed = 0;
		s.delay = 0;
		s.fadeFrames = 0;
		s.clock = s.fadeStart = s.fadeDuration = 0;
		s.writeIndex = s.count = 0;
		run(s);
	}
}

void AfPackTrail::run(Section &s) {
	const Common::Array<uint16> &c = s.commands;
	auto u32 = [&](uint32 at) { return at + 1 < c.size() ? (uint32)c[at] | ((uint32)c[at + 1] << 16) : 0; };
	for (;;) {
		const uint32 at = s.cursor;
		switch (c[at]) {
		case 0x0001: {
			// A wait reached in run r passes in run r + n, and the same run carries on.
			if (s.waiting)
				++s.elapsed;
			else {
				s.elapsed = 0;
				s.waiting = true;
			}
			if (s.elapsed < u32(at + 1))
				return;
			s.waiting = false;
			s.cursor = MIN<uint32>(at + 3, c.size() - 1);
			break;
		}
		case 0x0100:
			s.recording = true;
			++s.cursor;
			break;
		case 0x0101:
			s.recording = false;
			++s.cursor;
			break;
		case 0x0102:
			s.count = s.writeIndex = 0;
			++s.cursor;
			break;
		case 0x0200:
			if (s.delay == 0 && s.fadeFrames != 0) {
				s.fading = true;
				s.fadeStart = s.clock;
				s.fadeDuration = s.fadeFrames * kTick;
			}
			++s.cursor;
			break;
		case 0x0201:
			s.delay = at + 1 < c.size() ? (int16)c[at + 1] : 0;
			s.cursor = MIN<uint32>(at + 2, c.size() - 1);
			break;
		case 0x0202:
			s.fadeFrames = u32(at + 1);
			s.cursor = MIN<uint32>(at + 3, c.size() - 1);
			break;
		case 0x0400:
			// TODO: freeze keeps the trail in world space; the demo has no world placement, so
			// model and world space coincide and nothing changes.
			++s.cursor;
			break;
		default:
			// 0 is the end; the game would loop forever on unknown opcodes, treat them as the end.
			return;
		}
	}
}

void AfPackTrail::update(HelperLookup lookup, void *context) {
	for (uint i = 0; i < _sections.size(); ++i) {
		Section &s = _sections[i];
		run(s);
		if (s.recording) {
			float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; // a missing helper samples the origin
			lookup(context, (uint32)s.start, a);
			lookup(context, (uint32)s.end, b);
			push(s, _params[s.param], a, b);
		}
		s.clock += kTick;
	}
}

void AfPackTrail::push(Section &s, const Param &param, const float a[3], const float b[3]) {
	Sample &sample = s.ring[s.writeIndex];
	for (int k = 0; k < 3; ++k) {
		sample.edge[0][k] = a[k];
		sample.edge[1][k] = b[k];
	}
	s.count = MIN(s.count + 1, s.capacity);
	s.writeIndex = (s.writeIndex + 1) % s.capacity;
	if (s.count == 2)
		computeTangents(s, param, 0);
	else if (s.count >= 3) {
		computeTangents(s, param, 1);
		computeTangents(s, param, 0);
	}
}

void AfPackTrail::computeTangents(Section &s, const Param &param, uint32 segment) {
	const uint32 count = s.count;
	auto at = [&](uint32 i) -> Sample & { return s.ring[(s.writeIndex + s.capacity - 1 - i) % s.capacity]; };
	auto edge = [&](int e, uint32 i) -> const float * { return at(i).edge[e]; };
	// Neighbour used for the frame and side axes: the older sample, or the newer one at the end.
	auto other = [&](uint32 i) { return i + 1 < count ? i + 1 : i - 1; };

	auto frameVector = [&](uint32 i, float out[3]) {
		const uint32 j = other(i);
		float e1[3], e2[3], n[3], da[3], db[3];
		direction(edge(1, i), edge(0, i), e1);
		direction(edge(1, j), edge(0, j), e2);
		if (j == i + 1)
			cross(e2, e1, n);
		else
			cross(e1, e2, n);
		normalize(n);
		sub(edge(0, i), edge(0, j), da);
		sub(edge(1, i), edge(1, j), db);
		if (length(da) > length(db))
			cross(e1, n, out);
		else
			cross(n, e1, out);
	};
	auto sideAxis = [&](int e, uint32 i, float out[3]) {
		const uint32 j = other(i);
		float v[3], n[3];
		if (e == 0) {
			float d[3], f[3];
			direction(edge(1, i), edge(0, i), d);
			direction(edge(0, j), edge(0, i), f);
			cross(d, f, n);
			normalize(n);
			if (j == i + 1)
				cross(d, n, v);
			else
				cross(n, d, v);
		} else {
			float g[3], h[3];
			direction(edge(1, j), edge(1, i), g);
			direction(edge(0, i), edge(1, i), h);
			cross(g, h, n);
			normalize(n);
			if (j == i + 1)
				cross(n, h, v);
			else
				cross(h, n, v);
		}
		float frame[3];
		frameVector(i, frame);
		const float sign = dot(v, frame); // flip v to agree with the frame vector
		for (int k = 0; k < 3; ++k)
			out[k] = v[k] * sign;
		normalize(out);
	};
	auto newer = [&](int e, uint32 i, float out[3]) {
		if (i == 0)
			out[0] = out[1] = out[2] = 0;
		else
			direction(edge(e, i - 1), edge(e, i), out);
	};
	auto older = [&](int e, uint32 i, float out[3]) {
		if (i + 1 >= count)
			out[0] = out[1] = out[2] = 0;
		else
			direction(edge(e, i), edge(e, i + 1), out);
	};
	auto tangent = [&](int e, uint32 i, float wAxis, float wNewer, float wOlder, float magnitude, float out[3]) {
		float axis[3], n[3], o[3];
		sideAxis(e, i, axis);
		newer(e, i, n);
		older(e, i, o);
		for (int k = 0; k < 3; ++k)
			out[k] = wAxis * axis[k] + wNewer * n[k] + wOlder * o[k];
		normalize(out);
		for (int k = 0; k < 3; ++k)
			out[k] *= magnitude;
	};

	const uint32 seg = segment;
	float d[2][3], ea[3], eb[3];
	sub(edge(0, seg + 1), edge(0, seg), d[0]);
	sub(edge(1, seg + 1), edge(1, seg), d[1]);
	const float len[2] = {length(d[0]), length(d[1])};
	direction(edge(1, seg), edge(0, seg), ea);
	direction(edge(1, seg + 1), edge(0, seg + 1), eb);
	const float turn = (1 - dot(ea, eb)) / 2;
	// The edge that moved more gets the first coefficient pair.
	const int moreA = len[0] > len[1] ? 0 : 1;
	Sample &sample = at(seg);
	for (int e = 0; e < 2; ++e) {
		const int k = e == 0 ? moreA : 1 - moreA;
		const float m = param.kLen[k] * len[e] + param.kTurn[k] * turn;
		const bool head = count == 2 || seg == 0;
		const bool tail = count == 2 || (count == 3 && seg == 1);
		if (head)
			tangent(e, seg, param.headAxis, 0, param.headOlder, m, sample.leave[e]);
		else
			tangent(e, seg, param.midAxis, param.midNewer, param.midOlder, m, sample.leave[e]);
		if (tail)
			tangent(e, seg + 1, param.tailAxis, param.tailNewer, 0, m, sample.arrive[e]);
		else
			tangent(e, seg + 1, param.midAxis, param.midNewer, param.midOlder, m, sample.arrive[e]);
	}
}

void AfPackTrail::point(const Section &s, int e, float d, float lengthScale, float out[3]) {
	auto at = [&](uint32 i) -> const Sample & { return s.ring[(s.writeIndex + s.capacity - 1 - i) % s.capacity]; };
	if (d <= 0 || d >= 1) {
		const float *p = at(d <= 0 ? 0 : s.count - 1).edge[e];
		for (int k = 0; k < 3; ++k)
			out[k] = p[k];
		return;
	}
	// Both edges share one parameter, spaced by their combined movement (the scale cancels out,
	// only a non-positive one matters).
	const uint32 segments = s.count - 1;
	Common::Array<float> lengths(segments);
	float total = 0;
	for (uint32 i = 0; i < segments; ++i) {
		float da[3], db[3];
		sub(at(i + 1).edge[0], at(i).edge[0], da);
		sub(at(i + 1).edge[1], at(i).edge[1], db);
		lengths[i] = (length(da) + length(db)) * lengthScale;
		total += lengths[i];
	}
	uint32 seg = 0;
	float u = 0;
	if (total > 0) {
		float before = 0;
		seg = segments - 1;
		u = 1;
		for (uint32 i = 0; i < segments; ++i) {
			const float next = before + lengths[i] / total;
			if (d < next) {
				seg = i;
				u = lengths[i] > 0 ? (d - before) / (lengths[i] / total) : 0;
				break;
			}
			before = next;
		}
	}
	const Sample &a = at(seg), &b = at(seg + 1);
	const float v = 1 - u;
	for (int k = 0; k < 3; ++k) {
		const float p0 = a.edge[e][k], p1 = p0 - a.leave[e][k], p3 = b.edge[e][k], p2 = p3 + a.arrive[e][k];
		out[k] = v * v * v * p0 + 3 * u * v * v * p1 + 3 * u * u * v * p2 + u * u * u * p3;
	}
}

float AfPackTrail::fade(const Section &s) {
	if (!s.fading)
		return 1;
	if (s.fadeDuration <= 0)
		return s.clock < s.fadeStart ? 1 : 0;
	return MAX(0.0f, 1 - (s.clock - s.fadeStart) / s.fadeDuration);
}

void AfPackTrail::draw(uint32 modelColor, Common::Array<EffectBatch> &out) const {
	if (finished())
		return;
	for (uint i = 0; i < _sections.size(); ++i)
		drawSection(_sections[i], modelColor, out);
}

void AfPackTrail::drawSection(const Section &s, uint32 modelColor, Common::Array<EffectBatch> &out) const {
	if (s.count < 2 || (s.mode != 0 && s.mode != 1))
		return; // TODO: mode 2 (a camera-facing strip along edge A) is not used by any corpus file
	const Material &material = _materials[s.material];
	const float f = fade(s);
	// Colour along edge `key` (0: A, c0 -> c1; 1: B, c2 -> c3) at t, faded, as 0..255 bytes.
	auto color = [&](int key, float t, byte rgba[4]) {
		const byte *first = material.colors[2 * key], *second = material.colors[2 * key + 1];
		for (int c = 0; c < 4; ++c)
			rgba[c] = (byte)(int)(f * ((1 - t) * first[c] + t * second[c]));
	};
	struct Vertex {
		float p[3];
		float u, v;
		byte c[4];
	};
	Common::Array<Vertex> vertices;
	Common::Array<uint32> triangles;
	const uint32 n = s.rows;
	auto make = [&](int key, float t, float u, float v) {
		Vertex vertex;
		point(s, key, t, _params[s.param].lengthScale, vertex.p);
		vertex.u = u;
		vertex.v = v;
		color(key, t, vertex.c);
		return vertex;
	};
	auto tri = [&](uint32 a, uint32 b, uint32 c) {
		triangles.push_back(a);
		triangles.push_back(b);
		triangles.push_back(c);
	};
	if (s.mode == 0) {
		// Zig-zag: edge A at even indices, edge B at odd ones, half a row apart.
		vertices.resize(2 * n + 1);
		for (uint32 j = 0; j <= n; ++j) {
			const float d = j == 0 ? 0 : j == n ? 1 : (j - 0.5f) / (n - 1);
			vertices[2 * j] = make(0, d, d, 0);
		}
		for (uint32 j = 0; j < n; ++j) {
			const float d = j + 1 == n ? 1 : (float)j / (n - 1);
			vertices[2 * j + 1] = make(1, d, d, 1);
		}
		for (uint32 j = 0; j + 1 < n; ++j) {
			tri(2 * j, 2 * j + 1, 2 * j + 2);
			tri(2 * j + 1, 2 * j + 3, 2 * j + 2);
		}
		tri(2 * n - 2, 2 * n - 1, 2 * n);
	} else {
		// Rows of edge A, edge B and a centre line half a row further along.
		vertices.resize(3 * (n + 2));
		auto rowT = [&](uint32 r) { return r == 0 ? 0.0f : r == n + 1 ? 1.0f : (r - 0.5f) / (n + 0.5f); };
		for (uint32 r = 0; r <= n + 1; ++r) {
			vertices[3 * r] = make(0, rowT(r), rowT(r), 0);
			vertices[3 * r + 1] = make(1, rowT(r), rowT(r), 1);
		}
		for (uint32 r = 0; r <= n + 1; ++r) {
			Vertex &c = vertices[3 * r + 2];
			const Vertex &a0 = vertices[3 * r], &b0 = vertices[3 * r + 1];
			if (r == 0 || r == n + 1) {
				for (int k = 0; k < 3; ++k)
					c.p[k] = (a0.p[k] + b0.p[k]) / 2;
				for (int k = 0; k < 4; ++k)
					c.c[k] = (byte)((a0.c[k] + b0.c[k]) >> 1);
				c.u = r == 0 ? 0 : 1;
			} else {
				const Vertex &a1 = vertices[3 * (r + 1)], &b1 = vertices[3 * (r + 1) + 1];
				for (int k = 0; k < 3; ++k)
					c.p[k] = (a0.p[k] + b0.p[k] + a1.p[k] + b1.p[k]) / 4;
				for (int k = 0; k < 4; ++k)
					c.c[k] = (byte)((a0.c[k] + b0.c[k] + a1.c[k] + b1.c[k]) >> 2);
				c.u = r / (n + 0.5f);
			}
			c.v = 0.5f;
		}
		auto A = [](uint32 r) { return 3 * r; };
		auto B = [](uint32 r) { return 3 * r + 1; };
		auto C = [](uint32 r) { return 3 * r + 2; };
		tri(A(0), A(1), C(0));
		tri(C(0), B(1), B(0));
		for (uint32 r = 0; r < n; ++r) {
			tri(C(r), A(r + 1), C(r + 1));
			tri(C(r), C(r + 1), B(r + 1));
			tri(A(r + 1), A(r + 2), C(r + 1));
			tri(B(r + 1), C(r + 1), B(r + 2));
		}
		tri(C(n), A(n + 1), C(n + 1));
		tri(C(n), C(n + 1), B(n + 1));
	}

	EffectBatch batch;
	batch.blendMode = 2; // additive, depth test on, no depth writes
	if (material.textured)
		batch.texture.globalSlot = kTrailTextureSlot;
	float model[4];
	for (int c = 0; c < 4; ++c)
		model[c] = ((modelColor >> (8 * c)) & 0xFF) / 255.0f;
	for (uint i = 0; i < triangles.size(); ++i) {
		const Vertex &in = vertices[triangles[i]];
		EffectVertex v;
		v.x = in.p[0];
		v.y = in.p[1];
		v.z = in.p[2];
		v.u = in.u;
		v.v = in.v;
		v.r = in.c[0] / 255.0f * model[0];
		v.g = in.c[1] / 255.0f * model[1];
		v.b = in.c[2] / 255.0f * model[2];
		v.a = in.c[3] / 255.0f * model[3];
		batch.vertices.push_back(v);
	}
	out.push_back(batch);
}

} // End of namespace Graphics
