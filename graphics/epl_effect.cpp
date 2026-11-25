#include "graphics/epl_effect.h"

#include <cmath>

#include "common/endian.h"
#include "common/util.h"

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
	ParticleEmitter(const Common::EPL::Record &record, EffectTextureSource *textures)
			: _record(record), _textures(textures) {
		_original.bytes = record.parameters;
		_params = _original;
		_texture = textureFor(_record);
		const int32 emit = _params.s32(0x20), life = _params.s32(0xB8);
		const uint32 rate = _params.u32(0x24);
		_capacity = (uint32)MIN<int64>(300, (int64)(emit ? emit : life) * rate);
		_trailCount = MAX(0, _params.s32(0xC0));
		_trailWidth = MAX(0, _params.s32(0xC4));
		_trailLength = (uint32)(_trailCount * _trailWidth);
		_slots.resize(_capacity * (_trailLength + 1));
	}

	void reset() override { _ticks = 0; }
	void setPosition(const float p[3]) override { for (int i = 0; i < 3; ++i) _emitter[i] = p[i]; }
	void setRotation(const float q[4]) override { for (int i = 0; i < 4; ++i) _rotation[i] = q[i]; }
	void setScale(float scale) override {
		// Rewrite the scaled fields from the pristine copy: size keys and distance-like model fields.
		static const uint32 kSize[] = {0x64, 0x68, 0x74, 0x7C, 0x84};
		static const uint32 kWave[] = {0xC8, 0xCC, 0xD4, 0xD8, 0xE0};
		static const uint32 kHelix[] = {0xC8, 0xCC, 0xD4, 0xE4, 0xF0};
		static const uint32 kCone[] = {0xC8, 0xD8, 0xE0, 0xE4};
		_params = _original;
		for (uint32 off : kSize)
			_params.setF32(off, _original.f32(off) * scale);
		const uint32 *fields = nullptr;
		uint32 count = 0;
		switch (_record.sub) {
		case 1: fields = kWave; count = ARRAYSIZE(kWave); break;
		case 3: fields = kHelix; count = ARRAYSIZE(kHelix); break;
		case 5: fields = kCone; count = ARRAYSIZE(kCone); break;
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
		} else if (_ticks == 0 && _params.u8(0xBD)) {
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
				if (preroll) {
					const int32 age = (int32)(EffectRandom::next() % (uint32)lifetime);
					prerollAdvance(p, age);
					p.age = age;
				}
				applyKeys(p, lifetime);
				if (preroll)
					++p.age;
				if (_trailLength)
					updateTrail(i);
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
				// Rotation follows the particle's on-screen motion since the previous frame.
				float d[3];
				for (int k = 0; k < 3; ++k)
					d[k] = p.position[k] - p.previous[k];
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

private:
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
		float radius = 0, radiusStep = 0, height = 0, rise = 0, omega = 0;
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

	bool supported() const { return _record.sub == 1 || _record.sub == 3 || _record.sub == 5; }
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
			const float gravity = _params.f32(0xF0), acceleration = _params.f32(0xEC);
			p.height += p.rise * t - 0.5f * gravity * t * t;
			p.radius += p.radiusStep * t;
			helixPosition(p, t * (p.omega + 0.5f * acceleration * t) + p.phase);
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
			for (int32 k = 1; k < _trailWidth; ++k) {
				const float t = (float)k / (_trailWidth + 1);
				Particle &slot = trail[k];
				slot = head;
				for (int a = 0; a < 3; ++a)
					slot.position[a] = head.position[a] + (previousHead.position[a] - head.position[a]) * t;
				slot.size = head.size + (previousHead.size - head.size) * t;
				slot.angle = head.angle + (previousHead.angle - head.angle) * t;
				slot.color = (lerpColor(head.color, previousHead.color, t) & 0xFFFFFF) | (previousHead.color & 0xFF000000);
				slot.age = head.age;
			}
		}
	}
};

EffectPrimitive *createPrimitive(const Common::EPL::Record &record, EffectTextureSource *textures) {
	switch (record.kind) {
	case Common::EPL::kParticle:
		return new ParticleEmitter(record, textures);
	default:
		return nullptr; // TODO: rings, flash quads, wind, boards, models, object particles, filters
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
}

void EPLEffect::draw(const EffectCamera &camera, Common::Array<EffectBatch> &out) {
	if (_frame <= 0)
		return;
	for (uint i = 0; i < _children.size(); ++i) {
		Child &child = _children[i];
		if (child.primitive && _frame - 1 >= child.node.startFrame && !(child.node.flags & 2))
			child.primitive->draw(camera, child.color, out);
	}
}

void EPLEffect::restart() {
	for (uint i = 0; i < _children.size(); ++i)
		if (_children[i].primitive)
			_children[i].primitive->reset();
	_frame = 0;
}

} // End of namespace Graphics
