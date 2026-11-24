#ifndef GRAPHICS_RMD_RENDER_MESH_H
#define GRAPHICS_RMD_RENDER_MESH_H

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
	RMDBounds bounds;
};

void assembleRMDMesh(const Common::RMD &rmd, const Common::Array<Graphics::Surface *> &textures,
					 int width, int height, float rotY, const Common::RMD::BonePose *pose,
					 const RMDBounds *fixed, RMDRenderMesh &out,
					 const Common::Array<float> *uvTransforms = nullptr);

} // namespace Graphics

#endif // GRAPHICS_RMD_RENDER_MESH_H
