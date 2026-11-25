#include "graphics/gmo_render.h"

#ifdef USE_OPENGL_SHADERS

#include "graphics/gmo_mesh.h"

#include "common/array.h"
#include "common/system.h"

#include "graphics/opengl/blitter.h"
#include "graphics/opengl/context.h"
#include "graphics/opengl/debug.h"
#include "graphics/opengl/shader.h"
#include "graphics/opengl/system_headers.h"

// TODO: shader GL only; there is no software or fixed-function (USE_OPENGL_GAME) fallback, and
// the PSP GU blend maths and per-pixel depth test are approximated.

namespace Graphics {

namespace {

// Embedded game shader. Traditional attribute/varying/texture2D syntax
// compiled as GLSL 1.20 (translated to GLSL ES 1.00 on GLES contexts), so no
// .vertex/.fragment asset files are needed. The fragment stage mirrors the
// CPU GU_TFX_* emulation: texture * vertexColor * materialDiffuse, with the
// decal/blend/replace/add variants and the MAP_FACTOR mix. The lit branch is
// a close approximation of the CPU normal-dot/specular/reflection/bump
// shading; the standalone demo renders unlit. Unlit follows the GE with
// lighting off: the vertex colour, or the material ambient without one.
static const char *const kGMOVertexShader =
	"attribute vec3 a_position;\n"
	"attribute vec3 a_normal;\n"
	"attribute vec2 a_uv;\n"
	"attribute vec4 a_color;\n"
	"uniform mat4 u_projection;\n"
	"varying vec3 v_normal;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"    v_normal = a_normal;\n"
	"    v_uv = a_uv;\n"
	"    v_color = a_color;\n"
	"    gl_Position = u_projection * vec4(a_position, 1.0);\n"
	"}\n";

static const char *const kGMOFragmentShader =
	"varying vec3 v_normal;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"uniform sampler2D u_texture;\n"
	"uniform vec3 u_diffuse;\n"
	"uniform vec3 u_ambient;\n"
	"uniform vec3 u_emission;\n"
	"uniform vec3 u_specular;\n"
	"uniform float u_specPower;\n"
	"uniform float u_reflection;\n"
	"uniform float u_refraction;\n"
	"uniform float u_bump;\n"
	"uniform float u_mapFactor;\n"
	"uniform int u_texFunc;\n"
	"uniform int u_texComp;\n"
	"uniform int u_hasTexture;\n"
	"uniform int u_hasUv;\n"
	"uniform int u_hasNormal;\n"
	"uniform int u_unlit;\n"
	"uniform int u_hasColor;\n"
	"uniform float u_alpha;\n"
	"void main() {\n"
	"    vec3 frag = u_diffuse * v_color.rgb;\n"
	"    if (u_unlit != 0)\n"
	"        frag = (u_hasColor != 0) ? v_color.rgb : u_ambient;\n"
	"    float fa = v_color.a * u_alpha;\n"
	"    vec4 texel = vec4(1.0, 1.0, 1.0, 1.0);\n"
	"    if (u_hasTexture != 0 && u_hasUv != 0)\n"
	"        texel = texture2D(u_texture, v_uv);\n"
	"    vec3 o = frag;\n"
	"    float oa = fa;\n"
	"    if (u_texFunc == 3) {\n"                // GU_TFX_REPLACE
	"        o = texel.rgb;\n"
	"        oa = (u_texComp == 1) ? texel.a : fa;\n"
	"    } else if (u_texFunc == 4) {\n"         // GU_TFX_ADD
	"        o = frag + texel.rgb;\n"
	"        oa = (u_texComp == 1) ? texel.a * fa : fa;\n"
	"    } else if (u_texFunc == 1) {\n"         // GU_TFX_DECAL
	"        o = (u_texComp == 1) ? mix(frag, texel.rgb, texel.a) : texel.rgb;\n"
	"        oa = fa;\n"
	"    } else if (u_texFunc == 2) {\n"         // GU_TFX_BLEND
	"        o = mix(frag, vec3(0.5), texel.rgb);\n"
	"        oa = (u_texComp == 1) ? texel.a * fa : fa;\n"
	"    } else {\n"                            // GU_TFX_MODULATE
	"        o = frag * mix(vec3(1.0), texel.rgb, u_mapFactor);\n"
	"        oa = (u_texComp == 1) ? texel.a * fa : fa;\n"
	"    }\n"
	"    if (u_unlit != 0) {\n"
	"        o += u_emission;\n"
	"    } else {\n"
	"        float ndl = 1.0;\n"
	"        if (u_hasNormal != 0)\n"
	"            ndl = abs(dot(normalize(v_normal), vec3(0.0, 0.0, 1.0)));\n"
	"        o = o * (0.45 + 0.55 * ndl + u_ambient * 0.25);\n"
	"        o += u_emission;\n"
	"        if (u_specPower > 0.0)\n"
	"            o += u_specular * pow(ndl, u_specPower);\n"
	"        if (u_reflection > 0.0) {\n"
	"            vec3 n = (u_hasNormal != 0) ? normalize(v_normal) : vec3(0.0, 0.0, 1.0);\n"
	"            o += u_reflection * (0.25 + 0.55 * (n.z * 0.5 + 0.5));\n"
	"        }\n"
	"        if (u_refraction > 0.0)\n"
	"            o = mix(o, vec3(frag.b, frag.r, texel.r), u_refraction);\n"
	"        if (u_bump > 0.0 && u_hasTexture != 0 && u_hasUv != 0) {\n"
	"            float lum = dot(texel.rgb, vec3(0.299, 0.587, 0.114));\n"
	"            o *= clamp(ndl + (lum - 0.5) * u_bump, 0.0, 2.0);\n"
	"        }\n"
	"    }\n"
	"    gl_FragColor = vec4(o, oa);\n"
	"}\n";

struct GLTexture {
	const Graphics::Surface *surface;
	GLuint id;
};

// One batch of triangles sharing identical texture/material/blend state, so
// uniforms and blend setup only change between runs.
struct DrawRun {
	uint firstTri;
	uint triCount;
};

struct GLState {
	OpenGL::Shader *shader = nullptr;
	const Common::GMO *gmo = nullptr;
	Common::Array<Graphics::Surface *> textures; // decoded, owned
	Common::Array<GLTexture> glTextures;
	Common::Array<Tri> tris;
	Common::Array<float> verts;
	Common::Array<DrawRun> runs;
};

GLState &glState() {
	static GLState s;
	return s;
}

// Build an RGBA8888 byte buffer from any supported surface format, using the
// same channel/alpha extraction as the CPU texture sampler so both paths see
// identical texels.

void applyWrap(GLenum param, uint32 guWrap) {
	if (guWrap == kGuClamp) {
		if (OpenGLContext.textureEdgeClampSupported)
			GL_CALL(glTexParameteri(GL_TEXTURE_2D, param, GL_CLAMP_TO_EDGE));
#if !USE_FORCED_GLES && !USE_FORCED_GLES2
		else
			GL_CALL(glTexParameteri(GL_TEXTURE_2D, param, GL_CLAMP));
#endif
	} else {
		GL_CALL(glTexParameteri(GL_TEXTURE_2D, param, GL_REPEAT));
	}
}

GLuint textureFor(const Graphics::Surface *surface) {
	if (!surface)
		return 0;
	GLState &st = glState();
	for (uint i = 0; i < st.glTextures.size(); i++)
		if (st.glTextures[i].surface == surface)
			return st.glTextures[i].id;

	GLuint id = 0;
	GL_CALL(glGenTextures(1, &id));
	::OpenGL::uploadTexture(id, *surface, true);
	applyWrap(GL_TEXTURE_WRAP_S, kGuRepeat);
	applyWrap(GL_TEXTURE_WRAP_T, kGuRepeat);

	GLTexture entry;
	entry.surface = surface;
	entry.id = id;
	st.glTextures.push_back(entry);
	return id;
}

void resetResources() {
	GLState &st = glState();
	for (uint i = 0; i < st.glTextures.size(); i++)
		GL_CALL(glDeleteTextures(1, &st.glTextures[i].id));
	st.glTextures.clear();
	freeGMOTextures(st.textures);
	st.gmo = nullptr;
}

bool sameRunState(const Tri &a, const Tri &b) {
	if (a.tex != b.tex || a.hasUv != b.hasUv || a.hasNormal != b.hasNormal || a.hasCol != b.hasCol ||
	    a.alpha != b.alpha)
		return false;
	if (a.texFunc != b.texFunc || a.texComp != b.texComp)
		return false;
	if (a.texFilterMin != b.texFilterMin || a.texFilterMag != b.texFilterMag)
		return false;
	if (a.wrapU != b.wrapU || a.wrapV != b.wrapV)
		return false;
	if (a.blendEnabled != b.blendEnabled || a.blendMode != b.blendMode ||
	    a.blendSrc != b.blendSrc || a.blendDst != b.blendDst)
		return false;
	if (a.mapFactor != b.mapFactor || a.specPower != b.specPower ||
	    a.reflection != b.reflection || a.refraction != b.refraction || a.bump != b.bump)
		return false;
	for (int i = 0; i < 3; i++) {
		if (a.diffuse[i] != b.diffuse[i] || a.ambient[i] != b.ambient[i] ||
		    a.emission[i] != b.emission[i] || a.specular[i] != b.specular[i])
			return false;
	}
	return true;
}

GLenum blendFactorToGL(uint32 factor, bool isSrc) {
	switch (factor) {
	case kGuSrcAlpha: return GL_SRC_ALPHA;
	case kGuOneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
	case kGuDstAlpha: return GL_DST_ALPHA;
	case kGuOneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
	case kGuOneMinusDoubleSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA; // approximation
	case kGuDoubleSrcAlpha: return GL_SRC_ALPHA;                  // approximation
	case kGuDoubleDstAlpha: return GL_DST_ALPHA;                  // approximation
	case kGuOneMinusDoubleDstAlpha: return GL_ONE_MINUS_DST_ALPHA; // approximation
	case kGuOtherColor:
	case kGuOneMinusOtherColor:
		// GU's "other color" factors depend on the destination color and are
		// not expressible without GL_ARB_blend_func_extended; fall back to
		// the common case (opaque source / full destination). Approximation.
		return isSrc ? GL_ONE : GL_ZERO;
	case kGuFix:
	default:
		return GL_ONE;
	}
}

void applyBlend(const Tri &t) {
	// Animated material alpha fades parts without a BLEND_FUNC of their own.
	if (!t.blendEnabled && t.alpha < 1.0f) {
		GL_CALL(glEnable(GL_BLEND));
		GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
		GL_CALL(glBlendEquation(GL_FUNC_ADD));
		return;
	}
	if (!t.blendEnabled) {
		GL_CALL(glDisable(GL_BLEND));
		return;
	}
	GL_CALL(glEnable(GL_BLEND));
	GL_CALL(glBlendFunc(blendFactorToGL(t.blendSrc, true), blendFactorToGL(t.blendDst, false)));

	GLenum eq = GL_FUNC_ADD;
	switch (t.blendMode) {
	case kGuSubtract: eq = GL_FUNC_SUBTRACT; break;
	case kGuReverseSubtract: eq = GL_FUNC_REVERSE_SUBTRACT; break;
	case kGuMin:
	case kGuMax:
	case kGuAbs:
		// GL_MIN/GL_MAX need GL_ARB_imaging; otherwise approximate with add.
		if (OpenGLContext.imagingSupported && t.blendMode == kGuMin)
			eq = GL_MIN;
		else if (OpenGLContext.imagingSupported && t.blendMode == kGuMax)
			eq = GL_MAX;
		break;
	default: break;
	}
	GL_CALL(glBlendEquation(eq));
}

// Shared draw of a quad in screen space (0..W, 0..H, y down). Used by the
// overlay path; the vertex format matches kGMOVertexShader.
} // namespace

bool GMORenderer::isAvailable() {
	if (!g_system)
		return false;
	if (!g_system->hasFeature(OSystem::kFeatureOpenGLForGame))
		return false;
	return OpenGLContext.type != OpenGL::kContextNone && OpenGLContext.enginesShadersSupported;
}

void GMORenderer::computeBounds(const Common::GMO &gmo, int width, int height, float rotY,
                                const Common::GMO::BonePose *bonePose,
                                const Common::GMO::MaterialPose *materialPose, GMOBounds *outBounds) {
	if (outBounds)
		*outBounds = GMOBounds();
	if (width <= 0 || height <= 0)
		return;

	Common::Array<Graphics::Surface *> textures;
	decodeGMOTextures(gmo, textures);
	Common::Array<Tri> tris;
	buildGMOMesh(gmo, textures, width, height, rotY, bonePose, materialPose,
	             nullptr, outBounds, tris);
	freeGMOTextures(textures);
}

void GMORenderer::invalidate() {
	resetResources();
}

void GMORenderer::render(const Common::GMO &gmo, int width, int height, float rotY,
                           const Common::GMO::BonePose *bonePose, const Common::GMO::MaterialPose *materialPose,
                           const GMOBounds *fixedBounds, GMOBounds *outBounds,
                           bool unlit) {
	if (width <= 0 || height <= 0)
		return;

	GLState &st = glState();

	if (!st.shader) {
		static const char *const attributes[] = { "a_position", "a_normal", "a_uv", "a_color", nullptr };
		st.shader = OpenGL::Shader::fromStrings("GMO", kGMOVertexShader, kGMOFragmentShader, attributes, 120);
	}

	// Textures do not change while the same model is shown, so decode them
	// once and key the GL objects by the (stable) surface pointer.
	if (st.gmo != &gmo) {
		resetResources();
		decodeGMOTextures(gmo, st.textures);
		st.gmo = &gmo;
	}

	buildGMOMesh(gmo, st.textures, width, height, rotY, bonePose, materialPose,
	             fixedBounds, outBounds, st.tris);

	// Interleave 12 floats per vertex (position, normal, uv, color) and group
	// consecutive triangles that share material/texture/blend state.
	st.verts.clear();
	st.runs.clear();
	if (st.tris.empty())
		return;

	st.verts.reserve(st.tris.size() * 3 * 12);
	for (uint i = 0; i < st.tris.size(); i++) {
		const Tri &t = st.tris[i];
		if (st.runs.empty() || !sameRunState(st.tris[st.runs.back().firstTri], t)) {
			DrawRun run;
			run.firstTri = i;
			run.triCount = 1;
			st.runs.push_back(run);
		} else {
			st.runs.back().triCount++;
		}
		for (int k = 0; k < 3; k++) {
			st.verts.push_back(t.x[k]);
			st.verts.push_back(t.y[k]);
			st.verts.push_back(t.z[k]);
			if (t.hasNormal) {
				st.verts.push_back(t.nx[k]);
				st.verts.push_back(t.ny[k]);
				st.verts.push_back(t.nz[k]);
			} else {
				st.verts.push_back(0.0f);
				st.verts.push_back(0.0f);
				st.verts.push_back(0.0f);
			}
			st.verts.push_back(t.hasUv ? t.u[k] : 0.0f);
			st.verts.push_back(t.hasUv ? t.v[k] : 0.0f);
			st.verts.push_back(t.cr[k]);
			st.verts.push_back(t.cg[k]);
			st.verts.push_back(t.cb[k]);
			st.verts.push_back(t.ca[k]);
		}
	}

	// Z range for the depth buffer. The smaller `Tri::z` is the nearer vertex,
	// so map it to the smaller NDC z (0.01) and use the standard GL_LESS test.
	// The software rasterizer z-buffered per pixel;
	// without depth, overlapping parts of a model interpenetrate ("shred")
	// because the material sort is not a depth sort.
	float zMin = 1e30f, zMax = -1e30f;
	for (uint i = 0; i < st.tris.size(); i++)
		for (int k = 0; k < 3; k++) {
			if (st.tris[i].z[k] < zMin) zMin = st.tris[i].z[k];
			if (st.tris[i].z[k] > zMax) zMax = st.tris[i].z[k];
		}
	float zRange = zMax - zMin;
	if (zRange < 1e-6f) zRange = 1.0f;
	float zScale = 0.98f / zRange;
	float zBias = 0.01f - zMin * zScale;

	float proj[16] = {
		2.0f / width, 0, 0, 0,
		0, -2.0f / height, 0, 0,
		0, 0, zScale, 0,
		-1, 1, zBias, 1
	};

	OpenGL::Shader *shader = st.shader;
	GL_CALL(glDisable(GL_CULL_FACE));
	GL_CALL(glEnable(GL_DEPTH_TEST));
	GL_CALL(glDepthFunc(GL_LESS));
	GL_CALL(glDepthMask(GL_TRUE));

	// The CPU path clears to the demo's dark background colour. The default
	// clear depth (1.0) means the GL_LESS test above keeps the nearest
	// fragment.
	GL_CALL(glClearColor(32.0f / 255.0f, 32.0f / 255.0f, 40.0f / 255.0f, 1.0f));
	GL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

	shader->use(true);
	GL_CALL(glUniformMatrix4fv(shader->getUniformLocation("u_projection"), 1, GL_FALSE, proj));
	shader->setUniform("u_texture", (unsigned int)0);
	shader->setUniform("u_unlit", (unsigned int)(unlit ? 1 : 0));

	const int stride = 12 * sizeof(float);
	const byte *base = (const byte *)st.verts.data();
	shader->enableVertexAttribute("a_position", 3, GL_FLOAT, GL_FALSE, stride, base);
	shader->enableVertexAttribute("a_normal", 3, GL_FLOAT, GL_FALSE, stride, base + 3 * sizeof(float));
	shader->enableVertexAttribute("a_uv", 2, GL_FLOAT, GL_FALSE, stride, base + 6 * sizeof(float));
	shader->enableVertexAttribute("a_color", 4, GL_FLOAT, GL_FALSE, stride, base + 8 * sizeof(float));
	shader->use(true);
	GL_CALL(glActiveTexture(GL_TEXTURE0));

	for (uint r = 0; r < st.runs.size(); r++) {
		const Tri &t = st.tris[st.runs[r].firstTri];

		shader->setUniform("u_diffuse", Math::Vector3d(t.diffuse[0], t.diffuse[1], t.diffuse[2]));
		shader->setUniform("u_ambient", Math::Vector3d(t.ambient[0], t.ambient[1], t.ambient[2]));
		shader->setUniform("u_emission", Math::Vector3d(t.emission[0], t.emission[1], t.emission[2]));
		shader->setUniform("u_specular", Math::Vector3d(t.specular[0], t.specular[1], t.specular[2]));
		shader->setUniform1f("u_specPower", t.specPower);
		shader->setUniform1f("u_reflection", t.reflection);
		shader->setUniform1f("u_refraction", t.refraction);
		shader->setUniform1f("u_bump", t.bump);
		shader->setUniform1f("u_mapFactor", t.mapFactor);
		shader->setUniform("u_texFunc", (unsigned int)t.texFunc);
		shader->setUniform("u_texComp", (unsigned int)t.texComp);
		shader->setUniform("u_hasTexture", (unsigned int)(t.tex ? 1 : 0));
		shader->setUniform("u_hasUv", (unsigned int)(t.hasUv ? 1 : 0));
		shader->setUniform("u_hasNormal", (unsigned int)(t.hasNormal ? 1 : 0));
		shader->setUniform("u_hasColor", (unsigned int)(t.hasCol ? 1 : 0));
		shader->setUniform1f("u_alpha", t.alpha);

		if (t.tex) {
			GLuint id = textureFor(t.tex);
			GL_CALL(glBindTexture(GL_TEXTURE_2D, id));
			GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
			applyWrap(GL_TEXTURE_WRAP_S, t.wrapU);
			applyWrap(GL_TEXTURE_WRAP_T, t.wrapV);
		}

		if (t.alpha <= 0.0f)
			continue; // hidden by its motion (DIFFUSE alpha 0)
		applyBlend(t);
		// GMO layers are drawn in material order, so every surface must populate
		// the depth buffer before later material runs can cover it.
		GL_CALL(glDepthMask(GL_TRUE));
		GL_CALL(glDrawArrays(GL_TRIANGLES, st.runs[r].firstTri * 3, st.runs[r].triCount * 3));
	}

	// Leave the clear colour as the backend expects for its own clears.
	GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
}

void GMORenderer::shutdown() {
	GLState &st = glState();
	resetResources();
	delete st.shader;
	st.shader = nullptr;
	st.verts.clear();
	st.runs.clear();
	st.tris.clear();
}

} // End of namespace Graphics

#endif // USE_OPENGL_SHADERS
