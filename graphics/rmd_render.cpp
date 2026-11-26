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

#include "graphics/rmd_render.h"

#if defined(USE_OPENGL_SHADERS) || defined(RMD_RENDER_COMPILE_CHECK)

#include <cstring>

#include "common/array.h"
#include "common/system.h"
#include "math/vector3d.h"
#include "graphics/rmd_render_mesh.h"
#include "graphics/opengl/blitter.h"
#include "graphics/opengl/context.h"
#include "graphics/opengl/debug.h"
#include "graphics/opengl/shader.h"
#include "graphics/opengl/system_headers.h"

// TODO: shader GL only; there is no software or fixed-function (USE_OPENGL_GAME) fallback, so
// without engine shaders the SMT demo skips its model phase.

namespace Graphics {
namespace {

static const char *const kVertexShader =
	"attribute vec3 a_position;\n"
	"attribute vec3 a_normal;\n"
	"attribute vec2 a_uv;\n"
	"attribute vec4 a_color;\n"
	"uniform mat4 u_projection;\n"
	"varying vec3 v_normal;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  v_normal = a_normal; v_uv = a_uv; v_color = a_color;\n"
	"  gl_Position = u_projection * vec4(a_position, 1.0);\n"
	"}\n";

static const char *const kFragmentShader =
	"varying vec3 v_normal;\n"
	"varying vec2 v_uv;\n"
	"varying vec4 v_color;\n"
	"uniform sampler2D u_texture;\n"
	"uniform vec3 u_diffuse;\n"
	"uniform int u_hasTexture;\n"
	"uniform int u_hasUv;\n"
	"uniform int u_unlit;\n"
	"uniform int u_alphaTest;\n"
	"uniform int u_vertexColor;\n"
	"uniform int u_alphaMask;\n"
	"uniform int u_flipV;\n"
	"uniform float u_alphaMin;\n"
	"void main() {\n"
	"  int textured = (u_hasTexture != 0 && u_hasUv != 0) ? 1 : 0;\n"
	"  vec4 texel = vec4(1.0);\n"
	"  if (textured != 0) texel = texture2D(u_texture, u_flipV != 0 ? vec2(v_uv.x, 1.0 - v_uv.y) : v_uv);\n"
	"  if (u_alphaMask != 0) texel.rgb = vec3(1.0);\n"
	"  if (u_alphaMin > 0.0 && texel.a * v_color.a < u_alphaMin) discard;\n"
	// Cutout masks (fences, foliage) carry binary 0/255 texture alpha
	// independent of the material's blend state -- alpha *test* (discard),
	// not blend, so opaque neighbors keep writing depth correctly.
	"  if (u_alphaTest != 0 && texel.a < 0.5) discard;\n"
	// Real PS2 RW material data stores per-vertex prelight colors even on
	// textured geometry, but that data isn't meant to modulate the texture
	// (real samples like N23801.RMD carry near-black prelight on a richly
	// textured mesh -- multiplying it in crushes the texture to black).
	// Tried gating this on the real ModulateMatColor geometry flag (0x40)
	// instead of texture-presence, but real data disproves that too: some
	// geometries with the flag SET and some WITHOUT it both carry the same
	// literal (0,0,0) prelight in the same file (N23801.RMD geometry 0 vs 6)
	// -- the flag doesn't gate "safe to multiply", so there's no known-safe
	// way to consume it here. Vertex color only matters as a flat tint on
	// untextured geometry.
	"  vec3 color = (textured != 0 && u_vertexColor == 0) ? (texel.rgb * u_diffuse) : (texel.rgb * v_color.rgb * u_diffuse);\n"
	"  if (u_unlit == 0) color *= 0.45 + 0.55 * abs(dot(normalize(v_normal), vec3(0.0, 0.0, 1.0)));\n"
	"  gl_FragColor = vec4(color, texel.a * v_color.a);\n"
	"}\n";

using Tri = RMDRenderTriangle;

struct GLTexture { const Graphics::Surface *surface; GLuint id; };
struct DrawRun { uint first, count; };
struct State {
	OpenGL::Shader *shader = nullptr;
	const Common::RMD *model = nullptr;
	Common::Array<Graphics::Surface *> textures;
	Common::Array<GLTexture> glTextures;
	Common::Array<Tri> tris;
	Common::Array<float> verts;
	Common::Array<DrawRun> runs;
	GLuint previousFrame = 0; ///< copy of the last completed frame, for screen echo effects
	bool hasPreviousFrame = false;
};

State &state() { static State s; return s; }

void freeTextures(Common::Array<Graphics::Surface *> &textures) {
	for (uint i = 0; i < textures.size(); ++i) if (textures[i]) { textures[i]->free(); delete textures[i]; }
	textures.clear();
}

void build(const Common::RMD &rmd, const Common::Array<Graphics::Surface *> &textures, int width, int height,
		   float rotY, const Common::RMD::BonePose *pose, const RMDBounds *fixed, RMDBounds *bounds,
		   Common::Array<Tri> &out, const Common::RMD::MaterialPose *materialPose = nullptr,
		   const Common::Array<EffectBatch> *effects = nullptr, Common::Array<EffectBatch> *effectsOut = nullptr) {
	RMDRenderMesh mesh;
	assembleRMDMesh(rmd, textures, width, height, rotY, pose, fixed, mesh, materialPose, effects);
	out = mesh.triangles;
	if (effectsOut)
		*effectsOut = mesh.effects;
	if (bounds)
		*bounds = mesh.bounds;
}

void reset() {
	State &s = state();
	for (uint i = 0; i < s.glTextures.size(); ++i) GL_CALL(glDeleteTextures(1, &s.glTextures[i].id));
	if (s.previousFrame) { GL_CALL(glDeleteTextures(1, &s.previousFrame)); s.previousFrame = 0; }
	s.hasPreviousFrame = false;
	s.glTextures.clear(); freeTextures(s.textures); s.model = nullptr;
}

GLuint textureFor(const Graphics::Surface *surface) {
	if (!surface) return 0;
	State &s = state();
	for (uint i = 0; i < s.glTextures.size(); ++i) if (s.glTextures[i].surface == surface) return s.glTextures[i].id;
	GLuint id = 0; GL_CALL(glGenTextures(1, &id)); ::OpenGL::uploadTexture(id, *surface, true);
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT)); GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
	GLTexture entry = {surface, id};
	s.glTextures.push_back(entry); return id;
}

