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

#ifndef GRAPHICS_OPENGL_BLITTER_H
#define GRAPHICS_OPENGL_BLITTER_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "graphics/font.h"

namespace Graphics {
struct Surface;
}

#ifdef USE_OPENGL_SHADERS

namespace OpenGL {

/**
 * Generic OpenGL 2D blitter.
 *
 * Draws texture surfaces (decoded video frames, TMX/GMO textures, baked font
 * overlays) as alpha-blended textured quads through a single embedded GLSL
 * program and a shared quad VBO. Requires the backend to be in 3D mode
 * (`initGraphics3d`) so a GL context is current; there is no software
 * fallback.
 *
 * This is the engine-facing equivalent of what ScummVM's 3D engines each do
 * for movie/overlay presentation (e.g. GrimE's `prepareMovieFrame` /
 * `drawMovieFrame`): one texture reused across frames, uploaded once with
 * glTexImage2D and refreshed with glTexSubImage2D. The blitter owns its shader
 * and GPU state and does not share resources with Graphics::GMORenderer. The
 * caller presents the frame via OSystem::updateScreen().
 */
class Blitter {
public:
	/** Create the shader/VBO. Returns false when no usable GL context exists. */
	static bool init(int width, int height);

	/** Release all GL objects. Must be called while the context is alive. */
	static void shutdown();

	/** Clear the current render target to the given RGBA colour. */
	static void clear(byte r, byte g, byte b, byte a = 255);

	/**
	 * Upload @p s (converted to RGBA) and draw it into the rectangle
	 * (x, y, w, h). With @p refresh false a previously uploaded texture is
	 * reused when the surface pointer and dimensions are unchanged; video
	 * frames pass true so every frame is re-uploaded (via glTexSubImage2D,
	 * without reallocating the texture).
	 */
	static void drawSurface(const Graphics::Surface &s, int x, int y, int w, int h, bool refresh = true);

	/**
	 * Bake @p text into a transient RGBA surface with @p font and draw it at
	 * (x, y). @p color is a colour in the baked surface's RGBA8888 format.
	 */
	static void drawText(const Graphics::Font &font, const Common::String &text, int x, int y, int maxWidth,
	                     uint32 color, Graphics::TextAlign align = Graphics::kTextAlignLeft);
};

/** Convert @p src to tightly packed RGBA8888 bytes (row copy when it already is RGBA8888). */
void surfaceToRGBA(const Graphics::Surface &src, Common::Array<byte> &out);

/**
 * Upload @p s as RGBA8888 into GL texture @p id: glTexImage2D when @p allocate, else
 * glTexSubImage2D. Sets nearest filtering and clamp-to-edge wrapping; callers drawing
 * repeated UVs set their own wrap modes after binding.
 */
void uploadTexture(uint id, const Graphics::Surface &s, bool allocate);

} // End of namespace OpenGL

#endif // USE_OPENGL_SHADERS

#endif // GRAPHICS_OPENGL_BLITTER_H
