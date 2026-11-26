/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
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

#include "graphics/epl_effect.h"


#include "common/endian.h"
#include "common/memstream.h"
#include "common/util.h"
#include "graphics/rmd_render_mesh.h"
#include "graphics/surface.h"

namespace Graphics {

// ---------------------------------------------------------------------------
// Maths, random numbers
// ---------------------------------------------------------------------------

namespace EffectMath {

void rotate(const float q[4], const float v[3], float out[3]) {
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	const float r0[3] = {1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * z * w, 2 * x * z + 2 * y * w};
	const float r1[3] = {2 * x * y + 2 * z * w, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * x * w};
	const float r2[3] = {2 * x * z - 2 * y * w, 2 * y * z + 2 * x * w, 1 - 2 * x * x - 2 * y * y};
	for (int i = 0; i < 3; ++i)
		out[i] = v[0] * r0[i] + v[1] * r1[i] + v[2] * r2[i];
}

void multiply(const float a[4], const float b[4], float out[4]) {
	float r[4];
	r[0] = a[3] * b[0] + b[3] * a[0] + (a[1] * b[2] - a[2] * b[1]);
	r[1] = a[3] * b[1] + b[3] * a[1] + (a[2] * b[0] - a[0] * b[2]);
	r[2] = a[3] * b[2] + b[3] * a[2] + (a[0] * b[1] - a[1] * b[0]);
	r[3] = a[3] * b[3] - (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
	for (int i = 0; i < 4; ++i)
		out[i] = r[i];
}

void eulerToQuat(float ax, float ay, float az, float out[4]) {
	const float hx = -ax / 2, hy = -ay / 2, hz = -az / 2;
	const float cX = cosf(hx), sX = sinf(hx), cY = cosf(hy), sY = sinf(hy), cZ = cosf(hz), sZ = sinf(hz);
	out[0] = cX * sZ * sY + sX * cZ * cY;
	out[1] = cX * cZ * sY - sX * sZ * cY;
	out[2] = cX * sZ * cY + sX * cZ * sY;
	out[3] = cX * cZ * cY - sX * sZ * sY;
}

void matrixToPose(const float m[16], float position[3], float rotation[4]) {
	for (int k = 0; k < 3; ++k)
		position[k] = m[12 + k];
	auto norm = [](float v[3]) {
		const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (l > 0)
			for (int i = 0; i < 3; ++i)
				v[i] /= l;
	};
	auto cross = [](const float a[3], const float b[3], float o[3]) {
		o[0] = a[1] * b[2] - a[2] * b[1];
		o[1] = a[2] * b[0] - a[0] * b[2];
		o[2] = a[0] * b[1] - a[1] * b[0];
	};
	float right[3] = {m[0], m[1], m[2]}, up[3] = {m[4], m[5], m[6]}, at[3];
	norm(right);
	cross(right, up, at);
	norm(at);
	cross(at, right, up);
	// Rows of R(q) (see rotate()) are right, up, at.
	const float r[3][3] = {{right[0], right[1], right[2]}, {up[0], up[1], up[2]}, {at[0], at[1], at[2]}};
	const float trace = r[0][0] + r[1][1] + r[2][2];
	if (trace > 0) {
		const float s = sqrtf(trace + 1) * 2;
		rotation[3] = s / 4;
		rotation[0] = (r[2][1] - r[1][2]) / s;
		rotation[1] = (r[0][2] - r[2][0]) / s;
		rotation[2] = (r[1][0] - r[0][1]) / s;
	} else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
		const float s = sqrtf(1 + r[0][0] - r[1][1] - r[2][2]) * 2;
		rotation[3] = (r[2][1] - r[1][2]) / s;
		rotation[0] = s / 4;
		rotation[1] = (r[0][1] + r[1][0]) / s;
		rotation[2] = (r[0][2] + r[2][0]) / s;
	} else if (r[1][1] > r[2][2]) {
		const float s = sqrtf(1 + r[1][1] - r[0][0] - r[2][2]) * 2;
		rotation[3] = (r[0][2] - r[2][0]) / s;
		rotation[0] = (r[0][1] + r[1][0]) / s;
		rotation[1] = s / 4;
		rotation[2] = (r[1][2] + r[2][1]) / s;
	} else {
		const float s = sqrtf(1 + r[2][2] - r[0][0] - r[1][1]) * 2;
		rotation[3] = (r[1][0] - r[0][1]) / s;
		rotation[0] = (r[0][2] + r[2][0]) / s;
		rotation[1] = (r[1][2] + r[2][1]) / s;
		rotation[2] = s / 4;
	}
}

uint32 modulate(uint32 a, uint32 b) {
	uint32 out = 0;
	for (int c = 0; c < 4; ++c) {
		const float x = ((a >> (8 * c)) & 0xFF) / 255.0f, y = ((b >> (8 * c)) & 0xFF) / 255.0f;
		out |= (uint32)(int)(x * y * 255.0f) << (8 * c);
	}
	return out;
}

} // End of namespace EffectMath

namespace EffectRandom {

static uint32 s_state[4];
static bool s_seeded = false;

static uint32 rotr8(uint32 v) {
	return (v >> 8) | (v << 24);
}

void seed(uint32 seed) {
	s_state[0] = seed ^ 0x0AED1A0C;
	s_state[1] = rotr8(s_state[0]);
	s_state[2] = rotr8(s_state[1] ^ 0xAA5A02FE);
	s_state[3] = rotr8(s_state[2] ^ 0x11BE81C7);
	s_seeded = true;
}

uint32 next() {
	if (!s_seeded)
		seed(0x12345678);
	const uint32 r = ((s_state[1] << 2) | (s_state[0] >> 30)) ^ ((s_state[3] << 1) | (s_state[2] >> 31));
	s_state[3] = s_state[2];
	s_state[2] = s_state[1];
	s_state[1] = s_state[0];
	s_state[0] = r;
	return r;
}

float nextFloat() {
	return (next() & 0xFFFFFF) / 16777216.0f;
}

} // End of namespace EffectRandom

namespace {

using namespace EffectMath;

const float kPi = 3.14159265358979f;

float vary(float value, float variation) {
	return value * ((1 - variation) + variation * EffectRandom::nextFloat());
}

float signedRand() {
	return 2 * EffectRandom::nextFloat() - 1;
}

void normalize(float v[3]) {
	const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length > 0)
		for (int i = 0; i < 3; ++i)
			v[i] /= length;
}

uint32 lerpColor(uint32 a, uint32 b, float t) {
	uint32 out = 0;
	for (int c = 0; c < 3; ++c) {
		const float x = (a >> (8 * c)) & 0xFF, y = (b >> (8 * c)) & 0xFF;
		out |= (uint32)CLIP((int)(x + (y - x) * t), 0, 255) << (8 * c);
	}
	return out;
}

/** Little-endian view of a (scaled) parameter block. */
class Params {
public:
	Common::Array<byte> bytes;
	uint32 u32(uint32 off) const { return off + 4 <= bytes.size() ? READ_LE_UINT32(&bytes[off]) : 0; }
	int32 s32(uint32 off) const { return (int32)u32(off); }
	uint16 u16(uint32 off) const { return off + 2 <= bytes.size() ? READ_LE_UINT16(&bytes[off]) : 0; }
	byte u8(uint32 off) const { return off < bytes.size() ? bytes[off] : 0; }
	float f32(uint32 off) const {
		const uint32 bits = u32(off);
		float value;
		memcpy(&value, &bits, 4);
		return value;
	}
	void setF32(uint32 off, float value) {
		if (off + 4 > bytes.size())
			return;
		uint32 bits;
		memcpy(&bits, &value, 4);
		WRITE_LE_UINT32(&bytes[off], bits);
	}

	/**
	 * Colour keys (0x24 bytes at `keys`) and opacity block (0x10 bytes at `opacity`) sampled at
	 * frame f of duration d: RGB piecewise linear, alpha = alpha x fade envelope.
	 */
	uint32 sampleColor(uint32 keys, uint32 opacity, int32 f, int32 d) const {
		const uint32 start = u32(keys + 0x04) & 0xFFFFFF;
		const uint32 alpha = u32(opacity);
		if (d <= 0)
			return start | (MIN<uint32>(alpha, 255) << 24);
		uint32 rgb = start;
		const uint32 end = u32(keys + 0x08), mid1 = u32(keys + 0x0C), mid2 = u32(keys + 0x14);
		const int32 f1 = (int32)(f32(keys + 0x10) * d), f2 = (int32)(f32(keys + 0x18) * d);
		switch (u8(keys)) {
		case 0:
			rgb = lerpColor(start, end, (float)f / d);
			break;
		case 1:
			rgb = f < f1 ? lerpColor(start, mid1, f1 ? (float)f / f1 : 1) : lerpColor(mid1, end, d > f1 ? (float)(f - f1) / (d - f1) : 1);
			break;
		case 2:
			if (f < f1)
				rgb = lerpColor(start, mid1, f1 ? (float)f / f1 : 1);
			else if (f < f2)
				rgb = lerpColor(mid1, mid2, f2 > f1 ? (float)(f - f1) / (f2 - f1) : 1);
			else
				rgb = lerpColor(mid2, end, d > f2 ? (float)(f - f2) / (d - f2) : 1);
			break;
		default:
			break;
		}
		const int32 fi = (int32)(f32(opacity + 0x08) * d), fo = (int32)(f32(opacity + 0x0C) * d);
		float envelope = 1;
		if (f < fi)
			envelope = (float)f / fi;
		else if (f > fo)
			envelope = d > fo ? (float)(d - f) / (d - fo) : 0;
		const int32 a = CLIP((int32)(alpha * envelope), 0, 255);
		return (rgb & 0xFFFFFF) | ((uint32)a << 24);
	}