GLenum wrapMode(byte mode) {
	switch (mode) {
	case 2: return GL_MIRRORED_REPEAT;
	case 3: return GL_CLAMP_TO_BORDER;
	default: return GL_REPEAT;
	}
}

} // namespace

namespace {

// Effect blend modes in the game's draw-list order, and their blend equations (PS2 GS ALPHA
// register): 0/1 Cs*As + Cd*(1-As), 2 Cs*As + Cd, 3/4 Cd - Cs*As, 5/6 Cs - Cd*As.
const int kEffectDrawOrder[7] = {0, 1, 4, 5, 2, 6, 3};

void setEffectBlend(int mode) {
	switch (mode) {
	case 2:
		GL_CALL(glBlendEquation(GL_FUNC_ADD)); GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE));
		break;
	case 3: case 4:
		GL_CALL(glBlendEquation(GL_FUNC_REVERSE_SUBTRACT)); GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE));
		break;
	case 5: case 6:
		GL_CALL(glBlendEquation(GL_FUNC_SUBTRACT)); GL_CALL(glBlendFunc(GL_ONE, GL_SRC_ALPHA));
		break;
	default:
		GL_CALL(glBlendEquation(GL_FUNC_ADD)); GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
		break;
	}
}

void drawEffects(State &s, const Common::Array<EffectBatch> &batches, EffectTextureSource *textures) {
	if (batches.empty())
		return;
	// Effect lists: bilinear textures, vertex colours, no culling (culled batches arrive pre-culled).
	GL_CALL(glEnable(GL_BLEND));
	s.shader->setUniform("u_vertexColor", (unsigned int)1);
	s.shader->setUniform("u_diffuse", Math::Vector3d(1, 1, 1));
	s.shader->setUniform("u_alphaTest", (unsigned int)0);
	s.shader->setUniform("u_hasUv", (unsigned int)1);
	Common::Array<float> verts;
	// The 3D effect lists first, then the screen filter lists, each in the game's list order.
	for (int overlay = 0; overlay < 2; ++overlay)
	for (int order = 0; order < 7; ++order) {
		for (uint b = 0; b < batches.size(); ++b) {
			const EffectBatch &batch = batches[b];
			if (batch.overlay != (overlay != 0) || batch.blendMode != kEffectDrawOrder[order] || batch.vertices.empty())
				continue;
			verts.clear();
			for (uint i = 0; i < batch.vertices.size(); ++i) {
				const EffectVertex &v = batch.vertices[i];
				const float vertex[12] = {v.x, v.y, v.z, 0, 0, 1, v.u, v.v, v.r, v.g, v.b, v.a};
				for (int k = 0; k < 12; ++k)
					verts.push_back(vertex[k]);
			}
			GLuint texture = 0;
			if (batch.texture.previousFrame) {
				texture = s.hasPreviousFrame ? s.previousFrame : 0;
				if (!texture)
					continue; // nothing to echo on the very first frame
			} else {
				const Graphics::Surface *surface = batch.texture.surface ? batch.texture.surface :
					textures && batch.texture.valid() ? textures->textureSurface(batch.texture) : nullptr;
				texture = surface ? textureFor(surface) : 0;
			}
			s.shader->setUniform("u_hasTexture", (unsigned int)(texture ? 1 : 0));
			s.shader->setUniform("u_alphaMask", (unsigned int)(batch.alphaMask ? 1 : 0));
			s.shader->setUniform("u_flipV", (unsigned int)(batch.texture.previousFrame ? 1 : 0));
			if (texture) {
				GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));
				GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
				GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			}
			// The GS accepts equal depth; with the test off it still writes depth when asked to.
			const bool depthWrite = batch.depthWrite && !batch.overlay;
			if (batch.overlay || (!batch.depthTest && !depthWrite)) {
				GL_CALL(glDisable(GL_DEPTH_TEST));
			} else {
				GL_CALL(glEnable(GL_DEPTH_TEST));
				GL_CALL(glDepthFunc(batch.depthTest ? GL_LEQUAL : GL_ALWAYS));
			}
			// Alpha-gated depth writes: draw the colour without depth, then depth where alpha passes.
			const bool gated = depthWrite && batch.depthWriteMinAlpha > 0;
			GL_CALL(glDepthMask(depthWrite && !gated ? GL_TRUE : GL_FALSE));
			if (batch.depthOnly) { GL_CALL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE)); }
			setEffectBlend(batch.blendEquation >= 0 ? batch.blendEquation : batch.blendMode);
			const int stride = 12 * sizeof(float); const byte *base = (const byte *)verts.data();
			s.shader->enableVertexAttribute("a_position", 3, GL_FLOAT, GL_FALSE, stride, base);
			s.shader->enableVertexAttribute("a_normal", 3, GL_FLOAT, GL_FALSE, stride, base + 3 * sizeof(float));
			s.shader->enableVertexAttribute("a_uv", 2, GL_FLOAT, GL_FALSE, stride, base + 6 * sizeof(float));
			s.shader->enableVertexAttribute("a_color", 4, GL_FLOAT, GL_FALSE, stride, base + 8 * sizeof(float));
			s.shader->use(true);
			if (batch.depthOnly && gated)
				s.shader->setUniform1f("u_alphaMin", batch.depthWriteMinAlpha);
			if (!(batch.depthOnly && gated))
				GL_CALL(glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.vertices.size()));
			if (gated) {
				if (!batch.depthOnly) { GL_CALL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE)); }
				GL_CALL(glDepthMask(GL_TRUE));
				s.shader->setUniform1f("u_alphaMin", batch.depthWriteMinAlpha);
				GL_CALL(glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.vertices.size()));
				s.shader->setUniform1f("u_alphaMin", 0.0f);
				GL_CALL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
			}
			if (batch.depthOnly) { GL_CALL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE)); }
		}
	}
	GL_CALL(glBlendEquation(GL_FUNC_ADD)); GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	GL_CALL(glEnable(GL_DEPTH_TEST)); GL_CALL(glDepthMask(GL_FALSE)); GL_CALL(glDepthFunc(GL_LESS));
	s.shader->setUniform("u_vertexColor", (unsigned int)0);
	s.shader->setUniform("u_alphaMask", (unsigned int)0);
	s.shader->setUniform("u_flipV", (unsigned int)0);
}

