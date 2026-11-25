#ifndef GRAPHICS_EPL_EFFECT_H
#define GRAPHICS_EPL_EFFECT_H

#include "common/array.h"
#include "common/formats/epl.h"
#include "common/noncopyable.h"
#include "common/ptr.h"

namespace Graphics {

/**
 * Texture an effect primitive draws with: one of the 22 global effect textures
 * (effect/be_{slot+1:04d}.tmx in effect/effpack.bin), or a TMX embedded in the EPL tree.
 */
struct Surface;

struct EffectTextureRef {
	int globalSlot = -1;
	const Common::Array<byte> *embeddedTMX = nullptr;
	const Graphics::Surface *surface = nullptr; ///< already decoded (textures of embedded models)
	bool valid() const { return globalSlot >= 0 || embeddedTMX || surface; }
};

/** Supplies decoded effect textures; billboard sizes depend on their pixel dimensions. */
class EffectTextureSource {
public:
	virtual ~EffectTextureSource() {}
	virtual bool textureSize(const EffectTextureRef &texture, int &width, int &height) = 0;
	/** The decoded texture, kept alive by the source; nullptr if unavailable. */
	virtual const Graphics::Surface *textureSurface(const EffectTextureRef &texture) { return nullptr; }
};

/** Viewer in the effect's space: position and the screen right/up axes. */
struct EffectCamera {
	float position[3] = {0, 0, 0};
	float right[3] = {1, 0, 0};
	float up[3] = {0, 1, 0};
};

struct EffectVertex {
	float x, y, z;
	float u, v;
	float r, g, b, a; ///< 0..1
};

/**
 * Triangles (three vertices each) sharing one draw state. The seven blend modes select the
 * equation and the draw order:
 * 0, 1: Cs*As + Cd*(1-As);  2: Cs*As + Cd;  3, 4: Cd - Cs*As;  5, 6: Cs - Cd*As.
 * Draw order: 0, 1, 4, 5, 2, 6, 3.
 */
struct EffectBatch {
	int blendMode = 0;
	bool depthTest = true;
	bool screenSpace = false; ///< x, y are fractions of the screen (0, 0 = top left); z unused
	EffectTextureRef texture;
	Common::Array<EffectVertex> vertices;
};

/** One primitive of an effect tree (a particle emitter, a ring, ...). */
class EffectPrimitive {
public:
	virtual ~EffectPrimitive() {}
	virtual void advance() = 0;
	virtual void draw(const EffectCamera &camera, uint32 nodeColor, Common::Array<EffectBatch> &out) = 0;
	virtual void reset() = 0;
	virtual void setPosition(const float position[3]) = 0;
	virtual void setRotation(const float rotation[4]) = 0;
	virtual void setScale(float scale) = 0;
	virtual void setVectorScale(const float scale[3]) { setScale((scale[0] + scale[1] + scale[2]) / 3.0f); }
};

/**
 * A live Persona EPL effect tree: placed with setPose(), advanced one game tick (1/30 s) at a
 * time and drawn into effect batches in the same space as the pose.
 */
class EPLEffect : Common::NonCopyable {
public:
	EPLEffect(const Common::EPL &epl, EffectTextureSource *textures);
	~EPLEffect();

	/** World pose of the tree (position, rotation quaternion x,y,z,w). */
	void setPose(const float position[3], const float rotation[4]);
	void setScale(float scale);
	void setColor(uint32 color);
	/** One tick: camera-dependent placement, then every started node's primitive advances. */
	void advance(const EffectCamera &camera);
	void draw(const EffectCamera &camera, Common::Array<EffectBatch> &out);
	/** Restart every primitive from frame 0. */
	void restart();

private:
	struct Child {
		Common::EPL::Node node;
		EffectPrimitive *primitive = nullptr;
		uint32 color = 0xFFFFFFFF; ///< tree colour x node colour
	};

	Common::EPL::State _root;
	float _rotatedOffset[3] = {0, 0, 0}; ///< root local offset rotated by the world rotation
	float _position[3] = {0, 0, 0};
	float _rotation[4] = {0, 0, 0, 1};
	int32 _frame = 0;
	Common::Array<Child> _children;

	void applyRotation(const float rotation[4]);
	void applyPosition(const float position[3]);
	void origin(float out[3]) const;
	void childBase(const Child &child, const float origin[3], float out[3]) const;
	void resolveOffset(const Child &child, const float origin[3], const EffectCamera &camera, float out[3]) const;
	void resolveOrientation(const Child &child, const float origin[3], const EffectCamera &camera, float out[4]) const;
	void placeCameraDependent(const EffectCamera &camera);
};

namespace EffectMath {
/** v' = v * R(q) with RenderWare row vectors. */
void rotate(const float q[4], const float v[3], float out[3]);
/** Hamilton product a (x) b; with row vectors R(a (x) b) = R(a) R(b). */
void multiply(const float a[4], const float b[4], float out[4]);
void eulerToQuat(float ax, float ay, float az, float out[4]);
/**
 * Pose from a RenderWare row-major matrix (rows right, up, at; translation in m[12..14]):
 * the rotation is orthonormalised (scale and shear removed) and returned as a quaternion.
 */
void matrixToPose(const float m[16], float position[3], float rotation[4]);
/** Multiply two packed RGBA colours channel by channel (normalised, truncated). */
uint32 modulate(uint32 a, uint32 b);
}

/** The effects' shared random generator (seeded like the game, 0x12345678). */
namespace EffectRandom {
void seed(uint32 seed);
uint32 next();
float nextFloat(); ///< [0, 1)
}

} // End of namespace Graphics

#endif