	/** Scalar keys (0x2C bytes at `keys`) at frame f of duration d. */
	float sampleScalar(uint32 keys, int32 f, int32 d) const {
		const float start = f32(keys + 0x04);
		if (d <= 0)
			return start;
		const float end = f32(keys + 0x08), mid1 = f32(keys + 0x14), mid2 = f32(keys + 0x1C);
		const int32 f1 = (int32)(f32(keys + 0x18) * d), f2 = (int32)(f32(keys + 0x20) * d);
		auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
		switch (u8(keys)) {
		case 0:
			return lerp(start, end, (float)f / d);
		case 1:
			return f < f1 ? lerp(start, mid1, f1 ? (float)f / f1 : 1) : lerp(mid1, end, d > f1 ? (float)(f - f1) / (d - f1) : 1);
		case 2:
			if (f < f1)
				return lerp(start, mid1, f1 ? (float)f / f1 : 1);
			if (f < f2)
				return lerp(mid1, mid2, f2 > f1 ? (float)(f - f1) / (f2 - f1) : 1);
			return lerp(mid2, end, d > f2 ? (float)(f - f2) / (d - f2) : 1);
		default:
			return start;
		}
	}
};

void colorToFloats(uint32 c, float out[4]) {
	for (int i = 0; i < 4; ++i)
		out[i] = ((c >> (8 * i)) & 0xFF) / 255.0f;
}

EffectTextureRef textureFor(const Common::EPL::Record &record) {
	EffectTextureRef ref;
	if (record.resourceKind == Common::EPL::kResourceTextureSlot && record.resource.size() >= 4) {
		const uint32 slot = READ_LE_UINT32(record.resource.data());
		if (slot < 22)
			ref.globalSlot = (int)slot;
	} else if (record.resourceKind == Common::EPL::kResourceTMX && !record.resource.empty()) {
		ref.embeddedTMX = &record.resource;
	}
	return ref;
}

// ---------------------------------------------------------------------------
// Particle emitter (kind 1): common emitter parameters, models 1 (wave), 3 (helix) and
// 5 (cone), camera-facing textured billboards.
// ---------------------------------------------------------------------------

class ParticleEmitter : public EffectPrimitive {
public:
	/** Emitter parameters start at `paramOffset` in the record's parameter block. */
	ParticleEmitter(const Common::EPL::Record &record, EffectTextureSource *textures, uint32 paramOffset = 0,
			uint32 maxCapacity = 300, bool trails = true)
			: _record(record), _textures(textures) {
		if (paramOffset < record.parameters.size())
			_original.bytes = Common::Array<byte>(record.parameters.data() + paramOffset, record.parameters.size() - paramOffset);
		_params = _original;
		_texture = textureFor(_record);
		const int32 emit = _params.s32(0x20), life = _params.s32(0xB8);
		const uint32 rate = _params.u32(0x24);
		_capacity = (uint32)CLIP<int64>((int64)(emit ? emit : life) * rate, 0, maxCapacity);
		_trailCount = trails ? MAX(0, _params.s32(0xC0)) : 0;
		_trailWidth = trails ? MAX(0, _params.s32(0xC4)) : 0;
		_trailLength = (uint32)(_trailCount * _trailWidth);
		_slots.resize(_capacity * (_trailLength + 1));
	}

	void reset() override { _ticks = 0; }
	void clearHistory() override { _accumulator = 0; }
	void setPosition(const float p[3]) override { for (int i = 0; i < 3; ++i) _emitter[i] = p[i]; }
	void setRotation(const float q[4]) override { for (int i = 0; i < 4; ++i) _rotation[i] = q[i]; }
	void setScale(float scale) override {
		// Rewrite the scaled fields from the pristine copy: size keys and distance-like model fields.
		static const uint32 kSize[] = {0x64, 0x68, 0x74, 0x7C, 0x84};
		static const uint32 kWave[] = {0xC8, 0xCC, 0xD4, 0xD8, 0xE0};
		static const uint32 kHelix[] = {0xC8, 0xCC, 0xD4, 0xE4, 0xF0};
		static const uint32 kCone[] = {0xC8, 0xD8, 0xE0, 0xE4};
		static const uint32 kBounce[] = {0xC8, 0xDC, 0xE4, 0xE8};
		_params = _original;
		for (uint32 off : kSize)
			_params.setF32(off, _original.f32(off) * scale);
		const uint32 *fields = nullptr;
		uint32 count = 0;
		switch (_record.sub) {
		case 1: fields = kWave; count = ARRAYSIZE(kWave); break;
		case 3: fields = kHelix; count = ARRAYSIZE(kHelix); break;
		case 5: fields = kCone; count = ARRAYSIZE(kCone); break;
		case 8: fields = kBounce; count = ARRAYSIZE(kBounce); break;
		default: break;
		}
		for (uint32 i = 0; i < count; ++i)
			_params.setF32(fields[i], _original.f32(fields[i]) * scale);
	}

	void advance() override {
		const int32 lifetime = _params.s32(0xB8);
		if (lifetime <= 0 || _capacity == 0 || !supported())
			return;
		if (_ticks == 0)
			for (uint32 i = 0; i < _slots.size(); ++i)
				_slots[i].age = -1;
		const int32 emitDuration = _params.s32(0x20);
		const float rate = (float)_params.u32(0x24), rateVariation = _params.f32(0x28);
		bool preroll = false;
		uint32 budget;
		if (emitDuration != 0 && _ticks >= emitDuration) {
			budget = 0;
		} else if (_ticks == 0 && _params.u8(0xBD) && _record.sub != 8) {
			budget = rateVariation > 0 ? (uint32)(_capacity * (1.1f - rateVariation) * EffectRandom::nextFloat()) : _capacity;
			preroll = true;
		} else {
			_accumulator += rateVariation > 0 ? rate * (1.1f - rateVariation) * EffectRandom::nextFloat() : rate;
			budget = (uint32)fabsf(_accumulator);
			_accumulator -= budget;
		}
		for (uint32 i = 0; i < _capacity; ++i) {
			Particle &p = _slots[i];
			if (p.age >= lifetime) {
				p.age = emitDuration ? -2 : -1;
				for (uint32 t = 0; t < _trailLength; ++t)
					_slots[_capacity + i * _trailLength + t].age = -1;
			}
			if (p.age == -2)
				continue;
			if (p.age == -1) {
				if (budget == 0)
					continue;
				--budget;
				spawn(p);
				spawned(i);
				if (preroll) {
					const int32 age = (int32)(EffectRandom::next() % (uint32)lifetime);
					prerollAdvance(p, age);
					p.age = age;
				}
				applyKeys(p, lifetime);
				// A fresh particle leaves its trail alone; a prerolled one only seeds slot 0.
				if (preroll) {
					++p.age;
					if (_trailLength) {
						_slots[_capacity + i * _trailLength] = p;
						_slots[_capacity + i * _trailLength].age = -1;
					}
				}
				continue;
			}
			move(p);
			applyKeys(p, lifetime);
			if (_trailLength)
				updateTrail(i);
			++p.age;
		}
		++_ticks;
	}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		if (_ticks == 0 || !_texture.valid())
			return;
		int texW = 32, texH = 32;
		if (_textures)
			_textures->textureSize(_texture, texW, texH);
		const float scaleX = texW / 32.0f, scaleY = texH / 32.0f;
		EffectBatch batch;
		batch.blendMode = _params.u16(0x54);
		batch.texture = _texture;
		const bool local = _params.u8(0xBC) != 0;
		for (uint32 i = 0; i < _slots.size(); ++i) {
			const Particle &p = _slots[i];
			if (p.age < 0)
				continue;
			float pos[3] = {p.position[0], p.position[1], p.position[2]};
			if (local) {
				float r[3];
				rotate(_rotation, pos, r);
				for (int k = 0; k < 3; ++k)
					pos[k] = r[k] + _emitter[k];
			}
			float angle = p.angle;
			if (p.angleMode == 2) {
				// Rotation follows the particle's on-screen motion since the previous frame, measured
				// from the new position back to the old one on the raw (unposed) positions.
				// shortcut: the game projects with its perspective camera; this uses the camera's
				// screen axes. No corpus particle uses this mode.
				float d[3];
				for (int k = 0; k < 3; ++k)
					d[k] = p.previous[k] - p.position[k];
				const float sx = d[0] * camera.right[0] + d[1] * camera.right[1] + d[2] * camera.right[2];
				const float sy = d[0] * camera.up[0] + d[1] * camera.up[1] + d[2] * camera.up[2];
				angle = (sx != 0 || sy != 0) ? atan2f(sy, sx) : 0;
			}
			const float hx = scaleX * p.size / 2, hy = scaleY * p.size / 2;
			const float c = cosf(angle), s = sinf(angle);
			float rgba[4];
			colorToFloats(modulate(p.color, nodeColor), rgba);
			static const float corner[4][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
			EffectVertex v[4];
			for (int k = 0; k < 4; ++k) {
				const float dx = corner[k][0] * hx, dy = corner[k][1] * hy;
				const float rx = c * dx - s * dy, ry = s * dx + c * dy;
				for (int a = 0; a < 3; ++a)
					(&v[k].x)[a] = pos[a] + rx * camera.right[a] + ry * camera.up[a];
				v[k].u = corner[k][2];
				v[k].v = corner[k][3];
				v[k].r = rgba[0]; v[k].g = rgba[1]; v[k].b = rgba[2]; v[k].a = rgba[3];
			}
			static const int kIndices[6] = {0, 1, 2, 0, 2, 3};
			for (int k = 0; k < 6; ++k)
				batch.vertices.push_back(v[kIndices[k]]);
		}
		if (!batch.vertices.empty())
			out.push_back(batch);
	}

protected:
	struct Particle {
		float position[3] = {0, 0, 0};
		float previous[3] = {0, 0, 0};
		int32 age = -1;
		uint32 color = 0;
		float size = 0, angle = 0;
		float sizeFactor = 1, angleFactor = 1, angleOffset = 0;
		int angleMode = 0;
		// model state
		float axis[3] = {0, 0, 0}, wobble[3] = {0, 0, 0};
		float speed = 0, amplitude = 0, amplitudeStep = 0, phase = 0, phaseStep = 0, previousSin = 0;
		float radius = 0, radiusStep = 0, height = 0, rise = 0, omega = 0, bounce = 0;
		float spawnRotation[4] = {0, 0, 0, 1}, spawnPosition[3] = {0, 0, 0};
	};

	Common::EPL::Record _record;
	EffectTextureSource *_textures;
	EffectTextureRef _texture;
	Params _original, _params;
	float _emitter[3] = {0, 0, 0};
	float _rotation[4] = {0, 0, 0, 1};
	uint32 _capacity = 0, _trailLength = 0;
	int32 _trailCount = 0, _trailWidth = 0;
	Common::Array<Particle> _slots;
	int32 _ticks = 0;
	float _accumulator = 0;

	bool supported() const { return _record.sub == 1 || _record.sub == 3 || _record.sub == 5 || _record.sub == 8; }
	/** Called after primary particle slot `index` is born. */
	virtual void spawned(uint32 index) {}
	bool local() const { return _params.u8(0xBC) != 0; }

	void toWorld(const float in[3], float out[3]) const {
		if (local()) {
			for (int k = 0; k < 3; ++k)
				out[k] = in[k];
			return;
		}
		rotate(_rotation, in, out);
	}

