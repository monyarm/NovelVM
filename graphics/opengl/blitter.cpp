/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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

#include "graphics/opengl/blitter.h"

#ifdef USE_OPENGL_SHADERS

#include "common/array.h"
#include "common/system.h"

#include "graphics/opengl/context.h"
#include "graphics/opengl/debug.h"
#include "graphics/opengl/shader.h"
#include "graphics/opengl/system_headers.h"
#include "graphics/surface.h"

namespace OpenGL {

namespace {

// Embedded 2D blit program. Traditional attribute/varying/texture2D syntax
// compiled as GLSL 1.20 (translated to GLSL ES 1.00 on GLES contexts).
static const char *const kBlitVertexShader =
	"attribute vec2 a_position;\n"
	"attribute vec2 a_uv;\n"
	"uniform mat4 u_projection;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"    v_uv = a_uv;\n"
	"    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
	"}\n";

static const char *const kBlitFragmentShader =
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_texture;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(u_texture, v_uv);\n"
	"}\n";

// Cap on cached surface textures so a stream of one-shot surfaces cannot grow
// GPU memory without bound; the video/tmx/bust surfaces are stable pointers.
const uint kMaxCachedTextures = 16;

struct BlitTexture {
	const Graphics::Surface *surface;
	int w;
	int h;
	GLuint id;
};

struct BlitState {
	Shader *shader = nullptr;
	GLuint vbo = 0;
	GLuint textTex = 0;
	int width = 0;
	int height = 0;
	Common::Array<BlitTexture> textures;
};

BlitState &blitState() {
	static BlitState s;
	return s;
}

} // namespace

void surfaceToRGBA(const Graphics::Surface &src, Common::Array<byte> &out) {
	const Graphics::PixelFormat &f = src.format;
	out.resize((uint32)(src.w * src.h * 4));

	// Fast path: the video decoder is asked for RGBA8888, which is exactly
	// the byte order the uploader wants, so just copy rows (honouring pitch).
	if (f == Graphics::PixelFormat::createFormatRGBA32()) {
		for (uint16 y = 0; y < src.h; y++)
			memcpy(&out[(uint32)y * src.w * 4], src.getBasePtr(0, y), (uint32)src.w * 4);
		return;
	}

	for (uint16 y = 0; y < src.h; y++) {
		const byte *row = (const byte *)src.getBasePtr(0, y);
		for (uint16 x = 0; x < src.w; x++) {
			uint32 p = 0;
			memcpy(&p, row + x * f.bytesPerPixel, f.bytesPerPixel);
			byte r, g, b;
			f.colorToRGB(p, r, g, b);
			byte a = (f.aLoss == 8) ? 255 : (byte)(((p >> f.aShift) << f.aLoss) & 0xFF);
			byte *d = &out[(uint32)(y * src.w + x) * 4];
			d[0] = r; d[1] = g; d[2] = b; d[3] = a;
		}
	}
}

// allocate=true (re)allocates the texture storage with glTexImage2D; when the
// storage already exists and the size is unchanged, allocate=false refreshes
// it with glTexSubImage2D, matching how ScummVM updates movie frames.
void uploadTexture(uint id, const Graphics::Surface &s, bool allocate) {
	// Reused across calls: video playback uploads every frame, so a fresh buffer each time
	// would allocate and free a full RGBA frame per frame.
	static Common::Array<byte> rgba;
	surfaceToRGBA(s, rgba);

	GL_CALL(glBindTexture(GL_TEXTURE_2D, id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
	if (allocate)
		GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.w, s.h, 0,
		                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()));
	else
		GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s.w, s.h,
		                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()));
}

namespace {

GLuint createTexture(const Graphics::Surface &s) {
	GLuint id = 0;
	GL_CALL(glGenTextures(1, &id));
	uploadTexture(id, s, true);
	return id;
}

void drawTexture(GLuint tex, float x, float y, float w, float h) {
	BlitState &st = blitState();
	if (!st.shader || st.width <= 0 || st.height <= 0)
		return;

	GL_CALL(glDisable(GL_DEPTH_TEST));
	GL_CALL(glDisable(GL_CULL_FACE));
	GL_CALL(glEnable(GL_BLEND));
	GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	GL_CALL(glBlendEquation(GL_FUNC_ADD));

	const float x1 = x + w, y1 = y + h;
	const float verts[24] = {
		x,  y,  0.0f, 0.0f,
		x1, y,  1.0f, 0.0f,
		x,  y1, 0.0f, 1.0f,
		x1, y,  1.0f, 0.0f,
		x1, y1, 1.0f, 1.0f,
		x,  y1, 0.0f, 1.0f
	};

	Shader *shader = st.shader;
	shader->use(true);

	const float proj[16] = {
		2.0f / st.width, 0, 0, 0,
		0, -2.0f / st.height, 0, 0,
		0, 0, 1, 0,
		-1, 1, 0, 1
	};
	GL_CALL(glUniformMatrix4fv(shader->getUniformLocation("u_projection"), 1, GL_FALSE, proj));
	shader->setUniform("u_texture", (unsigned int)0);

	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, st.vbo));
	GL_CALL(glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts));
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, 0));

	const GLsizei stride = 4 * sizeof(float);
	shader->enableVertexAttribute("a_position", st.vbo, 2, GL_FLOAT, GL_FALSE, stride, 0);
	shader->enableVertexAttribute("a_uv", st.vbo, 2, GL_FLOAT, GL_FALSE, stride, 2 * sizeof(float));
	shader->use(true);

	GL_CALL(glActiveTexture(GL_TEXTURE0));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
	GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
}

} // namespace

