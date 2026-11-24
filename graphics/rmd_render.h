#ifndef SMT_GRAPHICS_RMD_RENDER_H
#define SMT_GRAPHICS_RMD_RENDER_H

#include "common/formats/rmd.h"

namespace Graphics {

struct RMDBounds {
	float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool valid = false;
};

#if defined(USE_OPENGL_SHADERS) || defined(RMD_RENDER_COMPILE_CHECK)

class RMDRenderer {
public:
	static bool isAvailable();
	static void render(const Common::RMD &rmd, int width, int height, float rotY = 0.0f,
	                  const Common::RMD::BonePose *bonePose = nullptr,
	                  const RMDBounds *fixedBounds = nullptr,
	                  RMDBounds *outBounds = nullptr, bool unlit = false,
	                  const Common::RMD::MaterialPose *materialPose = nullptr); ///< RMD::evaluateSlotMaterials()
	static void computeBounds(const Common::RMD &rmd, int width, int height, float rotY,
	                         const Common::RMD::BonePose *bonePose, RMDBounds *outBounds);
	static void invalidate();
	static void shutdown();
};

#endif

} // End of namespace Graphics

#endif // SMT_GRAPHICS_RMD_RENDER_H