	void spawn(Particle &p) {
		p = Particle();
		p.age = 0;
		const int32 lifetime = MAX(1, _params.s32(0xB8));
		const float *origin = local() ? nullptr : _emitter;
		switch (_record.sub) {
		case 1: { // wave: straight line motion with a sine wobble
			const float up[3] = {0, 1, 0};
			toWorld(up, p.axis);
			float w[3] = {signedRand(), 0, signedRand()};
			normalize(w);
			toWorld(w, p.wobble);
			p.speed = vary(_params.f32(0xCC), _params.f32(0xD0));
			p.amplitude = vary(_params.f32(0xD8), _params.f32(0xDC));
			const float a1 = vary(_params.f32(0xE0), _params.f32(0xE4));
			p.amplitudeStep = (a1 - p.amplitude) / lifetime;
			p.phaseStep = vary(_params.f32(0xE8), _params.f32(0xEC));
			p.phase = 2 * kPi * EffectRandom::nextFloat();
			p.previousSin = sinf(p.phase);
			const float spread = _params.f32(0xC8) * signedRand();
			for (int k = 0; k < 3; ++k)
				p.position[k] = p.wobble[k] * spread + (origin ? origin[k] : 0);
			break;
		}
		case 3: { // helix around the emitter's up axis
			p.radius = vary(_params.f32(0xCC), _params.f32(0xD0));
			p.radiusStep = (vary(_params.f32(0xD4), _params.f32(0xD8)) - p.radius) / lifetime;
			p.phase = 2 * kPi * EffectRandom::nextFloat();
			p.omega = vary(_params.f32(0xDC), _params.f32(0xE0));
			p.rise = vary(_params.f32(0xE4), _params.f32(0xE8));
			p.height = _params.f32(0xC8) * EffectRandom::nextFloat();
			for (int k = 0; k < 4; ++k)
				p.spawnRotation[k] = _rotation[k];
			for (int k = 0; k < 3; ++k)
				p.spawnPosition[k] = _emitter[k];
			helixPosition(p, p.phase);
			break;
		}
		case 8: { // bounce: launched downward within the cone, bouncing off a floor height
			const float cone = vary(_params.f32(0xD0), _params.f32(0xD4));
			float d[3] = {cone * signedRand(), -(1 - cone), cone * signedRand()};
			normalize(d);
			toWorld(d, p.axis);
			const float speed = _params.f32(0xDC);
			p.speed = fabsf(vary(speed, _params.f32(0xE0)));
			p.bounce = -vary(_params.f32(0xEC), _params.f32(0xF0));
			const float spread = vary(_params.f32(0xC8), _params.f32(0xCC)) * (speed < 0 ? -1 : 1);
			for (int k = 0; k < 3; ++k)
				p.position[k] = p.axis[k] * spread + (origin ? origin[k] : 0);
			break;
		}
		case 5: { // cone: launched along a random direction within the cone
			const float cone = vary(_params.f32(0xD0), _params.f32(0xD4));
			float d[3] = {cone * signedRand(), 1 - cone, cone * signedRand()};
			normalize(d);
			toWorld(d, p.axis);
			const float speed = _params.f32(0xD8);
			p.speed = fabsf(vary(speed, _params.f32(0xDC)));
			const float spread = vary(_params.f32(0xC8), _params.f32(0xCC)) * (speed < 0 ? -1 : 1);
			for (int k = 0; k < 3; ++k)
				p.position[k] = p.axis[k] * spread + (origin ? origin[k] : 0);
			break;
		}
		default:
			break;
		}
		// The size/angle factors are drawn after the model's own random values.
		const float sizeVariation = _params.f32(0x6C), angleVariation = _params.f32(0x98);
		p.angleMode = _params.u8(0x9C);
		p.sizeFactor = (1 - sizeVariation) + sizeVariation * EffectRandom::nextFloat();
		if (p.angleMode != 2)
			p.angleFactor = (1 - angleVariation) + angleVariation * EffectRandom::nextFloat();
		if (p.angleMode == 1) {
			p.angleOffset = 2 * kPi * EffectRandom::nextFloat();
			if (EffectRandom::next() & 1)
				p.angleFactor = -p.angleFactor;
		}
		for (int k = 0; k < 3; ++k)
			p.previous[k] = p.position[k];
	}

	void helixPosition(Particle &p, float theta) {
		const float local[3] = {p.radius * cosf(theta), p.height, p.radius * sinf(theta)};
		if (this->local()) {
			for (int k = 0; k < 3; ++k)
				p.position[k] = local[k];
			return;
		}
		float r[3];
		rotate(p.spawnRotation, local, r);
		for (int k = 0; k < 3; ++k)
			p.position[k] = r[k] + p.spawnPosition[k];
	}

	void prerollAdvance(Particle &p, int32 t) {
		switch (_record.sub) {
		case 1: {
			const float gravity = _params.f32(0xD4);
			const float along = p.speed * t - 0.5f * gravity * t * t;
			p.phase += p.phaseStep * t;
			p.amplitude += p.amplitudeStep * t;
			const float s = sinf(p.phase);
			for (int k = 0; k < 3; ++k)
				p.position[k] += p.axis[k] * along + p.wobble[k] * p.amplitude * s;
			p.previousSin = s;
			p.phase += p.phaseStep;
			p.amplitude += p.amplitudeStep;
			break;
		}
		case 3: {
			const float gravity = _params.f32(0xF0);
			p.height += p.rise * t - 0.5f * gravity * t * t;
			p.radius += p.radiusStep * t;
			helixPosition(p, p.phase); // preroll keeps the spawn angle; only height and radius advance
			break;
		}
		case 5: {
			const float accel = _params.f32(0xE0), gravity = _params.f32(0xE4);
			const float distance = MAX(0.0f, p.speed * t + 0.5f * accel * t * t);
			for (int k = 0; k < 3; ++k)
				p.position[k] += p.axis[k] * distance;
			p.position[1] -= 0.5f * gravity * t * t;
			break;
		}
		default:
			break;
		}
		for (int k = 0; k < 3; ++k)
			p.previous[k] = p.position[k];
	}

	void move(Particle &p) {
		for (int k = 0; k < 3; ++k)
			p.previous[k] = p.position[k];
		const float age = (float)p.age;
		switch (_record.sub) {
		case 1: {
			const float s = sinf(p.phase);
			const float along = p.speed - _params.f32(0xD4) * age;
			for (int k = 0; k < 3; ++k)
				p.position[k] += p.wobble[k] * p.amplitude * (s - p.previousSin) + p.axis[k] * along;
			p.phase += p.phaseStep;
			p.amplitude += p.amplitudeStep;
			p.previousSin = s;
			break;
		}
		case 3: {
			const float theta = age * (p.omega + 0.5f * _params.f32(0xEC) * age) + p.phase;
			p.height += p.rise - _params.f32(0xF0) * age;
			p.radius += p.radiusStep;
			helixPosition(p, theta);
			break;
		}
		case 8: {
			const float step = MAX(0.0f, p.speed + _params.f32(0xE4) * age);
			p.axis[1] -= _params.f32(0xE8);
			p.position[0] += p.axis[0] * step;
			p.position[1] += p.axis[1];
			p.position[2] += p.axis[2] * step;
			const float floor = _params.f32(0xD8);
			if (p.position[1] < floor) {
				p.bounce *= _params.f32(0xF4);
				p.axis[1] *= p.bounce;
				p.position[1] = floor + p.bounce * (p.position[1] - floor);
			}
			break;
		}
		case 5: {
			const float step = MAX(0.0f, p.speed + _params.f32(0xE0) * age);
			for (int k = 0; k < 3; ++k)
				p.position[k] += p.axis[k] * step;
			p.position[1] -= _params.f32(0xE4) * age;
			break;
		}
		default:
			break;
		}
	}

	void applyKeys(Particle &p, int32 lifetime) {
		p.color = _params.sampleColor(0x2C, 0x50, p.age, lifetime);
		p.size = _params.sampleScalar(0x60, p.age, lifetime) * p.sizeFactor;
		float angle = _params.sampleScalar(0x8C, p.age, lifetime) * p.angleFactor + p.angleOffset;
		while (angle > kPi)
			angle -= 2 * kPi;
		while (angle <= -kPi)
			angle += 2 * kPi;
		p.angle = angle;
	}

	// Trail history behind primary particle i: slots shift back trailWidth at a time each frame
	// and fade linearly toward the tail; intermediate slots are interpolated.
	void updateTrail(uint32 i) {
		Particle &head = _slots[i];
		Particle *trail = &_slots[_capacity + i * _trailLength];
		if (head.age == 0) {
			for (uint32 t = 0; t < _trailLength; ++t)
				trail[t].age = -1;
			trail[0] = head;
			trail[0].age = -1;
			return;
		}
		trail[0].age = head.age - 1;
		for (int32 t = (int32)_trailLength - 1; t >= _trailWidth; --t)
			trail[t] = trail[t - _trailWidth];
		const uint32 headAlpha = head.color >> 24;
		for (uint32 t = 0; t < _trailLength; ++t) {
			const uint32 k = _trailLength - 1 - t; // 0 at the tail
			trail[t].color = (trail[t].color & 0xFFFFFF) | ((headAlpha * k / _trailLength) << 24);
		}
		const Particle previousHead = trail[_trailWidth < (int32)_trailLength ? _trailWidth : 0];
		trail[0] = head;
		trail[0].age = -1;
		if (_trailWidth > 1 && _trailCount >= 2 && previousHead.age >= 0) {
			// With enough history the in-between slots follow a Catmull-Rom curve through the
			// head two steps back, the previous head and the current head (doubled).
			const bool curved = head.age >= 2 && _trailCount >= 3;
			const Particle *older = curved && 2 * _trailWidth < (int32)_trailLength ? &trail[2 * _trailWidth] : nullptr;
			if (curved && (!older || older->age < 0))
				return;
			for (int32 k = _trailWidth - 1; k >= 1; --k) {
				const float t = (float)(_trailWidth - k) / (_trailWidth + 1); // from the previous head
				Particle &slot = trail[k];
				slot = previousHead;
				for (int a = 0; a < 3; ++a) {
					const float p1 = previousHead.position[a], p2 = head.position[a];
					if (older) {
						const float p0 = older->position[a], p3 = p2;
						slot.position[a] = 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
							(-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
					} else {
						slot.position[a] = p1 + (p2 - p1) * t;
					}
				}
				slot.size = previousHead.size + (head.size - previousHead.size) * t;
				slot.angle = previousHead.angle + (head.angle - previousHead.angle) * t;
				slot.color = (lerpColor(previousHead.color, head.color, t) & 0xFFFFFF) | (previousHead.color & 0xFF000000);
				slot.age = head.age;
			}
		}
	}
};

/**
 * A small RenderWare model embedded in an effect (texture dictionary + clump, optionally an
 * animation): it parses like an RMD without Persona chunks. Drawn as effect triangles.
 */
class EmbeddedModel : Common::NonCopyable {
public:
	~EmbeddedModel() {
		for (uint i = 0; i < _surfaces.size(); ++i)
			if (_surfaces[i]) {
				_surfaces[i]->free();
				delete _surfaces[i];
			}
	}

