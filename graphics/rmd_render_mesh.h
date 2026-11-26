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

#ifndef GRAPHICS_RMD_RENDER_MESH_H
#define GRAPHICS_RMD_RENDER_MESH_H

#include "graphics/epl_effect.h"
#include "graphics/rmd_render.h"

namespace Graphics {

struct RMDRenderVertex {
	float x, y, z, nx, ny, nz, u, v, r, g, b, a;
};

struct RMDRenderTriangle {
	RMDRenderVertex v[3];
	const Graphics::Surface *texture = nullptr;
	byte horizontalAddressing = 1;
	byte verticalAddressing = 1;
	float diffuse[3] = {1, 1, 1};
	bool alpha = false;
	bool alphaTest = false;
	bool hasUv = false;
};

struct RMDRenderMesh {
	Common::Array<RMDRenderTriangle> triangles;
	Common::Array<EffectBatch> effects; ///< effect batches in the same screen space as the triangles
	RMDBounds bounds;
};

/** Viewer for effects drawn with assembleRMDMesh(rotY): model-space camera position and screen axes. */
EffectCamera rmdEffectCamera(float rotY);

void assembleRMDMesh(const Common::RMD &rmd, const Common::Array<Graphics::Surface *> &textures,
					 int width, int height, float rotY, const Common::RMD::BonePose *pose,
					 const RMDBounds *fixed, RMDRenderMesh &out,
					 const Common::RMD::MaterialPose *materialPose = nullptr,
					 const Common::Array<EffectBatch> *effects = nullptr,
					 bool fitToScreen = true); ///< false: leave vertices in (spun) model space

} // namespace Graphics

#endif // GRAPHICS_RMD_RENDER_MESH_H
