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

#ifndef SMT_GRAPHICS_GMO_RENDER_H
#define SMT_GRAPHICS_GMO_RENDER_H

#include "common/formats/gmo.h"
#include "graphics/surface.h"

namespace Graphics {

/**
 * World-space bounds used for auto-framing. Z is kept so the framing can stay
 * stable while the model spins about Y (the larger of the X/Z extents is used
 * as the horizontal span). Shared by the mesh builder (gmo_mesh.h) and the
 * renderer.
 */
struct GMOBounds {
	float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool valid = false;
};

#ifdef USE_OPENGL_SHADERS

/**
 * Hardware GMO renderer: builds the shared triangle list (gmo_mesh.h) and
 * draws it with an embedded GLSL program. There is deliberately no software
 * fallback -- the engines that use this are OpenGL-only.
 *
 * The vertex shader receives the already-projected screen-space triangles and
 * an orthographic projection; the fragment shader implements the GMO material
 * and PSP GU texture functions (MODULATE/DECAL/BLEND/REPLACE/ADD) with vertex
 * color and diffuse. Triangles arrive in the mesh builder's `transAlgo`
 * painter's order, so no depth buffer is needed.
 *
 * The caller must have switched the backend into 3D mode (`initGraphics3d`)
 * so a GL context is current and the render target is the 3D framebuffer;
 * `width`/`height` describe that target.
 */
class GMORenderer {
public:
	/** Whether a usable OpenGL shader context is present. */
	static bool isAvailable();

	/**
	 * Render `gmo` into the currently bound GL render target, sized
	 * width x height. `rotY` rotates the model about world Y (radians);
	 * `fixedBounds` keeps the
	 * framing constant; `outBounds` receives the computed world bounds.
	 */
	static void render(const Common::GMO &gmo, int width, int height, float rotY = 0.0f,
	                   const Common::GMO::BonePose *bonePose = nullptr,
	                   const Common::GMO::MaterialPose *materialPose = nullptr,
	                   const GMOBounds *fixedBounds = nullptr,
	                   GMOBounds *outBounds = nullptr,
	                   bool unlit = false);

	/**
	 * Compute just the world-space bounds for the given pose without drawing,
	 * so callers can fix the camera before the first GL frame.
	 */
	static void computeBounds(const Common::GMO &gmo, int width, int height, float rotY,
	                          const Common::GMO::BonePose *bonePose,
	                          const Common::GMO::MaterialPose *materialPose, GMOBounds *outBounds);

	/** Drop cached textures before the caller replaces the GMO object. */
	static void invalidate();

	/** Release all GL objects. Must be called while the context is alive. */
	static void shutdown();
};

#endif // USE_OPENGL_SHADERS

} // End of namespace Graphics

#endif // SMT_GRAPHICS_GMO_RENDER_H