	void load(const Common::Array<byte> &stream) {
		if (stream.empty())
			return;
		Common::MemoryReadStream in(stream.data(), stream.size());
		_rmd.reset(new Common::RMD(&in));
		if (!_rmd->ok()) {
			_rmd.reset();
			return;
		}
		_surfaces.resize(_rmd->textures().size());
		for (uint i = 0; i < _surfaces.size(); ++i)
			_surfaces[i] = _rmd->getTextureSurface(i);
		pose(-1);
	}

	/** Rebuild the model-space mesh at `time` seconds of animation 0 (< 0: bind pose). */
	void pose(float time) {
		if (!_rmd)
			return;
		Common::RMD::BonePose bones;
		const bool animated = time >= 0 && !_rmd->animations().empty() && _rmd->animations()[0].duration > 0 &&
			_rmd->evaluateAnimation(0, fmodf(time, _rmd->animations()[0].duration), bones);
		RMDRenderMesh mesh;
		assembleRMDMesh(*_rmd, _surfaces, 1, 1, 0.0f, animated ? &bones : nullptr, nullptr, mesh, nullptr, nullptr, false);
		_mesh = mesh.triangles;
	}

	const Common::Array<RMDRenderTriangle> &mesh() const { return _mesh; }

	/**
	 * Append the mesh placed by (v x scale) R(rotation) + position, coloured `rgba` (0..1), into
	 * one batch per texture among out[firstBatch..].
	 */
	void emit(Common::Array<EffectBatch> &out, uint32 firstBatch, int blendMode, const float rotation[4], float scale,
			const float position[3], const float rgba[4]) const {
		for (uint t = 0; t < _mesh.size(); ++t) {
			const RMDRenderTriangle &tri = _mesh[t];
			EffectBatch *batch = nullptr;
			for (uint b = firstBatch; b < out.size() && !batch; ++b)
				if (out[b].texture.surface == tri.texture && out[b].blendMode == blendMode)
					batch = &out[b];
			if (!batch) {
				out.push_back(EffectBatch());
				batch = &out.back();
				batch->blendMode = blendMode;
				batch->texture.surface = tri.texture;
			}
			for (int k = 0; k < 3; ++k) {
				const RMDRenderVertex &in = tri.v[k];
				const float scaled[3] = {in.x * scale, in.y * scale, in.z * scale};
				float world[3];
				rotate(rotation, scaled, world);
				EffectVertex v;
				v.x = world[0] + position[0];
				v.y = world[1] + position[1];
				v.z = world[2] + position[2];
				v.u = in.u;
				v.v = in.v;
				// The effect overrides the material colours; prelight only tints untextured meshes
				// (textured ones can carry black prelight, as RMDRenderer notes).
				const bool prelit = tri.texture == nullptr;
				v.r = rgba[0] * (prelit ? in.r : 1); v.g = rgba[1] * (prelit ? in.g : 1); v.b = rgba[2] * (prelit ? in.b : 1);
				v.a = rgba[3] * in.a;
				batch->vertices.push_back(v);
			}
		}
	}

private:
	Common::ScopedPtr<Common::RMD> _rmd;
	Common::Array<Graphics::Surface *> _surfaces;
	Common::Array<RMDRenderTriangle> _mesh;
};

// Kind 28: the particle emitter (after a 0x44-byte header) drawing a small embedded RW model per
// particle, spinning about a per-particle axis and tinted by an optional six-colour palette.
class ObjectParticleEmitter : public ParticleEmitter {
public:
	ObjectParticleEmitter(const Common::EPL::Record &record, EffectTextureSource *textures)
			: ParticleEmitter(record, textures, 0x44, 100, false) {
		_header.bytes = record.parameters;
		_header.bytes.resize(MIN<uint>(record.parameters.size(), 0x44));
		_spin.resize(_slots.size());
		if (record.resourceKind == Common::EPL::kResourceRWStream)
			_model.load(record.resource);
	}

	void setScale(float scale) override {
		ParticleEmitter::setScale(scale);
		_instanceScale = scale;
	}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		if (_ticks == 0 || _model.mesh().empty())
			return;
		const float spinAccel = _header.f32(0x1C);
		const bool normaliseSize = _header.u16(0x20) != 0;
		const uint32 firstBatch = out.size();
		for (uint i = 0; i < _capacity; ++i) {
			const Particle &p = _slots[i];
			if (p.age < 0)
				continue;
			const Spin &spin = _spin[i];
			// Spin about the particle's axis (none while the spin angle is negative), then the
			// emitter rotation; uniform size; particle position (local mode: in the emitter pose).
			const float f = spin.omega * p.age + 0.5f * spinAccel * p.age * p.age;
			float q[4] = {0, 0, 0, 1};
			if (f >= 0) {
				const float half = (f + spin.startAngle) / 2;
				for (int k = 0; k < 3; ++k)
					q[k] = spin.axis[k] * sinf(half);
				q[3] = cosf(half);
			}
			float rotation[4];
			multiply(q, _rotation, rotation);
			const float size = normaliseSize && _instanceScale != 0 ? p.size / _instanceScale : p.size;
			float position[3] = {p.position[0], p.position[1], p.position[2]};
			if (local()) {
				float r[3];
				rotate(_rotation, position, r);
				for (int k = 0; k < 3; ++k)
					position[k] = r[k] + _emitter[k];
			}
			float rgba[4];
			colorToFloats(modulate(modulate(p.color, nodeColor), paletteColor(spin.palette, p.age)), rgba);
			_model.emit(out, firstBatch, _params.u16(0x54), rotation, size, position, rgba);
		}
		// The meshes cull back faces, depth-test per header byte 0x22, and in blend mode 0 also
		// write depth.
		for (uint b = firstBatch; b < out.size(); ++b) {
			out[b].cullBack = true;
			out[b].depthTest = (_header.u8(0x22) & 1) != 0;
			out[b].depthWrite = _params.u32(0x54) == 0; // the whole word, not just the list
		}
	}

private:
	struct Spin {
		float axis[3] = {0, 1, 0};
		float omega = 0, startAngle = 0;
		int32 palette = 0;
	};
	Params _header;
	Common::Array<Spin> _spin;
	EmbeddedModel _model;
	float _instanceScale = 1;

	void spawned(uint32 index) override {
		Spin &s = _spin[index];
		s = Spin();
		if (_header.u8(0x00)) {
			float axis[3] = {signedRand(), signedRand(), signedRand()};
			const float length = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
			if (length > 0)
				for (int k = 0; k < 3; ++k)
					s.axis[k] = axis[k] / length;
		} else {
			float axis[3] = {_header.f32(0x04), _header.f32(0x08), _header.f32(0x0C)};
			normalize(axis);
			if (axis[0] != 0 || axis[1] != 0 || axis[2] != 0)
				for (int k = 0; k < 3; ++k)
					s.axis[k] = axis[k];
		}
		s.omega = vary(_header.f32(0x14), _header.f32(0x18));
		s.startAngle = 2 * kPi * EffectRandom::nextFloat();
		const int32 mode = _header.s32(0x24);
		if (mode == 1)
			s.palette = (int32)(EffectRandom::next() % 6);
		else if (mode == 2)
			s.palette = (int32)(index % 6);
		else
			s.palette = -1;
	}

	uint32 paletteColor(int32 start, int32 age) const {
		if (start < 0)
			return 0xFFFFFFFF;
		const int32 segment = MAX(1, _header.s32(0x28) / 6);
		const int32 i = (start + age / segment) % 6;
		const float t = (float)(age % segment) / segment;
		const uint32 a = _header.u32(0x2C + 4 * i), b = _header.u32(0x2C + 4 * ((i + 1) % 6));
		return (lerpColor(a, b, t) & 0xFFFFFF) | 0xFF000000;
	}
};

// ---------------------------------------------------------------------------
// Geometry primitives: local vertices placed by (v x scale) R(q) + position, vertex colour times
// the kind's sampled material colour times the node colour.
// ---------------------------------------------------------------------------

class GeometryPrimitive : public EffectPrimitive {
public:
	explicit GeometryPrimitive(const Common::EPL::Record &record) : _record(record) {
		_params.bytes = record.parameters;
	}
	void setPosition(const float p[3]) override { for (int i = 0; i < 3; ++i) _position[i] = p[i]; }
	void setRotation(const float q[4]) override { for (int i = 0; i < 4; ++i) _rotation[i] = q[i]; }
	void setScale(float scale) override { _scale[0] = _scale[1] = _scale[2] = scale; }
	void setVectorScale(const float scale[3]) override { for (int i = 0; i < 3; ++i) _scale[i] = scale[i]; }

protected:
	struct Vertex {
		float position[3];
		float u, v;
		uint32 color;
	};

	Common::EPL::Record _record;
	Params _params;
	float _position[3] = {0, 0, 0};
	float _rotation[4] = {0, 0, 0, 1};
	float _scale[3] = {1, 1, 1};

	/** Emit triangles (index triples into `vertices`), coloured by vertex x material colour. */
	void emit(EffectBatch &batch, const Vertex *vertices, const int *indices, int indexCount, uint32 material) const {
		for (int i = 0; i < indexCount; ++i) {
			const Vertex &in = vertices[indices[i]];
			const float scaled[3] = {in.position[0] * _scale[0], in.position[1] * _scale[1], in.position[2] * _scale[2]};
			float world[3];
			rotate(_rotation, scaled, world);
			EffectVertex out;
			out.x = world[0] + _position[0];
			out.y = world[1] + _position[1];
			out.z = world[2] + _position[2];
			out.u = in.u;
			out.v = in.v;
			float rgba[4];
			colorToFloats(modulate(in.color, material), rgba);
			out.r = rgba[0]; out.g = rgba[1]; out.b = rgba[2]; out.a = rgba[3];
			batch.vertices.push_back(out);
		}
	}
};

/** Packed colour with every channel (alpha too) times env. */
uint32 scaleColor(uint32 c, float env) {
	uint32 out = 0;
	for (int i = 0; i < 4; ++i)
		out |= (uint32)CLIP((int)(((c >> (8 * i)) & 0xFF) * env), 0, 255) << (8 * i);
	return out;
}

/** Packed colour with RGB times env and the given alpha. */
uint32 scaleRGB(uint32 c, float env, uint32 alpha) {
	return (scaleColor(c, env) & 0xFFFFFF) | (alpha << 24);
}

// Kind 12: flat ring (sub 1) or filled disc (sub 3) in the local XZ plane, growing over time.
class Ring : public GeometryPrimitive {
public:
	using GeometryPrimitive::GeometryPrimitive;

