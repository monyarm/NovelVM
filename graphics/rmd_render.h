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

#ifndef SMT_GRAPHICS_RMD_RENDER_H
#define SMT_GRAPHICS_RMD_RENDER_H

#include "common/formats/rmd.h"
#include "graphics/epl_effect.h"

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
	                  const Common::RMD::MaterialPose *materialPose = nullptr, ///< RMD::evaluateSlotMaterials()
	                  const Common::Array<EffectBatch> *effects = nullptr, ///< model-space effect batches
	                  EffectTextureSource *effectTextures = nullptr);
	static void computeBounds(const Common::RMD &rmd, int width, int height, float rotY,
	                         const Common::RMD::BonePose *bonePose, RMDBounds *outBounds);
	static void invalidate();
	static void shutdown();
};

#endif

} // End of namespace Graphics

#endif // SMT_GRAPHICS_RMD_RENDER_H