bool Blitter::init(int width, int height) {
	if (!g_system || !g_system->hasFeature(OSystem::kFeatureOpenGLForGame))
		return false;
	if (OpenGLContext.type == kContextNone || !OpenGLContext.enginesShadersSupported)
		return false;

	BlitState &st = blitState();
	st.width = width;
	st.height = height;

	if (!st.shader) {
		static const char *const attributes[] = { "a_position", "a_uv", nullptr };
		st.shader = Shader::fromStrings("Blitter", kBlitVertexShader, kBlitFragmentShader, attributes, 120);
	}
	if (!st.vbo && st.shader)
		st.vbo = Shader::createBuffer(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_DYNAMIC_DRAW);

	return st.shader != nullptr && st.vbo != 0;
}

void Blitter::shutdown() {
	BlitState &st = blitState();
	for (uint i = 0; i < st.textures.size(); i++)
		GL_CALL(glDeleteTextures(1, &st.textures[i].id));
	st.textures.clear();
	if (st.textTex) {
		GL_CALL(glDeleteTextures(1, &st.textTex));
		st.textTex = 0;
	}
	if (st.vbo) {
		Shader::freeBuffer(st.vbo);
		st.vbo = 0;
	}
	delete st.shader;
	st.shader = nullptr;
	st.width = st.height = 0;
}

void Blitter::clear(byte r, byte g, byte b, byte a) {
	GL_CALL(glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
	GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
}

void Blitter::drawSurface(const Graphics::Surface &s, int x, int y, int w, int h, bool refresh) {
	if (s.w <= 0 || s.h <= 0 || w <= 0 || h <= 0 || !s.getPixels())
		return;
	BlitState &st = blitState();
	if (!st.shader)
		return;

	BlitTexture *found = nullptr;
	for (uint i = 0; i < st.textures.size(); i++) {
		if (st.textures[i].surface == &s) {
			found = &st.textures[i];
			break;
		}
	}

	GLuint tex = 0;
	if (!found) {
		if (st.textures.size() >= kMaxCachedTextures) {
			GL_CALL(glDeleteTextures(1, &st.textures[0].id));
			st.textures.erase(st.textures.begin());
		}
		BlitTexture entry;
		entry.surface = &s;
		entry.w = s.w;
		entry.h = s.h;
		entry.id = createTexture(s);
		st.textures.push_back(entry);
		tex = entry.id;
	} else {
		if (found->w != s.w || found->h != s.h) {
			GL_CALL(glDeleteTextures(1, &found->id));
			found->w = s.w;
			found->h = s.h;
			found->id = createTexture(s);
		} else if (refresh) {
			// Same dimensions: refresh in place without reallocating.
			uploadTexture(found->id, s, false);
		}
		tex = found->id;
	}

	drawTexture(tex, (float)x, (float)y, (float)w, (float)h);
}

void Blitter::drawText(const Graphics::Font &font, const Common::String &text, int x, int y, int maxWidth,
                       uint32 color, Graphics::TextAlign align) {
	if (text.empty() || maxWidth <= 0)
		return;
	BlitState &st = blitState();
	if (!st.shader)
		return;

	const int fontHeight = font.getFontHeight();
	if (fontHeight <= 0)
		return;

	Graphics::Surface tmp;
	tmp.create(maxWidth, fontHeight, Graphics::PixelFormat::createFormatRGBA32());
	tmp.fillRect(Common::Rect(0, 0, maxWidth, fontHeight), 0);
	font.drawString(&tmp, text, 0, 0, maxWidth, color, align);

	if (st.textTex == 0)
		GL_CALL(glGenTextures(1, &st.textTex));
	uploadTexture(st.textTex, tmp, true);
	drawTexture(st.textTex, (float)x, (float)y, (float)maxWidth, (float)fontHeight);

	tmp.free();
}

} // End of namespace OpenGL

#endif // USE_OPENGL_SHADERS