	void reset() override { _age = 0; }
	void advance() override { ++_age; }

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		const int32 duration = _params.s32(0x34);
		const int32 age = _age - 1; // geometry is rebuilt before the age step, colour after it
		if (_age <= 0 || (duration != 0 && _age > duration) || (_record.sub != 1 && _record.sub != 3))
			return;
		const uint32 material = modulate(_params.sampleColor(0x00, 0x24, _age, duration), nodeColor);
		if ((material >> 24) == 0)
			return;
		const uint32 tail = _record.sub == 1 ? 0x60 : 0x5C;
		const float speed = _params.f32(tail), accel = _params.f32(tail + 4);
		float t = _params.u8(tail + 8) ? (float)(duration - age) : (float)age;
		const float v = fabsf(speed);
		if (accel < 0)
			t = MIN(t, -v / accel);
		const float r = _params.f32(0x40) + t * (v + 0.5f * accel * t);
		const float inner = _params.f32(0x50), middle = _params.f32(0x54), outer = _params.f32(0x58);
		const float radii[4] = {_record.sub == 3 ? 0 : r, r + inner, r + inner + middle, r + inner + middle + outer};
		const uint32 colors[4] = {_params.u32(0x44), _params.u32(0x48), _params.u32(0x48), _params.u32(0x4C)};
		const uint32 segments = MAX<uint32>(3, _params.u32(0x38));

		EffectBatch batch;
		batch.blendMode = _params.u16(0x28);
		batch.depthTest = _params.u8(0x3C) != 0;
		static const int kStrip[18] = {0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6};
		for (uint32 k = 0; k < segments; ++k) {
			Vertex rows[8];
			for (int row = 0; row < 2; ++row) {
				const float theta = 2 * kPi * (k + row) / segments;
				for (int j = 0; j < 4; ++j) {
					Vertex &vertex = rows[row * 4 + j];
					vertex.position[0] = cosf(theta) * radii[j];
					vertex.position[1] = 0;
					vertex.position[2] = sinf(theta) * radii[j];
					vertex.u = vertex.v = 0;
					vertex.color = colors[j];
				}
			}
			emit(batch, rows, kStrip, 18, material);
		}
		out.push_back(batch);
	}

private:
	int32 _age = 0;
};

// Kind 10: a pool of quads (rays, streaks, a crown) in the local XZ plane.
class FlashQuads : public GeometryPrimitive {
public:
	FlashQuads(const Common::EPL::Record &record, EffectTextureSource *textures) : GeometryPrimitive(record) {
		// Embedded TMX replaces the default texture; a texture slot resource is ignored.
		if (record.resourceKind == Common::EPL::kResourceTMX && !record.resource.empty())
			_texture.embeddedTMX = &_record.resource;
		else if (record.sub == 4 || record.sub == 6)
			_texture.globalSlot = 0x12;
		else if (record.sub == 5)
			_texture.globalSlot = 0x13;
		_quads.resize(MIN<uint32>(_params.u32(0x38), 1024)); // corpus counts are small
	}

	void reset() override { _frame = 0; }

	void advance() override {
		const int32 duration = _params.s32(0x34), life = _params.s32(0x4C);
		if ((duration != 0 && _frame > duration) || life <= 0 || !supported()) {
			++_frame;
			return;
		}
		if (_frame == 0)
			for (uint i = 0; i < _quads.size(); ++i)
				_quads[i].age = -1;
		const bool burst = _frame == 0 && _params.u8(0x55);
		int32 births = burst ? (int32)_quads.size() : _params.s32(0x50);
		for (uint i = 0; i < _quads.size(); ++i) {
			Quad &q = _quads[i];
			// Death and birth each take a frame of their own; geometry is first built the frame after birth.
			if (q.age >= life) {
				q.age = _params.u8(0x54) ? -1 : -2;
				q.built = false;
				continue;
			}
			if (q.age == -1) {
				if (births > 0) {
					--births;
					spawn(q, life);
					if (burst)
						preAdvance(q, (int32)(EffectRandom::next() % (uint32)life));
				}
				continue;
			}
			if (q.age >= 0) {
				step(q);
				q.built = true;
				++q.age;
			}
		}
		++_frame;
	}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		const int32 duration = _params.s32(0x34), life = _params.s32(0x4C);
		if (_frame <= 0 || (duration != 0 && _frame > duration) || life <= 0 || !supported())
			return;
		const uint32 material = modulate(_params.sampleColor(0x00, 0x24, _frame, duration), nodeColor);
		EffectBatch batch;
		batch.blendMode = _params.u16(0x28);
		batch.depthTest = _params.u8(0x56) != 0;
		batch.texture = _texture;
		const uint32 innerColor = _params.u32(0x58), outerColor = _params.u32(0x5C);
		for (uint i = 0; i < _quads.size(); ++i) {
			const Quad &q = _quads[i];
			if (q.age <= 0 || !q.built)
				continue;
			const float env = envelope(q.age - 1, life);
			const bool odd = (i & 1) != 0;
			Vertex v[5];
			if (_record.sub == 1) {
				// Rays: v0/v1 tip edge, v3 centre, v2/v4 sides; untextured.
				for (int k = 0; k < 5; ++k) {
					for (int a = 0; a < 3; ++a)
						v[k].position[a] = q.vertices[k][a];
					v[k].u = v[k].v = 0;
				}
				v[0].color = v[1].color = scaleRGB(outerColor, env, 0);
				v[2].color = v[4].color = (uint32)((odd ? 128 : 255) * env) << 24;
				v[3].color = scaleRGB(innerColor, env, odd ? 0xFF : 0x40);
				static const int kRays[9] = {0, 3, 2, 0, 1, 3, 1, 4, 3};
				emit(batch, v, kRays, 9, material);
			} else {
				// Textured quad; odd quads use the texture's left half, even ones the right.
				const float u0 = odd ? 0.0f : 0.5f;
				static const float kUV[4][2] = {{0, 0}, {0.5f, 0}, {0, 1}, {0.5f, 1}};
				for (int k = 0; k < 4; ++k) {
					for (int a = 0; a < 3; ++a)
						v[k].position[a] = q.vertices[k][a];
					v[k].u = u0 + kUV[k][0];
					v[k].v = kUV[k][1];
				}
				v[0].color = v[1].color = scaleColor(outerColor, env);
				v[2].color = v[3].color = scaleColor(innerColor, env);
				static const int kQuad[6] = {0, 1, 3, 0, 3, 2};
				emit(batch, v, kQuad, 6, material);
			}
		}
		if (!batch.vertices.empty())
			out.push_back(batch);
	}