/** Keep the finished frame for effects that echo the previous frame (only while one is shown). */
void capturePreviousFrame(State &s, const Common::Array<EffectBatch> &batches, int width, int height) {
	bool wanted = false;
	for (uint b = 0; b < batches.size() && !wanted; ++b)
		wanted = batches[b].texture.previousFrame;
	if (!wanted) {
		s.hasPreviousFrame = false;
		return;
	}
	if (!s.previousFrame)
		GL_CALL(glGenTextures(1, &s.previousFrame));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, s.previousFrame));
	GL_CALL(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width, height, 0)); // RGB: alpha reads as 1
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	s.hasPreviousFrame = true;
}

} // namespace

bool RMDRenderer::isAvailable() { return g_system && g_system->hasFeature(OSystem::kFeatureOpenGLForGame) && OpenGLContext.type != OpenGL::kContextNone && OpenGLContext.enginesShadersSupported; }

void RMDRenderer::computeBounds(const Common::RMD &rmd, int width, int height, float rotY, const Common::RMD::BonePose *pose, RMDBounds *out) {
	Common::Array<Graphics::Surface *> textures; textures.resize(rmd.textures().size());
	for (uint i = 0; i < textures.size(); ++i) textures[i] = rmd.getTextureSurface(i);
	Common::Array<Tri> tris; build(rmd, textures, width, height, rotY, pose, nullptr, out, tris);
	freeTextures(textures);
}

