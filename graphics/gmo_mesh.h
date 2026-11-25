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

#ifndef SMT_GRAPHICS_GMO_MESH_H
#define SMT_GRAPHICS_GMO_MESH_H

#include "common/array.h"
#include "common/formats/gmo.h"
#include "graphics/surface.h"

#include "graphics/gmo_render.h"

namespace Graphics {

// PSP GU enumerations (pspgu.h) used by the GMO material/layer chunks.
// Resolved here so the renderer reads the material state from one place.
enum GuTextureFilter { kGuNearest = 0, kGuLinear = 1 };
enum GuWrap { kGuRepeat = 0, kGuClamp = 1 };
enum GuTexFunc { kGuTfxModulate = 0, kGuTfxDecal = 1, kGuTfxBlend = 2, kGuTfxReplace = 3, kGuTfxAdd = 4 };
enum GuTexComp { kGuTccRgb = 0, kGuTccRgba = 1 };
enum GuBlendOp { kGuAdd = 0, kGuSubtract = 1, kGuReverseSubtract = 2, kGuMin = 3, kGuMax = 4, kGuAbs = 5 };
enum GuBlendFactor {
	kGuOtherColor = 0, kGuOneMinusOtherColor = 1, kGuSrcAlpha = 2, kGuOneMinusSrcAlpha = 3,
	kGuDstAlpha = 4, kGuOneMinusDstAlpha = 5, kGuDoubleSrcAlpha = 6, kGuOneMinusDoubleSrcAlpha = 7,
	kGuDoubleDstAlpha = 8, kGuOneMinusDoubleDstAlpha = 9, kGuFix = 10
};

/**
 * One fully-transformed, projected triangle ready for rasterization.
 *
 * Everything here is backend-agnostic: screen-space coordinates plus view
 * depth, texture coordinates, view-space normals, per-corner vertex color and
 * the resolved GMO material/layer GU state. `tex` points into the surface
 * array owned by the caller.
 */
struct Tri {
	float x[3], y[3], z[3];       // screen coords + depth (smaller = nearer; see the depth mapping in gmo_render.cpp)
	float u[3], v[3];             // texture coords
	float nx[3], ny[3], nz[3];    // view-space normals
	bool hasUv, hasNormal, hasCol;
	float cr[3], cg[3], cb[3], ca[3]; // per-corner vertex color
	const Graphics::Surface *tex; // texture to sample, or null = flat
	// material
	float diffuse[3], ambient[3], emission[3], specular[3];
	float alpha;                  // material (diffuse) alpha
	float specPower, reflection, refraction, bump, mapFactor;
	// layer
	uint32 texFunc, texComp, texFilterMin, texFilterMag, wrapU, wrapV, mapType, texGen;
	uint32 blendMode, blendSrc, blendDst;
	bool blendEnabled;
	uint32 transAlgo;
	byte r, g, b;                 // flat fallback
};

/**
 * Decode every embedded texture the model references into RGBA-ish surfaces.
 * Invalid/empty textures are skipped, exactly like the historical inline
 * loop, so the resulting indices match the `texSurfaces[texIdx]` lookup.
 * The returned surfaces are owned by the caller.
 */
void decodeGMOTextures(const Common::GMO &gmo, Common::Array<Graphics::Surface *> &out);

/** Free the surfaces produced by decodeGMOTextures(). */
void freeGMOTextures(Common::Array<Graphics::Surface *> &textures);

/**
 * Assemble the shared triangle list for `gmo`.
 *
 * This performs the per-bone transform chain, optional linear-blend skinning,
 * world spin and view orientation, auto-framing (or caller-supplied fixed bounds),
 * mesh primitive expansion and texture-coordinate derivation, then stable
 * sorts the triangles by `transAlgo` (material render order). It is a pure
 * function of its inputs; `textures` is only referenced through Tri::tex.
 *
 * `width`/`height` are the render-target dimensions (used for framing and
 * projection). On success `outTris` holds the sorted triangles; it is cleared
 * first so callers can reuse one array across frames.
 */
void buildGMOMesh(const Common::GMO &gmo, const Common::Array<Graphics::Surface *> &textures,
                  int width, int height, float rotY,
                  const Common::GMO::BonePose *bonePose, const Common::GMO::MaterialPose *materialPose,
                  const GMOBounds *fixedBounds, GMOBounds *outBounds,
                  Common::Array<Tri> &outTris);

} // End of namespace Graphics

#endif // SMT_GRAPHICS_GMO_MESH_H