private:
	struct Quad {
		int32 age = -1;
		bool built = false;
		float phase = 0, omega = 0;
		float width = 0, length = 0, lengthStep = 0, radius = 0, radiusStep = 0, tilt = 0, tiltStep = 0;
		float distance = 0, speed = 0;
		float vertices[5][3] = {};
	};

	EffectTextureRef _texture;
	Common::Array<Quad> _quads;
	int32 _frame = 0;

	bool supported() const { return _record.sub == 1 || _record.sub == 4 || _record.sub == 5 || _record.sub == 6; }
	bool reversed() const { return _record.sub == 5 && _params.u8(0x84); }

	float envelope(int32 age, int32 life) const {
		float fadeIn = _params.f32(0x44), fadeOut = _params.f32(0x48);
		if (reversed()) {
			// Age runs from life down to 0, with the fade fractions mirrored.
			age = life - age;
			const float in = 1 - fadeOut;
			fadeOut = 1 - fadeIn;
			fadeIn = in;
		}
		const int32 fi = (int32)(fadeIn * life), fo = (int32)(fadeOut * life);
		if (age < fi)
			return (float)age / fi;
		if (age > fo)
			return life > fo ? (float)(life - age) / (life - fo) : 0;
		return 1;
	}

	static float randomSign() { return (EffectRandom::next() & 1) ? -1.0f : 1.0f; }

	void spawn(Quad &q, int32 life) {
		q = Quad();
		q.age = 0;
		q.phase = 2 * kPi * EffectRandom::nextFloat();
		switch (_record.sub) {
		case 1: {
			q.omega = vary(_params.f32(0x74), _params.f32(0x78)); // variation first, then the sign
			q.omega *= randomSign();
			q.radius = vary(_params.f32(0x6C), _params.f32(0x70));
			const float factor = vary(1.0f, _params.f32(0x68));
			q.width = _params.f32(0x60) * factor;
			q.length = _params.f32(0x64) * factor;
			break;
		}
		case 4: {
			q.omega = vary(_params.f32(0x74), _params.f32(0x78)); // variation first, then the sign
			q.omega *= randomSign();
			const int32 grow = MAX(1, _params.s32(0x70));
			q.lengthStep = vary(_params.f32(0x68), _params.f32(0x6C)) / grow;
			q.width = vary(_params.f32(0x60), _params.f32(0x64));
			break;
		}
		case 5:
			q.length = vary(_params.f32(0x68), _params.f32(0x6C));
			q.distance = vary(_params.f32(0x70), _params.f32(0x74));
			q.speed = vary(_params.f32(0x78), _params.f32(0x7C));
			q.width = vary(_params.f32(0x60), _params.f32(0x64));
			break;
		case 6: {
			auto pair = [&](uint32 off, float &value, float &delta) {
				value = vary(_params.f32(off), _params.f32(off + 4));
				delta = (vary(_params.f32(off + 8), _params.f32(off + 12)) - value) / life;
			};
			pair(0x68, q.length, q.lengthStep);
			pair(0x78, q.radius, q.radiusStep);
			pair(0x88, q.tilt, q.tiltStep);
			q.width = vary(_params.f32(0x60), _params.f32(0x64));
			q.omega = vary(_params.f32(0x98), _params.f32(0x9C));
			break;
		}
		default:
			break;
		}
	}

	// Burst births start part-way through their life.
	void preAdvance(Quad &q, int32 age) {
		if (_record.sub == 4)
			q.length = q.lengthStep * (age % MAX(1, _params.s32(0x70)));
		if (_record.sub == 6) {
			q.length += q.lengthStep * age;
			q.radius += q.radiusStep * age;
			q.tilt += q.tiltStep * age;
		}
		q.age = age;
	}

	float angle(const Quad &q, float accel) const {
		const float sign = q.omega > 0 ? 1.0f : -1.0f; // the game takes the minus branch at 0
		return q.phase + q.age * (q.omega + sign * 0.5f * accel * q.age);
	}

	static void set(float out[3], float x, float y, float z) {
		out[0] = x;
		out[1] = y;
		out[2] = z;
	}

	void step(Quad &q) {
		switch (_record.sub) {
		case 1: {
			const float phi = angle(q, _params.f32(0x7C));
			const float d[3] = {cosf(phi), 0, sinf(phi)}, p[3] = {sinf(phi), 0, -cosf(phi)};
			set(q.vertices[0], d[0] * q.radius + p[0] * q.length, 0, d[2] * q.radius + p[2] * q.length);
			set(q.vertices[1], d[0] * q.radius - p[0] * q.length, 0, d[2] * q.radius - p[2] * q.length);
			set(q.vertices[2], p[0] * q.width, 0, p[2] * q.width);
			set(q.vertices[3], 0, 0, 0);
			set(q.vertices[4], -p[0] * q.width, 0, -p[2] * q.width);
			break;
		}
		case 4: {
			const float phi = angle(q, _params.f32(0x7C));
			if (q.age < MAX(1, _params.s32(0x70)))
				q.length += q.lengthStep;
			const float d[3] = {cosf(phi), 0, sinf(phi)}, p[3] = {sinf(phi), 0, -cosf(phi)};
			set(q.vertices[0], d[0] * q.length + p[0] * q.width, 0, d[2] * q.length + p[2] * q.width);
			set(q.vertices[1], d[0] * q.length - p[0] * q.width, 0, d[2] * q.length - p[2] * q.width);
			set(q.vertices[2], p[0] * q.width, 0, p[2] * q.width);
			set(q.vertices[3], -p[0] * q.width, 0, -p[2] * q.width);
			break;
		}
		case 5: {
			const float d[3] = {cosf(q.phase), 0, sinf(q.phase)}, p[3] = {sinf(q.phase), 0, -cosf(q.phase)};
			const float accel = _params.f32(0x80), v = q.speed;
			float t = reversed() ? (float)(_params.s32(0x4C) - q.age) : (float)q.age;
			if (accel < 0)
				t = MIN(t, -v / accel);
			const float dist = t * (v + 0.5f * accel * t) + q.distance;
			for (int k = 0; k < 4; ++k) {
				const float l = (k < 2 ? 1 : -1) * q.length, w = (k % 2 == 0 ? 1 : -1) * q.width;
				set(q.vertices[k], d[0] * (dist + l) + p[0] * w, 0, d[2] * (dist + l) + p[2] * w);
			}
			break;
		}
		case 6: {
			const float phi = q.phase + q.age * (q.omega + 0.5f * _params.f32(0xA0) * q.age);
			const float base[3] = {cosf(phi) * q.radius, 0, sinf(phi) * q.radius};
			float up[3] = {cosf(phi) * q.tilt, 1 - q.tilt, sinf(phi) * q.tilt};
			normalize(up);
			const float w[3] = {sinf(phi) * q.width, 0, -cosf(phi) * q.width};
			for (int a = 0; a < 3; ++a) {
				q.vertices[2][a] = base[a] + w[a];
				q.vertices[3][a] = base[a] - w[a];
				q.vertices[0][a] = base[a] + up[a] * q.length + w[a];
				q.vertices[1][a] = base[a] + up[a] * q.length - w[a];
			}
			q.length += q.lengthStep;
			q.radius += q.radiusStep;
			q.tilt += q.tiltStep;
			break;
		}
		default:
			break;
		}
	}
};

// Kind 16 sub 1: twisting textured strips around the node's up axis (a funnel of wind/flame).
class WindFunnel : public GeometryPrimitive {
public:
	WindFunnel(const Common::EPL::Record &record) : GeometryPrimitive(record) {
		// Embedded TMX, else global slot 0x14 whatever slot the resource names.
		if (record.resourceKind == Common::EPL::kResourceTMX && !record.resource.empty())
			_texture.embeddedTMX = &_record.resource;
		else
			_texture.globalSlot = 0x14;
		_segments = MAX<uint32>(3, MIN<uint32>(_params.u32(0x8C), 256));
		_strips.resize(MIN<uint32>(_params.u32(0x38), 256));
		for (uint i = 0; i < _strips.size(); ++i)
			_strips[i].rows.resize((_segments + 1) * 4);
	}

	void reset() override { _frame = 0; }

	void advance() override {
		const int32 duration = _params.s32(0x34), life = _params.s32(0x80);
		if ((duration != 0 && _frame > duration) || life <= 0 || _record.sub != 1) {
			++_frame;
			return;
		}
		if (_frame == 0)
			for (uint i = 0; i < _strips.size(); ++i)
				_strips[i].age = -1;
		const bool burst = _frame == 0 && _params.u8(0xB8);
		int32 births = burst ? (int32)_strips.size() : _params.s32(0x84);
		for (uint i = 0; i < _strips.size(); ++i) {
			Strip &s = _strips[i];
			// Death and birth each take a frame of their own; a strip is first built the frame after birth.
			if (s.age >= life) {
				s.age = _params.u8(0x88) ? -1 : -2;
				s.built = false;
				continue;
			}
			if (s.age == -1) {
				if (births > 0) {
					--births;
					spawn(s, life);
					if (burst) {
						s.age = (int32)(EffectRandom::next() % (uint32)life);
						s.radius += s.radiusStep * s.age;
						s.height += s.heightStep * s.age;
					}
				}
				continue;
			}
			if (s.age >= 0) {
				build(s);
				s.built = true;
				++s.age;
			}
		}
		++_frame;
	}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		const int32 duration = _params.s32(0x34), life = _params.s32(0x80);
		if (_frame <= 0 || (duration != 0 && _frame > duration) || life <= 0 || _record.sub != 1)
			return;
		const uint32 global = modulate(_params.sampleColor(0x00, 0x24, _frame, duration), nodeColor);
		EffectBatch batch;
		batch.blendMode = _params.u16(0x28);
		batch.depthTest = _params.u8(0xB9) != 0;
		batch.texture = _texture;
		// Static vertex colours: feathered across the strip, faded along it.
		const int32 fi = (int32)(_params.f32(0x78) * _segments), fo = (int32)(_params.f32(0x7C) * _segments);
		const float uExtent = _params.f32(0x90);
		static const int kStrip[18] = {0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6};
		for (uint i = 0; i < _strips.size(); ++i) {
			const Strip &s = _strips[i];
			if (s.age <= 0 || !s.built || s.age >= life) // a strip on its last built frame has colour 0
				continue;
			const uint32 material = modulate(_params.sampleColor(0x3C, 0x60, s.age, life), global);
			Vertex rows[8];
			for (uint32 seg = 0; seg < _segments; ++seg) {
				for (uint32 r = 0; r < 2; ++r) {
					const int32 row = (int32)(seg + r);
					float along = 1;
					if (row < fi)
						along = (float)row / fi;
					else if (row > fo)
						along = (int32)_segments > fo ? (float)((int32)_segments - row) / ((int32)_segments - fo) : 0;
					for (int j = 0; j < 4; ++j) {
						const Row &src = s.rows[row * 4 + j];
						Vertex &v = rows[r * 4 + j];
						for (int a = 0; a < 3; ++a)
							v.position[a] = src.position[a];
						v.u = uExtent * j / 3;
						v.v = src.v;
						v.color = 0xFFFFFF | ((j == 0 || j == 3) ? 0 : (uint32)CLIP((int)(255 * along), 0, 255) << 24);
					}
				}
				emit(batch, rows, kStrip, 18, material);
			}
		}
		if (!batch.vertices.empty())
			out.push_back(batch);
	}

private:
	struct Row {
		float position[3] = {0, 0, 0};
		float v = 0;
	};
	struct Strip {
		int32 age = -1;
		bool built = false;
		float radius = 0, radiusStep = 0, height = 0, heightStep = 0, swell = 0;
		float phase = 0, omega = 0, width = 0, twist = 0, v = 0;
		Common::Array<Row> rows;
	};

	EffectTextureRef _texture;
	uint32 _segments = 3;
	Common::Array<Strip> _strips;
	int32 _frame = 0;

	void spawn(Strip &s, int32 life) {
		Common::Array<Row> rows;
		rows.swap(s.rows);
		s = Strip();
		s.rows.swap(rows);
		s.age = 0;
		s.radius = vary(_params.f32(0xBC), _params.f32(0xC0));
		s.radiusStep = (vary(_params.f32(0xC4), _params.f32(0xC8)) - s.radius) / life;
		s.height = vary(_params.f32(0xCC), _params.f32(0xD0));
		s.heightStep = (vary(_params.f32(0xD4), _params.f32(0xD8)) - s.height) / life;
		s.swell = vary(_params.f32(0xE8), _params.f32(0xE0));
		s.phase = 2 * kPi * EffectRandom::nextFloat();
		s.omega = vary(_params.f32(0xEC), _params.f32(0xF0));
		s.width = vary(_params.f32(0xB0), _params.f32(0xB4));
		s.twist = vary(_params.f32(0xA8), _params.f32(0xAC));
		s.v = vary(_params.f32(0x98), _params.f32(0x9C));
	}

	void build(Strip &s) {
		float theta = s.phase + s.age * (s.omega + 0.5f * _params.f32(0xF4) * s.age);
		s.radius += s.radiusStep;
		s.height += s.heightStep;
		const float n = (float)(_segments + 1), rise = s.height / n;
		const float step = (s.omega >= 0 ? s.twist : -s.twist) / n;
		const float centre = _params.f32(0xE4), vSpan = _params.f32(0x98);
		float y = 0;
		for (uint32 seg = 0; seg <= _segments; ++seg) {
			const float c = seg / n - centre, e = s.swell * c * c, r = s.radius + e;
			const float p[3] = {cosf(theta) * r, y, sinf(theta) * r};
			float normal[3] = {cosf(theta) * e, rise, sinf(theta) * e};
			normalize(normal);
			const float quarter = s.width / 4;
			static const float kOffsets[4] = {2, 1, -1, -2};
			for (int j = 0; j < 4; ++j) {
				Row &row = s.rows[seg * 4 + j];
				for (int a = 0; a < 3; ++a)
					row.position[a] = p[a] + normal[a] * quarter * kOffsets[j];
				row.v = s.v + vSpan * (seg / n);
			}
			theta += step;
			y += rise;
		}
		s.v += _params.f32(0xA4);
	}
};

