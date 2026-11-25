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

#include "graphics/rmd_render_mesh.h"

#include <cmath>

#include "common/array.h"

namespace Graphics {
namespace {

Common::RMD::Matrix multiply(const Common::RMD::Matrix &a, const Common::RMD::Matrix &b) {
	Common::RMD::Matrix out = {};
	for (int i = 0; i < 16; ++i)
		out.m[i] = 0;
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			for (int i = 0; i < 4; ++i)
				out.m[row * 4 + col] += a.m[row * 4 + i] * b.m[i * 4 + col];
	return out;
}

Common::RMD::Matrix frameMatrix(const Common::RMD::Frame &frame) {
	Common::RMD::Matrix out = {};
	for (int row = 0; row < 3; ++row)
		for (int col = 0; col < 3; ++col)
			out.m[row * 4 + col] = frame.transform[row * 3 + col];
	out.m[12] = frame.transform[9];
	out.m[13] = frame.transform[10];
	out.m[14] = frame.transform[11];
	out.m[15] = 1.0f;
	return out;
}

void transform(const Common::RMD::Matrix &m, const Common::RMD::Vec3 &in, float &x, float &y, float &z) {
	x = in.x * m.m[0] + in.y * m.m[4] + in.z * m.m[8] + m.m[12];
	y = in.x * m.m[1] + in.y * m.m[5] + in.z * m.m[9] + m.m[13];
	z = in.x * m.m[2] + in.y * m.m[6] + in.z * m.m[10] + m.m[14];
}

int textureIndex(const Common::RMD &rmd, const Common::RMD::Material &material) {
	if (material.textureName.empty())
		return -1;
	for (uint i = 0; i < rmd.textures().size(); ++i)
		if (rmd.textures()[i].name == material.textureName)
			return i;
	return -1;
}

} // namespace

void assembleRMDMesh(const Common::RMD &rmd, const Common::Array<Graphics::Surface *> &textures,
					 int width, int height, float rotY, const Common::RMD::BonePose *pose,
					 const RMDBounds *fixed, RMDRenderMesh &out, const Common::RMD::MaterialPose *materialPose,
					 const Common::Array<EffectBatch> *effects, bool fitToScreen) {
	const Common::Array<float> *uvTransforms = materialPose ? &materialPose->uvTransforms : nullptr;
	out.triangles.clear();
	out.bounds = RMDBounds();
	if (width <= 0 || height <= 0)
		return;
	Common::Array<Common::RMD::Matrix> bind, bindWorld, world;
	bind.resize(MAX((uint)1, rmd.frames().size()));
	bindWorld.resize(bind.size());
	world.resize(bind.size());
	for (uint i = 0; i < rmd.frames().size(); ++i) {
		bind[i] = frameMatrix(rmd.frames()[i]);
		bindWorld[i] = rmd.frames()[i].parent >= 0 ? multiply(bind[i], bindWorld[rmd.frames()[i].parent]) : bind[i];
		world[i] = pose && i < pose->worldTransforms.size() ? pose->worldTransforms[i] : bindWorld[i];
	}
	Common::RMD::Matrix spin = {};
	spin.m[0] = cosf(rotY);
	spin.m[2] = sinf(rotY);
	spin.m[8] = -sinf(rotY);
	spin.m[10] = cosf(rotY);
	spin.m[5] = 1.0f;
	spin.m[15] = 1.0f;
	float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
	float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
	if (fixed && fixed->valid) {
		minX = fixed->minX; maxX = fixed->maxX;
		minY = fixed->minY; maxY = fixed->maxY;
		minZ = fixed->minZ; maxZ = fixed->maxZ;
	}

	// Real PS2 cutout textures (fences, foliage, etc.) carry their own
	// per-pixel alpha independent of the material's flat color/alpha, e.g.
	// N23801.RMD's cage texture is opaque-white at the material level but
	// has real fully-transparent texels for the gaps between bars. Require
	// a meaningful fraction of fully-transparent pixels (not just a stray
	// quantization/edge artifact) before treating a texture as a cutout
	// mask -- this is alpha *test* territory (binary keep/discard), not
	// smooth alpha blending, so it's handled as RMDRenderTriangle::alphaTest.
	Common::Array<bool> textureIsCutout(textures.size(), false);
	for (uint t = 0; t < textures.size(); ++t) {
		if (!textures[t])
			continue;
		uint32 transparent = 0, total = (uint32)textures[t]->w * textures[t]->h;
		for (int y = 0; y < textures[t]->h; ++y)
			for (int x = 0; x < textures[t]->w; ++x) {
				uint32 packed = 0;
				memcpy(&packed, textures[t]->getBasePtr(x, y), textures[t]->format.bytesPerPixel);
				byte r, g, b, a;
				textures[t]->format.colorToARGB(packed, a, r, g, b);
				if (a == 0) ++transparent;
			}
		if (total > 0 && transparent * 100 >= total)
			textureIsCutout[t] = true;
	}

	Common::Array<uint32> atomics;
	for (uint c = 0; c < rmd.clumps().size(); ++c)
		for (uint a = 0; a < rmd.clumps()[c].atomics.size(); ++a)
			atomics.push_back(rmd.clumps()[c].atomics[a]);
	if (atomics.empty())
		for (uint a = 0; a < rmd.atomics().size(); ++a)
			atomics.push_back(a);
	for (uint ai = 0; ai < atomics.size(); ++ai) {
		if (atomics[ai] >= rmd.atomics().size())
			continue;
		const Common::RMD::Atomic &atomic = rmd.atomics()[atomics[ai]];
		if (atomic.geometry < 0 || (uint)atomic.geometry >= rmd.geometries().size())
			continue;
		const Common::RMD::Geometry &g = rmd.geometries()[atomic.geometry];
		Common::RMD::Matrix owner;
		if (atomic.frame >= 0 && (uint)atomic.frame < world.size())
			owner = world[atomic.frame];
		for (uint i = 0; i + 2 < g.indices.size(); i += 3) {
			uint ids[3] = {g.indices[i], g.indices[i + 1], g.indices[i + 2]};
			if (ids[0] >= g.positions.size() || ids[1] >= g.positions.size() || ids[2] >= g.positions.size())
				continue;
			RMDRenderTriangle tri;
			uint materialIndex = i / 3 < g.materialIndices.size() ? g.materialIndices[i / 3] : 0;
			const Common::RMD::Material *mat = materialIndex < rmd.materials().size() ? &rmd.materials()[materialIndex] : nullptr;
			// Material transparency animation: fully transparent materials draw nothing (and write no depth).
			const float opacity = materialPose && materialIndex < materialPose->opacity.size() ? materialPose->opacity[materialIndex] : 1.0f;
			if (opacity <= 0.0f)
				continue;
			int tex = mat ? textureIndex(rmd, *mat) : -1;
			tri.texture = tex >= 0 && (uint)tex < textures.size() ? textures[tex] : nullptr;
			if (tex >= 0 && (uint)tex < rmd.textures().size()) {
				tri.horizontalAddressing = rmd.textures()[tex].horizontalAddressing;
				tri.verticalAddressing = rmd.textures()[tex].verticalAddressing;
			}
			tri.alpha = (mat && (mat->alpha || mat->color.a != 255)) || opacity < 1.0f;
			tri.alphaTest = tex >= 0 && (uint)tex < textureIsCutout.size() && textureIsCutout[tex];
			if (mat) {
				const float factor = mat->diffuse > 0.0f ? mat->diffuse : 1.0f;
				// material colour, or its animated value (RMD::MaterialPose::color)
				const bool animated = materialPose && (materialIndex + 1) * 3 <= materialPose->color.size();
				for (int c = 0; c < 3; ++c) {
					const float base = animated ? materialPose->color[materialIndex * 3 + c]
						: (c == 0 ? mat->color.r : c == 1 ? mat->color.g : mat->color.b) / 255.0f;
					tri.diffuse[c] = MIN(1.0f, base * factor);
				}
			}
			for (int k = 0; k < 3; ++k) {
				Common::RMD::Vec3 p = g.positions[ids[k]];
				float x, y, z;
				if (g.skinned && ids[k] < g.skin.influences.size()) {
					const Common::RMD::SkinInfluence &inf = g.skin.influences[ids[k]];
					bool validInfluence = true;
					x = y = z = 0;
					for (int w = 0; w < 4; ++w) {
						if (inf.weights[w] < 0 || !std::isfinite(inf.weights[w]) || inf.indices[w] >= g.skin.inverseBindMatrices.size() ||
								inf.indices[w] >= g.skin.rendererBoneFrames.size()) {
							validInfluence = false;
							break;
						}
						uint frame = g.skin.rendererBoneFrames[inf.indices[w]];
						if (frame >= world.size()) {
							validInfluence = false;
							break;
						}
						// The file's own inverse-bind matrix is already fully composed (world-space
						// mesh-to-bone), unlike a matrix we'd derive from the frame hierarchy's
						// local transforms -- deriving our own from bindWorld[frame] silently
						// dropped whatever real-world skinning correction the original authoring
						// tool baked in here, causing severe stretching at bones lacking an
						// explicit compensating scale elsewhere in their ancestor chain (confirmed:
						// swapping in the stored matrix eliminates the stretch entirely, verified
						// visually across multiple previously-broken NPC files with zero regression
						// on previously-clean ones).
						Common::RMD::Matrix skin = multiply(g.skin.inverseBindMatrices[inf.indices[w]], world[frame]);
						float tx, ty, tz; transform(skin, p, tx, ty, tz);
						x += tx * inf.weights[w]; y += ty * inf.weights[w]; z += tz * inf.weights[w];
					}
					const float weightSum = inf.weights[0] + inf.weights[1] + inf.weights[2] + inf.weights[3];
					if (validInfluence && std::isfinite(weightSum) && weightSum > 0) {
						p = {x, y, z};
					} else {
						x = p.x; y = p.y; z = p.z;
					}
					// Skin matrices are serialized relative to the atomic frame.
				} else {
					transform(owner, p, x, y, z);
				}
				Common::RMD::Vec3 wp = {x, y, z};
				transform(spin, wp, x, y, z);
				if (!(fixed && fixed->valid)) {
					minX = MIN(minX, x); maxX = MAX(maxX, x);
					minY = MIN(minY, y); maxY = MAX(maxY, y);
					minZ = MIN(minZ, z); maxZ = MAX(maxZ, z);
				}
				tri.v[k].x = x; tri.v[k].y = y; tri.v[k].z = z;
				if (g.uvs.size() > ids[k]) {
					tri.hasUv = true;
					const float u = g.uvs[ids[k]].u, v = g.uvs[ids[k]].v;
					// per-material UV animation (RMD::evaluateSlotMaterials): u' = m0 u + m2 v + m4
					if (uvTransforms && (materialIndex + 1) * 6 <= uvTransforms->size()) {
						const float *m = &(*uvTransforms)[materialIndex * 6];
						tri.v[k].u = m[0] * u + m[2] * v + m[4];
						tri.v[k].v = m[1] * u + m[3] * v + m[5];
					} else {
						tri.v[k].u = u;
						tri.v[k].v = v;
					}
				}
				if (g.colors.size() > ids[k]) { tri.v[k].r = g.colors[ids[k]].r / 255.0f; tri.v[k].g = g.colors[ids[k]].g / 255.0f; tri.v[k].b = g.colors[ids[k]].b / 255.0f; tri.v[k].a = g.colors[ids[k]].a / 255.0f; } else tri.v[k].r = tri.v[k].g = tri.v[k].b = tri.v[k].a = 1;
					if (mat) tri.v[k].a *= mat->color.a / 255.0f;
					tri.v[k].a *= opacity;
			}
			Common::RMD::Vec3 ab = {tri.v[1].x - tri.v[0].x, tri.v[1].y - tri.v[0].y, tri.v[1].z - tri.v[0].z};
			Common::RMD::Vec3 ac = {tri.v[2].x - tri.v[0].x, tri.v[2].y - tri.v[0].y, tri.v[2].z - tri.v[0].z};
			float nx = ab.y * ac.z - ab.z * ac.y, ny = ab.z * ac.x - ab.x * ac.z, nz = ab.x * ac.y - ab.y * ac.x;
			float length = sqrtf(nx * nx + ny * ny + nz * nz);
			if (length > 0.000001f) { nx /= length; ny /= length; nz /= length; } else { nx = 0; ny = 0; nz = 1; }
			for (int k = 0; k < 3; ++k) { tri.v[k].nx = nx; tri.v[k].ny = ny; tri.v[k].nz = nz; }
			out.triangles.push_back(tri);
		}
	}
	for (uint i = 1; i < out.triangles.size(); ++i) { RMDRenderTriangle key = out.triangles[i]; int j = i - 1; while (j >= 0 && out.triangles[j].alpha > key.alpha) { out.triangles[j + 1] = out.triangles[j]; --j; } out.triangles[j + 1] = key; }
	if (minX > maxX)
		return;
	out.bounds.minX = minX; out.bounds.maxX = maxX; out.bounds.minY = minY; out.bounds.maxY = maxY; out.bounds.minZ = minZ; out.bounds.maxZ = maxZ; out.bounds.valid = true;
	if (!fitToScreen)
		return;
	float cx = (minX + maxX) * 0.5f, cy = (minY + maxY) * 0.5f;
	float spanX = MAX(maxX - minX, maxZ - minZ), spanY = maxY - minY;
	float scale = MIN(height * 0.85f / MAX(spanY, 0.0001f), width * 0.95f / MAX(spanX, 0.0001f));
	for (uint i = 0; i < out.triangles.size(); ++i)
		for (int k = 0; k < 3; ++k) { out.triangles[i].v[k].x = width * 0.5f + (out.triangles[i].v[k].x - cx) * scale; out.triangles[i].v[k].y = height * 0.5f - (out.triangles[i].v[k].y - cy) * scale; }
	// Effects live in model space: same spin and fit, without widening the model's bounds.
	if (effects) {
		out.effects = *effects;
		for (uint b = 0; b < out.effects.size(); ++b) {
			Common::Array<EffectVertex> &vs = out.effects[b].vertices;
			if (!out.effects[b].cullBack)
				continue;
			// The viewer looks along +z after the spin: a front face's normal has z < 0.
			Common::Array<EffectVertex> kept;
			for (uint i = 0; i + 2 < vs.size(); i += 3) {
				float p[3][3];
				for (int k = 0; k < 3; ++k) {
					const Common::RMD::Vec3 in = {vs[i + k].x, vs[i + k].y, vs[i + k].z};
					transform(spin, in, p[k][0], p[k][1], p[k][2]);
				}
				const float ax = p[1][0] - p[0][0], ay = p[1][1] - p[0][1], bx = p[2][0] - p[0][0], by = p[2][1] - p[0][1];
				if (ax * by - ay * bx <= 0)
					for (int k = 0; k < 3; ++k)
						kept.push_back(vs[i + k]);
			}
			vs = kept;
		}
		for (uint b = 0; b < out.effects.size(); ++b)
			for (uint i = 0; i < out.effects[b].vertices.size(); ++i) {
				EffectVertex &v = out.effects[b].vertices[i];
				if (out.effects[b].screenSpace) {
					v.x *= width;
					v.y *= height;
					v.z = minZ; // inside the depth range; screen-space batches don't depth test
					continue;
				}
				if (out.effects[b].anchored) {
					const float *a = out.effects[b].anchor;
					const Common::RMD::Vec3 p = {a[0], a[1], a[2]};
					float x, y, z;
					transform(spin, p, x, y, z);
					v.x = width * 0.5f + (x - cx) * scale + v.x * width / 640.0f;
					v.y = height * 0.5f - (y - cy) * scale + v.y * height / 448.0f;
					v.z = minZ;
					continue;
				}
				const Common::RMD::Vec3 p = {v.x, v.y, v.z};
				float x, y, z;
				transform(spin, p, x, y, z);
				v.x = width * 0.5f + (x - cx) * scale;
				v.y = height * 0.5f - (y - cy) * scale;
				v.z = z;
			}
	}
}

EffectCamera rmdEffectCamera(float rotY) {
	// The spin maps x' = x cos - z sin, z' = x sin + z cos; screen x is x', screen up is y and
	// smaller z' is nearer. Invert that for the screen axes and a distant viewer.
	EffectCamera camera;
	const float c = cosf(rotY), s = sinf(rotY), distance = 100000.0f;
	camera.right[0] = c; camera.right[1] = 0; camera.right[2] = -s;
	camera.up[0] = 0; camera.up[1] = 1; camera.up[2] = 0;
	camera.position[0] = -s * distance; camera.position[1] = 0; camera.position[2] = -c * distance;
	return camera;
}

} // namespace Graphics