void RMDRenderer::invalidate() { reset(); }

void RMDRenderer::render(const Common::RMD &rmd, int width, int height, float rotY, const Common::RMD::BonePose *pose, const RMDBounds *fixed, RMDBounds *out, bool unlit, const Common::RMD::MaterialPose *materialPose,
		const Common::Array<EffectBatch> *effects, EffectTextureSource *effectTextures) {
	if (width <= 0 || height <= 0) return;
	State &s = state();
	if (!s.shader) { static const char *const attrs[] = {"a_position", "a_normal", "a_uv", "a_color", nullptr}; s.shader = OpenGL::Shader::fromStrings("RMD", kVertexShader, kFragmentShader, attrs, 120); }
	if (s.model != &rmd) { reset(); s.model = &rmd; s.textures.resize(rmd.textures().size()); for (uint i = 0; i < s.textures.size(); ++i) s.textures[i] = rmd.getTextureSurface(i); }
	Common::Array<EffectBatch> effectBatches;
	build(rmd, s.textures, width, height, rotY, pose, fixed, out, s.tris, materialPose, effects, &effectBatches); s.verts.clear(); s.runs.clear(); if (s.tris.empty()) return;
	for (uint i = 0; i < s.tris.size(); ++i) {
		const Tri *previous = s.runs.empty() ? nullptr : &s.tris[s.runs.back().first];
		bool same = previous && previous->texture == s.tris[i].texture &&
			previous->horizontalAddressing == s.tris[i].horizontalAddressing &&
			previous->verticalAddressing == s.tris[i].verticalAddressing &&
			previous->alpha == s.tris[i].alpha && previous->alphaTest == s.tris[i].alphaTest &&
			previous->hasUv == s.tris[i].hasUv;
		if (same) for (int c = 0; c < 3; ++c) same = same && previous->diffuse[c] == s.tris[i].diffuse[c];
		// Cap run length even when state doesn't change: two coincident
		// duplicate/decal layers that happen to share the same material
		// (not just different ones, as originally assumed) would
		// otherwise merge into one run and get the exact same polygon
		// offset below -- no separation at all. Splitting long same-state
		// runs into smaller draw calls guarantees every reasonably-sized
		// contiguous sub-mesh gets its own offset, at the cost of a few
		// extra draw calls on large meshes.
		const uint kMaxRunTriangles = 32;
		if (!same || s.runs.back().count >= kMaxRunTriangles) s.runs.push_back({i, 1}); else ++s.runs.back().count;
		for (int k = 0; k < 3; ++k) for (int n = 0; n < 12; ++n) s.verts.push_back(((float *)&s.tris[i].v[k])[n]);
	}
	float zMin = 1e30f, zMax = -1e30f; for (uint i = 0; i < s.tris.size(); ++i) for (int k = 0; k < 3; ++k) { zMin = MIN(zMin, s.tris[i].v[k].z); zMax = MAX(zMax, s.tris[i].v[k].z); }
	for (uint b = 0; b < effectBatches.size(); ++b) for (uint i = 0; i < effectBatches[b].vertices.size(); ++i) { zMin = MIN(zMin, effectBatches[b].vertices[i].z); zMax = MAX(zMax, effectBatches[b].vertices[i].z); }
	float range = MAX(zMax - zMin, 1.0f), proj[16] = {2.0f / width, 0, 0, 0, 0, -2.0f / height, 0, 0, 0, 0, 0.98f / range, 0, -1, 1, 0.01f - zMin * 0.98f / range, 1};
	GLboolean previousDepthMask = GL_TRUE;
	GL_CALL(glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask));
	GL_CALL(glDisable(GL_CULL_FACE)); GL_CALL(glEnable(GL_DEPTH_TEST)); GL_CALL(glDepthFunc(GL_LESS));
	// Some real models (e.g. P3F PS024.RMD) genuinely author coincident
	// duplicate geometry -- separate triangles at (near-)identical bind
	// positions and identical skin weights, on different materials, as a
	// multi-pass decal/pattern layer. The gap between layers is a few
	// thousandths of a world unit, so the two near-parallel surfaces cross
	// each other sub-pixel by sub-pixel and z-fight/moire regardless of
	// depth-buffer precision (confirmed 24-bit here). Nothing in the material
	// data (flags/blend/alpha) says which layer is meant to win, and no
	// documented RenderWare/PS2 GS convention resolves it either (RW MatFX
	// dual-pass and the GS ZTE bit are both all-or-nothing, not per-material).
	// A tiny, monotonically-growing polygon offset per draw run breaks the
	// tie deterministically by draw order instead of flickering -- the bias
	// is minuscule next to genuine depth separations elsewhere in the model.
	GL_CALL(glEnable(GL_POLYGON_OFFSET_FILL));
	GL_CALL(glClearColor(32.0f / 255.0f, 32.0f / 255.0f, 40.0f / 255.0f, 1.0f));
	GL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	s.shader->use(true); GL_CALL(glUniformMatrix4fv(s.shader->getUniformLocation("u_projection"), 1, GL_FALSE, proj)); s.shader->setUniform("u_texture", (unsigned int)0); s.shader->setUniform("u_unlit", (unsigned int)(unlit ? 1 : 0)); s.shader->setUniform("u_vertexColor", (unsigned int)0); s.shader->setUniform("u_alphaMask", (unsigned int)0); s.shader->setUniform("u_flipV", (unsigned int)0); s.shader->setUniform1f("u_alphaMin", 0.0f);
	const int stride = 12 * sizeof(float); const byte *base = (const byte *)s.verts.data(); s.shader->enableVertexAttribute("a_position", 3, GL_FLOAT, GL_FALSE, stride, base); s.shader->enableVertexAttribute("a_normal", 3, GL_FLOAT, GL_FALSE, stride, base + 3 * sizeof(float)); s.shader->enableVertexAttribute("a_uv", 2, GL_FLOAT, GL_FALSE, stride, base + 6 * sizeof(float)); s.shader->enableVertexAttribute("a_color", 4, GL_FLOAT, GL_FALSE, stride, base + 8 * sizeof(float)); GL_CALL(glActiveTexture(GL_TEXTURE0));
	s.shader->use(true);
	for (uint r = 0; r < s.runs.size(); ++r) { const Tri &t = s.tris[s.runs[r].first]; s.shader->setUniform("u_diffuse", Math::Vector3d(t.diffuse[0], t.diffuse[1], t.diffuse[2])); s.shader->setUniform("u_hasTexture", (unsigned int)(t.texture ? 1 : 0)); s.shader->setUniform("u_hasUv", (unsigned int)(t.hasUv ? 1 : 0)); s.shader->setUniform("u_alphaTest", (unsigned int)(t.alphaTest ? 1 : 0)); if (t.texture) { GL_CALL(glBindTexture(GL_TEXTURE_2D, textureFor(t.texture))); GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode(t.horizontalAddressing))); GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode(t.verticalAddressing))); } if (t.alpha) { GL_CALL(glEnable(GL_BLEND)); GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)); GL_CALL(glDepthMask(GL_FALSE)); } else { GL_CALL(glDisable(GL_BLEND)); GL_CALL(glDepthMask(GL_TRUE)); } GL_CALL(glPolygonOffset(0.0f, -(float)r * 32.0f)); GL_CALL(glDrawArrays(GL_TRIANGLES, s.runs[r].first * 3, s.runs[r].count * 3)); }
	GL_CALL(glPolygonOffset(0.0f, 0.0f));
	GL_CALL(glDisable(GL_POLYGON_OFFSET_FILL));
	drawEffects(s, effectBatches, effectTextures);
	capturePreviousFrame(s, effectBatches, width, height);
	GL_CALL(glDepthMask(previousDepthMask));
	GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
}

void RMDRenderer::shutdown() { State &s = state(); reset(); delete s.shader; s.shader = nullptr; s.tris.clear(); s.verts.clear(); s.runs.clear(); }

} // End of namespace Graphics

#endif