// Kind 7: one textured quad in the node's XZ plane, sized by the texture and a size key.
class Board : public GeometryPrimitive {
public:
	Board(const Common::EPL::Record &record, EffectTextureSource *textures)
			: GeometryPrimitive(record), _textures(textures), _texture(textureFor(_record)) {}

	void reset() override { _age = 0; }
	void advance() override { ++_age; }

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		const int32 duration = _params.s32(0x8C);
		if (_age == 0)
			return;
		int32 age = _age;
		if (duration > 0 && age >= duration) {
			if (!_params.u8(0x44))
				return;
			age %= duration;
		}
		int texW = 32, texH = 32;
		if (_textures && _texture.valid())
			_textures->textureSize(_texture, texW, texH);
		const float size = _params.sampleScalar(0x34, age, duration) * _params.f32(0x90);
		const float hx = texW / 32.0f * size, hy = texH / 32.0f * size;
		const float spin = _params.sampleScalar(0x60, age, duration), c = cosf(spin), s = sinf(spin);
		static const float kCorner[4][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {-1, 1, 0, 0}, {1, 1, 1, 0}};
		Vertex v[4];
		for (int k = 0; k < 4; ++k) {
			const float x = kCorner[k][0] * hx, z = kCorner[k][1] * hy;
			v[k].position[0] = c * x - s * z;
			v[k].position[1] = 0;
			v[k].position[2] = s * x + c * z;
			v[k].u = kCorner[k][2];
			v[k].v = kCorner[k][3];
			v[k].color = 0xFFFFFFFF;
		}
		EffectBatch batch;
		batch.blendMode = _params.u16(0x28);
		batch.depthTest = _params.u8(0x94) != 0;
		batch.texture = _texture;
		static const int kQuad[6] = {0, 1, 3, 0, 3, 2};
		emit(batch, v, kQuad, 6, modulate(_params.sampleColor(0x00, 0x24, age, duration), nodeColor));
		out.push_back(batch);
	}

private:
	EffectTextureSource *_textures;
	EffectTextureRef _texture;
	int32 _age = 0;
};

// Kind 26: an embedded animated model placed at the node, tinted and scaled by keys.
class ModelEffect : public GeometryPrimitive {
public:
	ModelEffect(const Common::EPL::Record &record) : GeometryPrimitive(record) {
		if (record.resourceKind == Common::EPL::kResourceRWStream)
			_model.load(record.resource);
	}

	void reset() override {
		_elapsed = 0;
		_model.pose(0);
	}

	void advance() override {
		const int32 duration = _params.s32(0x60);
		if (duration == 0 || _elapsed <= duration) {
			++_elapsed;
			_model.pose(_elapsed / 30.0f); // the model's animation advances one game tick
		}
	}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		const int32 duration = _params.s32(0x60);
		if (_model.mesh().empty() || (duration != 0 && _elapsed > duration))
			return;
		float rgba[4];
		colorToFloats(modulate(_params.sampleColor(0x00, 0x24, _elapsed, duration), nodeColor), rgba);
		if (rgba[3] <= 0)
			return; // a fully transparent model isn't queued
		const float scale = (_scale[0] + _scale[1] + _scale[2]) / 3 * _params.sampleScalar(0x34, _elapsed, duration) / 10;
		// The list only orders the model: it draws with normal alpha blending, culls back faces,
		// and depth-tests and writes depth when parameter 0x64 is set.
		const bool depth = _params.u8(0x64) != 0;
		// Depth is only written where the alpha test (A >= 0x7F of 0x80) passes.
		auto states = [&](uint32 first, bool depthTest, bool depthOnly, bool depthWrite, float minAlpha) {
			for (uint b = first; b < out.size(); ++b) {
				out[b].blendEquation = 0;
				out[b].cullBack = true;
				out[b].depthTest = depthTest;
				out[b].depthWrite = depthWrite;
				out[b].depthWriteMinAlpha = minAlpha;
				out[b].depthOnly = depthOnly;
			}
		};
		const float kAlphaTest = 127 / 128.0f;
		// Duration 1225 marks a translucent model: while it isn't opaque, a depth-only pass first
		// keeps it from showing its own back surfaces, then the colour pass doesn't write depth.
		if (duration == 1225 && rgba[3] < 1) {
			uint32 first = out.size();
			_model.emit(out, first, _params.u16(0x28), _rotation, scale, _position, rgba);
			states(first, true, true, depth, 1 / 255.0f); // depth for every texel that isn't clear
			first = out.size();
			_model.emit(out, first, _params.u16(0x28), _rotation, scale, _position, rgba);
			states(first, true, false, false, 0);
			return;
		}
		const uint32 first = out.size();
		_model.emit(out, first, _params.u16(0x28), _rotation, scale, _position, rgba);
		states(first, depth, false, depth, kAlphaTest);
	}

private:
	EmbeddedModel _model;
	int32 _elapsed = 0;
};

// Kinds 4 and 5: screen filters, drawn after every 3D effect list without depth. They sample
// at the frame before the age step and stop (alpha 0) once it passes the duration; a duration of
// 0 holds frame 0 forever.
class ScreenFilter : public EffectPrimitive {
public:
	explicit ScreenFilter(const Common::EPL::Record &record) : _record(record) { _params.bytes = record.parameters; }

	void reset() override { _age = 0; }
	void advance() override { ++_age; }
	void setPosition(const float position[3]) override { for (int k = 0; k < 3; ++k) _position[k] = position[k]; }
	void setRotation(const float rotation[4]) override {}
	void setScale(float scale) override { _scale = scale; }

protected:
	Common::EPL::Record _record;
	Params _params;
	int32 _age = 0;
	float _position[3] = {0, 0, 0};
	float _scale = 1;

	/** Frame and colour (0..1) of the last update; false when nothing is to be drawn. */
	bool sample(uint32 nodeColor, int32 &frame, float rgba[4]) const {
		if (_age <= 0)
			return false;
		const int32 duration = _params.s32(0xB8);
		frame = duration == 0 ? 0 : _age - 1;
		if (frame > duration)
			return false;
		colorToFloats(modulate(_params.sampleColor(0x00, 0x24, frame, duration), nodeColor), rgba);
		return rgba[3] > 0;
	}

	EffectBatch makeBatch() const {
		EffectBatch batch;
		batch.blendMode = _params.u16(0x28);
		batch.depthTest = false;
		batch.overlay = true;
		return batch;
	}

	/** Quad (x0, y0)-(x1, y1) with UVs per corner: top left, bottom left, bottom right, top right. */
	static void quad(EffectBatch &batch, float x0, float y0, float x1, float y1, const float uv[4][2], const float rgba[4]) {
		const float corners[4][2] = {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}};
		static const int kOrder[6] = {0, 1, 2, 0, 2, 3};
		for (int i = 0; i < 6; ++i) {
			const int k = kOrder[i];
			EffectVertex v;
			v.x = corners[k][0];
			v.y = corners[k][1];
			v.z = 0;
			v.u = uv[k][0];
			v.v = uv[k][1];
			v.r = rgba[0]; v.g = rgba[1]; v.b = rgba[2]; v.a = rgba[3];
			batch.vertices.push_back(v);
		}
	}
};

// Kind 5 sub 1: the whole screen filled with the sampled colour (a flash or fade).
class ScreenFlash : public ScreenFilter {
public:
	using ScreenFilter::ScreenFilter;

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		int32 frame;
		float rgba[4];
		if (!sample(nodeColor, frame, rgba))
			return;
		EffectBatch batch = makeBatch();
		batch.screenSpace = true;
		static const float kUV[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
		quad(batch, 0, 0, 1, 1, kUV, rgba);
		out.push_back(batch);
	}
};

// Kind 4 sub 1: the previous frame over the whole screen, zoomed and rotated about its centre
// (zoom > 1 shrinks it toward the centre), tinted by the sampled colour.
class ScreenEcho : public ScreenFilter {
public:
	using ScreenFilter::ScreenFilter;

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		int32 frame;
		float rgba[4];
		if (!sample(nodeColor, frame, rgba)) {
			// Keep the renderer copying frames while the echo runs, so its first visible frame
			// has one (the game always has its display buffer).
			const int32 duration = _params.s32(0xB8);
			if (_age > 0 && (duration == 0 || _age - 1 <= duration)) {
				EffectBatch request = makeBatch();
				request.texture.previousFrame = true;
				out.push_back(request);
			}
			return;
		}
		const int32 duration = _params.s32(0xB8);
		const float zoom = 1 + 0.01f * _params.sampleScalar(0x34, frame, duration);
		const float angle = 0.01f * _params.sampleScalar(0x60, frame, duration);
		if (zoom == 0 || fabsf(zoom) >= 7) // a negative zoom mirrors the echo
			return;
		// UVs of the 640x448 screen corners (normalised, so the rotation is not aspect-correct).
		float uv[4][2];
		static const float kCorners[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
		const float c = cosf(angle), s = sinf(angle);
		for (int k = 0; k < 4; ++k) {
			const float du = zoom * (kCorners[k][0] - 0.5f), dv = zoom * (kCorners[k][1] - 0.5f);
			uv[k][0] = 0.5f + du * c - dv * s;
			uv[k][1] = 0.5f + du * s + dv * c;
		}
		EffectBatch batch = makeBatch();
		batch.screenSpace = true;
		batch.texture.previousFrame = true;
		quad(batch, 0, 0, 1, 1, uv, rgba);
		out.push_back(batch);
	}
};

// Kind 5 sub 2: a square of the sampled colour masked by its texture's alpha, centred on the
// screen or on the effect's projected position.
class ScreenSprite : public ScreenFilter {
public:
	explicit ScreenSprite(const Common::EPL::Record &record) : ScreenFilter(record), _texture(textureFor(_record)) {}

	void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) override {
		int32 frame;
		float rgba[4];
		if (!sample(nodeColor, frame, rgba))
			return;
		const float radius = _params.sampleScalar(0x8C, frame, _params.s32(0xB8));
		EffectBatch batch = makeBatch();
		batch.texture = _texture;
		batch.alphaMask = true;
		static const float kUV[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
		if (_params.u8(0xBC)) {
			batch.screenSpace = true;
			const float hx = radius / 640, hy = radius / 448;
			quad(batch, 0.5f - hx, 0.5f - hy, 0.5f + hx, 0.5f + hy, kUV, rgba);
		} else {
			// shortcut: the game sizes this 650 * radius * scale / camera distance pixels (at most
			// 1500); the demo has no perspective camera, so a distance of 650 is assumed.
			const float h = MIN(radius * _scale, 1500.0f);
			batch.anchored = true;
			for (int k = 0; k < 3; ++k)
				batch.anchor[k] = _position[k];
			quad(batch, -h, -h, h, h, kUV, rgba);
		}
		out.push_back(batch);
	}

private:
	EffectTextureRef _texture;
};

