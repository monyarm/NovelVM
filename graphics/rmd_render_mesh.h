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