// TODO: effect features no P3 FES/P4 RMD uses, so they could not be checked against real data
// and are parsed but not drawn:
// - kinds 2/15 track trails, 9 lens flare, 13 thunder, 21 camera shake/sequence, 23 screen
//   sprite, 27 helper (model children), 29 distortion particle, 30 line nova, 31 battle-only
//   effect, 32 model drawable (P4);
// - particle emitter models 2, 4, 6, 7, 9, 10 and 11 (cone with an angle span, Bezier, swirl);
// - ring sub 2 (random strip), flash quad subs 2, 3 (radial rays) and 7 (boxes), wind subs 2
//   (falling funnel) and 3 (spiral);
// - board textures from an animated track (resource kind 2) or a TLP (7);
// - screen blur subs 2-5 (kind 4) and screen filter subs other than 1 and 2 (kind 5).
EffectPrimitive *createPrimitive(const Common::EPL::Record &record, EffectTextureSource *textures) {
	switch (record.kind) {
	case Common::EPL::kParticle:
		return new ParticleEmitter(record, textures);
	case Common::EPL::kRing:
		return new Ring(record);
	case Common::EPL::kFlashQuads:
		return new FlashQuads(record, textures);
	case Common::EPL::kWind:
		return new WindFunnel(record);
	case Common::EPL::kObjectParticle:
		return new ObjectParticleEmitter(record, textures);
	case Common::EPL::kModel:
		return new ModelEffect(record);
	case Common::EPL::kScreenFilter:
		if (record.sub == 1)
			return new ScreenFlash(record);
		if (record.sub == 2)
			return new ScreenSprite(record);
		return nullptr; // not used by any P3 FES/P4 RMD
	case Common::EPL::kScreenBlur:
		if (record.sub == 1)
			return new ScreenEcho(record);
		return nullptr; // subs 2-5 are not used by any P3 FES/P4 RMD
	case Common::EPL::kBoard:
		return new Board(record, textures);
	default:
		return nullptr; // not used by any P3 FES/P4 RMD
	}
}

} // End of anonymous namespace

// ---------------------------------------------------------------------------
// Effect tree
// ---------------------------------------------------------------------------

EPLEffect::EPLEffect(const Common::EPL &epl, EffectTextureSource *textures) {
	_root = epl.root();
	for (uint i = 0; i < epl.nodes().size(); ++i) {
		Child child;
		child.node = epl.nodes()[i];
		child.primitive = createPrimitive(child.node.record, textures);
		_children.push_back(child);
	}
	const float zero[3] = {0, 0, 0}, identity[4] = {0, 0, 0, 1};
	setPose(zero, identity);
	setScale(1.0f);
	setColor(0xFFFFFFFF);
}

EPLEffect::~EPLEffect() {
	for (uint i = 0; i < _children.size(); ++i)
		delete _children[i].primitive;
}

void EPLEffect::origin(float out[3]) const {
	for (int k = 0; k < 3; ++k)
		out[k] = _position[k] + _rotatedOffset[k];
}

void EPLEffect::childBase(const Child &child, const float origin[3], float out[3]) const {
	for (int k = 0; k < 3; ++k)
		out[k] = origin[k];
	if (child.node.state.flags & Common::EPL::kStateBaseHeight)
		out[1] = 5.0f;
}

void EPLEffect::applyRotation(const float rotation[4]) {
	for (int k = 0; k < 4; ++k)
		_rotation[k] = rotation[k];
	rotate(rotation, _root.localOffset, _rotatedOffset);
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (!child.primitive)
			continue;
		float q[4];
		multiply(child.node.state.orientation, rotation, q);
		child.primitive->setRotation(q);
	}
	applyPosition(_position);
}

void EPLEffect::applyPosition(const float position[3]) {
	for (int k = 0; k < 3; ++k)
		_position[k] = position[k];
	float o[3];
	origin(o);
	const float treeScale = _root.scale * _root.multiplier;
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (!child.primitive)
			continue;
		float base[3], offset[3], rotated[3], p[3];
		childBase(child, o, base);
		for (int k = 0; k < 3; ++k)
			offset[k] = child.node.state.offset[k] * ((child.node.state.flags & Common::EPL::kStateScaleOffset) ? treeScale : 1);
		rotate(_rotation, offset, rotated);
		for (int k = 0; k < 3; ++k)
			p[k] = base[k] + rotated[k];
		child.primitive->setPosition(p);
	}
}

void EPLEffect::setPose(const float position[3], const float rotation[4]) {
	for (int k = 0; k < 3; ++k)
		_position[k] = position[k];
	applyRotation(rotation);
}

void EPLEffect::setScale(float scale) {
	_root.scale = scale;
	const float treeScale = scale * _root.multiplier;
	float o[3];
	origin(o);
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (!child.primitive)
			continue;
		const Common::EPL::State &s = child.node.state;
		const float inherit = (child.node.flags & 1) ? treeScale : 1;
		if (!(s.flags & Common::EPL::kStateVectorScale)) {
			child.primitive->setScale(s.scale * inherit);
		} else {
			float v[3];
			for (int k = 0; k < 3; ++k)
				v[k] = s.scaleVector[k] * s.scale * inherit;
			child.primitive->setVectorScale(v);
		}
		if (s.flags & Common::EPL::kStateScaleOffset) {
			// The game places these children without the tree rotation here; kept as is.
			float base[3], p[3];
			childBase(child, o, base);
			for (int k = 0; k < 3; ++k)
				p[k] = base[k] + s.offset[k] * treeScale;
			child.primitive->setPosition(p);
		}
	}
}

void EPLEffect::setColor(uint32 color) {
	_root.color = color;
	for (uint i = 0; i < _children.size(); ++i)
		_children[i].color = modulate(color, _children[i].node.state.color);
}

void EPLEffect::resolveOffset(const Child &child, const float origin[3], const EffectCamera &camera, float out[3]) const {
	const Common::EPL::State &s = child.node.state;
	const bool flat = (s.flags & Common::EPL::kStateAimOffsetFlat) != 0;
	float d[3];
	for (int k = 0; k < 3; ++k)
		d[k] = origin[k] - camera.position[k];
	normalize(d);
	const float oy = flat ? 0 : s.offset[1];
	const float length = sqrtf(s.offset[0] * s.offset[0] + oy * oy + s.offset[2] * s.offset[2]);
	float dir[3] = {-d[0], flat ? 0 : -d[1], -d[2]};
	normalize(dir);
	for (int k = 0; k < 3; ++k)
		out[k] = dir[k] * length;
	if (flat)
		out[1] = s.offset[1];
}

void EPLEffect::resolveOrientation(const Child &child, const float origin[3], const EffectCamera &camera, float out[4]) const {
	const Common::EPL::State &s = child.node.state;
	float d[3];
	for (int k = 0; k < 3; ++k)
		d[k] = origin[k] - camera.position[k];
	normalize(d);
	const bool yawOnly = (s.flags & Common::EPL::kStateFaceCameraYaw) != 0;
	const float pitch = yawOnly ? 0 : -acosf(CLIP(-d[1], -1.0f, 1.0f));
	const float yaw = atan2f(d[0], d[2]);
	float aim[4];
	eulerToQuat(pitch, yaw, 0, aim);
	if (yawOnly)
		multiply(s.orientation, aim, out);
	else
		for (int k = 0; k < 4; ++k)
			out[k] = aim[k];
}

void EPLEffect::placeCameraDependent(const EffectCamera &camera) {
	float o[3];
	origin(o);
	if (_root.flags & (Common::EPL::kStateFaceCamera | Common::EPL::kStateFaceCameraYaw)) {
		Child root;
		root.node.state = _root;
		// For the root, the orientation slot holds the current world rotation.
		for (int k = 0; k < 4; ++k)
			root.node.state.orientation[k] = _rotation[k];
		float aim[4], saved[4];
		resolveOrientation(root, o, camera, aim);
		for (int k = 0; k < 4; ++k)
			saved[k] = _rotation[k];
		applyRotation(aim);
		for (int k = 0; k < 4; ++k)
			_rotation[k] = saved[k];
	}
	const float treeScale = _root.scale * _root.multiplier;
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		const Common::EPL::State &s = child.node.state;
		if (!child.primitive || _frame < child.node.startFrame || (child.node.flags & 2))
			continue;
		if (s.flags & (Common::EPL::kStateAimOffset | Common::EPL::kStateAimOffsetFlat)) {
			float offset[3], base[3], p[3];
			resolveOffset(child, o, camera, offset);
			childBase(child, o, base);
			const float k = (s.flags & Common::EPL::kStateScaleOffset) ? treeScale : 1;
			for (int a = 0; a < 3; ++a)
				p[a] = base[a] + offset[a] * k;
			child.primitive->setPosition(p);
		}
		if (s.flags & (Common::EPL::kStateFaceCamera | Common::EPL::kStateFaceCameraYaw)) {
			float q[4];
			resolveOrientation(child, o, camera, q);
			child.primitive->setRotation(q);
		}
	}
}

void EPLEffect::advance(const EffectCamera &camera) {
	placeCameraDependent(camera);
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (child.primitive && _frame >= child.node.startFrame && !(child.node.flags & 2))
			child.primitive->advance();
	}
	++_frame;
	_advancedSinceDraw = true;
}

void EPLEffect::draw(const EffectCamera &camera, Common::Array<EffectBatch> &out) {
	if (_frame <= 0)
		return;
	// Frames drawn without a tick in between still re-aim the camera-facing nodes.
	if (!_advancedSinceDraw)
		placeCameraDependent(camera);
	_advancedSinceDraw = false;
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (child.primitive && _frame - 1 >= child.node.startFrame && !(child.node.flags & 2))
			child.primitive->draw(camera, child.color, out);
	}
}

void EPLEffect::restart(bool fresh) {
	for (uint i = 0; i < _children.size(); ++i)
		if (_children[i].primitive) {
			_children[i].primitive->reset();
			if (fresh)
				_children[i].primitive->clearHistory();
		}
	_frame = 0;
}

} // End of namespace Graphics
