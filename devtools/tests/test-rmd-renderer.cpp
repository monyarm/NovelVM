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

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "common/memstream.h"
#define private public
#include "common/formats/rmd.h"
#undef private
#include "graphics/rmd_render_mesh.h"

static bool near(float actual, float expected) {
	return fabsf(actual - expected) < 0.0001f;
}

static uint32 readDump32(const Common::Array<byte> &data, uint32 offset) {
	return (uint32)data[offset] | ((uint32)data[offset + 1] << 8) |
		((uint32)data[offset + 2] << 16) | ((uint32)data[offset + 3] << 24);
}

static void dumpSurface(const Graphics::Surface &surface, const char *path) {
	std::ofstream output(path, std::ios::binary);
	for (int y = 0; y < surface.h; ++y)
		for (int x = 0; x < surface.w; ++x) {
			uint32 packed = 0;
			memcpy(&packed, surface.getBasePtr(x, y), surface.format.bytesPerPixel);
			byte r, g, b;
			surface.format.colorToRGB(packed, r, g, b);
			output.put((char)r); output.put((char)g); output.put((char)b);
			output.put((char)(surface.format.aLoss == 8 ? 255 : (byte)(((packed >> surface.format.aShift) << surface.format.aLoss) & 255)));
		}
}

static int dumpRMD(const char *path, const char *textureDirectory = nullptr,
		const char *meshDumpPath = nullptr, bool strictSelfCheck = true) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		return 2;
	const std::streamsize size = input.tellg();
	input.seekg(0);
	std::vector<byte> data((size_t)size);
	if (size && !input.read((char *)data.data(), size))
		return 3;
	Common::MemoryReadStream stream(data.data(), (uint32)data.size(), DisposeAfterUse::NO);
	Common::RMD rmd(&stream);
	std::cout << "bytes=" << data.size() << " ok=" << (rmd.ok() ? 1 : 0)
			  << " frames=" << rmd.frames().size() << " hierarchy=" << rmd._hierarchy.size()
			  << " geometries=" << rmd.geometries().size() << " materials=" << rmd.materials().size()
			  << " textures=" << rmd.textures().size() << " animations=" << rmd.animations().size() << "\n";
	for (uint i = 0; i < rmd.frames().size(); ++i) {
		const Common::RMD::Frame &f = rmd.frames()[i];
		std::cout << "frame=" << i << " parent=" << f.parent << " nameId=" << f.nameId << " name=" << f.name.c_str()
				  << " bindT=(" << f.transform[9] << "," << f.transform[10] << "," << f.transform[11] << ")\n";
	}
	for (uint i = 0; i < rmd._hierarchy.size(); ++i)
		std::cout << "hanim=" << i << " nodeId=" << rmd._hierarchy[i].nodeId
				  << " index=" << rmd._hierarchy[i].index << " flags=" << rmd._hierarchy[i].flags << "\n";
	for (uint i = 0; i < rmd.atomics().size(); ++i)
		std::cout << "atomic=" << i << " frame=" << rmd.atomics()[i].frame << " geometry=" << rmd.atomics()[i].geometry << "\n";
	for (uint a = 0; a < rmd.animations().size(); ++a) {
		const Common::RMD::Animation &anim = rmd.animations()[a];
		std::cout << "anim=" << a << " duration=" << anim.duration << " supported=" << anim.supported
				  << " compressed=" << anim.compressed
				  << " tracks=" << anim.tracks.size() << " reason=" << anim.unsupportedReason.c_str() << "\n";
		for (uint t = 0; t < anim.tracks.size() && t < 5; ++t) {
			int32 frame = rmd.frameForHierarchy(anim.tracks[t].bone);
			std::cout << "  track=" << t << " bone=" << anim.tracks[t].bone << " ->frame=" << frame
					  << " keys=" << anim.tracks[t].keys.size() << "\n";
			for (uint k = 0; k < anim.tracks[t].keys.size() && k < 3; ++k) {
				const Common::RMD::AnimationKey &key = anim.tracks[t].keys[k];
				std::cout << "    key=" << k << " time=" << key.time
						  << " t=(" << key.translation.x << "," << key.translation.y << "," << key.translation.z << ")"
						  << " q=(" << key.rotation.x << "," << key.rotation.y << "," << key.rotation.z << "," << key.rotation.w << ")\n";
			}
		}
	}
	for (uint i = 0; i < rmd.geometries().size(); ++i) {
		const Common::RMD::Geometry &g = rmd.geometries()[i];
		std::cout << "geometry=" << i << " vertices=" << g.positions.size() << " triangles=" << g.declaredTriangleCount
				  << " materials=" << g.declaredMaterialCount << " indices=" << g.indices.size()
				  << " materialIndices=" << g.materialIndices.size() << " strips=" << (g.triangleStrip ? 1 : 0)
				  << " skinned=" << (g.skinned ? 1 : 0) << "\n";
		if (g.materialMap.size()) {
			std::cout << "materialMap=";
			for (uint j = 0; j < g.materialMap.size(); ++j)
				std::cout << (j ? "," : "") << g.materialMap[j];
			std::cout << "\n";
		}
		if (i == 1 && g.uvs.size()) {
			float minU = g.uvs[0].u, maxU = g.uvs[0].u, minV = g.uvs[0].v, maxV = g.uvs[0].v;
			for (uint j = 1; j < g.uvs.size(); ++j) {
				minU = MIN(minU, g.uvs[j].u); maxU = MAX(maxU, g.uvs[j].u);
				minV = MIN(minV, g.uvs[j].v); maxV = MAX(maxV, g.uvs[j].v);
			}
			std::cout << "uvRange=" << minU << "," << maxU << " " << minV << "," << maxV << "\n";
			for (uint j = 1; j < g.materialIndices.size(); ++j)
				if (g.materialIndices[j] != g.materialIndices[j - 1] &&
						(g.materialIndices[j] == 4 || g.materialIndices[j - 1] == 4))
					std::cout << "uvMaterialBoundary=" << j << " material=" << g.materialIndices[j - 1] << "->" << g.materialIndices[j]
							<< " tri0=" << g.indices[j * 3] << "," << g.indices[j * 3 + 1] << "," << g.indices[j * 3 + 2]
							<< " uv0=" << g.uvs[g.indices[j * 3]].u << "," << g.uvs[g.indices[j * 3]].v << "\n";
		}
		if (i == 1 && g.uvs.size() >= 3) {
			for (uint j = 0; j < 3; ++j)
				std::cout << "uv=" << j << " " << g.uvs[j].u << "," << g.uvs[j].v << "\n";
		}
		if (g.materialIndices.size()) {
			Common::Array<uint32> counts(rmd.materials().size(), 0);
			for (uint j = 0; j < g.materialIndices.size(); ++j)
				if (g.materialIndices[j] < counts.size())
					++counts[g.materialIndices[j]];
			std::cout << "materialTriangles=";
			for (uint j = 0; j < counts.size(); ++j)
				if (counts[j])
					std::cout << (j ? "," : "") << j << ":" << counts[j];
			std::cout << "\n";
		}
		if (g.skinned) {
			std::cout << "skin bones=" << g.skin.boneCount << " used=" << g.skin.usedBoneCount
					  << " weights=" << g.skin.maxWeightsPerVertex << "\n";
			std::cout << "usedbones=";
			for (uint j = 0; j < g.skin.usedBones.size() && j < 50; ++j)
				std::cout << (j ? "," : "") << (uint)g.skin.usedBones[j];
			std::cout << "\n";
			for (uint j = 0; j < g.skin.influences.size() && j < 3; ++j)
				std::cout << "influence=" << j << " indices=" << (uint)g.skin.influences[j].indices[0] << "," << (uint)g.skin.influences[j].indices[1]
						  << " weights=" << g.skin.influences[j].weights[0] << "," << g.skin.influences[j].weights[1] << "\n";
			for (uint j = 0; j < g.skin.rendererBoneFrames.size() && j < 8; ++j)
				std::cout << "skinframe=" << j << " frame=" << g.skin.rendererBoneFrames[j] << "\n";
			for (uint j = 0; j < g.skin.inverseBindMatrices.size() && j < 4; ++j) {
				const Common::RMD::Matrix &m = g.skin.inverseBindMatrices[j];
				std::cout << "skinmat=" << j;
				for (int k = 0; k < 16; ++k)
					std::cout << " " << m.m[k];
				std::cout << "\n";
			}
		}
	}
	for (uint32 offset = 0; offset + 20 <= rmd._data.size(); ++offset) {
		if (readDump32(rmd._data, offset) != 0x50E || readDump32(rmd._data, offset + 8) != 0x1C020037)
			continue;
		const uint32 nodeSize = readDump32(rmd._data, offset + 4);
		if (nodeSize < 12 || nodeSize > rmd._data.size() - offset - 12)
			continue;
		const uint32 nodeEnd = offset + 12 + nodeSize;
		uint32 p = offset + 12;
		const uint32 splitCount = readDump32(rmd._data, p + 4);
		std::cout << "meshList primitive=" << readDump32(rmd._data, p) << " splits=" << splitCount
				  << " faces=" << readDump32(rmd._data, p + 8);
		p += 12;
		for (uint32 split = 0; split < splitCount && p + 8 <= nodeEnd; ++split) {
			const uint32 indexCount = readDump32(rmd._data, p);
			const uint32 material = readDump32(rmd._data, p + 4);
			std::cout << " split" << split << "=" << indexCount << ":" << material;
			p += 8 + indexCount * 4;
		}
		std::cout << "\n";
	}
	for (uint i = 0; i < rmd.materials().size(); ++i)
		std::cout << "material=" << i << " texture=" << rmd.materials()[i].textureName.c_str()
				  << " mask=" << rmd.materials()[i].maskName.c_str() << " rgba="
				  << (uint)rmd.materials()[i].color.r << "," << (uint)rmd.materials()[i].color.g << ","
				  << (uint)rmd.materials()[i].color.b << "," << (uint)rmd.materials()[i].color.a << "\n";
	for (uint i = 0; i < rmd.textures().size(); ++i)
		std::cout << "texture=" << i << " name=" << rmd.textures()[i].name.c_str()
				  << " mask=" << rmd.textures()[i].maskName.c_str() << " bytes=" << rmd.textures()[i].nativeData.size() << "\n";
	bool textureFailure = false;
	for (uint i = 0; i < rmd.textures().size(); ++i) {
		Graphics::Surface *surface = rmd.getTextureSurface(i);
		textureFailure = textureFailure || !surface;
		std::cout << "textureDecode=" << i << " ok=" << (surface ? 1 : 0);
		if (surface) {
			std::cout << " size=" << surface->w << "x" << surface->h;
			if (textureDirectory) {
				Common::String texturePath = Common::String::format("%s/texture-%u.rgba", textureDirectory, i);
				dumpSurface(*surface, texturePath.c_str());
			}
			if (i == 1) {
				const byte *pixel = (const byte *)surface->getBasePtr(0, 0);
				uint32 packed = 0;
				memcpy(&packed, pixel, surface->format.bytesPerPixel);
				byte red, green, blue;
				surface->format.colorToRGB(packed, red, green, blue);
				std::cout << "texturePixel=1 " << (uint)red << "," << (uint)green << "," << (uint)blue << "," << (uint)pixel[3] << "\n";
				if (strictSelfCheck && (red != 193 || green != 160 || blue != 127 || pixel[3] != 255))
					return 9;
			}
			surface->free();
			delete surface;
		}
		std::cout << "\n";
	}
	if (meshDumpPath) {
		Common::Array<Graphics::Surface *> textures;
		textures.resize(rmd.textures().size());
		for (uint ti = 0; ti < textures.size(); ++ti)
			textures[ti] = rmd.getTextureSurface(ti);
		Graphics::RMDRenderMesh mesh;
		const float rotY = (float)M_PI;
		Graphics::assembleRMDMesh(rmd, textures, 100, 100, rotY, nullptr, nullptr, mesh);
		std::cout << "meshBounds valid=" << mesh.bounds.valid << " x=" << mesh.bounds.minX << ".." << mesh.bounds.maxX
				  << " y=" << mesh.bounds.minY << ".." << mesh.bounds.maxY << " z=" << mesh.bounds.minZ << ".." << mesh.bounds.maxZ << "\n";
		std::ofstream dump(meshDumpPath);
		dump << "basePose rotY=" << rotY << " width=100 height=100 animation=disabled triangles=" << mesh.triangles.size() << "\n";
		for (uint j = 0; j < mesh.triangles.size(); ++j) {
			const Graphics::RMDRenderTriangle &tri = mesh.triangles[j];
			dump << "renderTri=" << j << " texture=";
			for (uint ti = 0; ti < textures.size(); ++ti) if (textures[ti] == tri.texture) { dump << rmd.textures()[ti].name.c_str(); break; }
			dump << " alpha=" << (tri.alpha ? 1 : 0) << " hasUv=" << (tri.hasUv ? 1 : 0);
			for (int k = 0; k < 3; ++k)
				dump << " v" << k << "=" << tri.v[k].x << "," << tri.v[k].y << "," << tri.v[k].z << "," << tri.v[k].u << "," << tri.v[k].v << "," << tri.v[k].a;
			dump << "\n";
		}
	}
	if (rmd.geometries().size() == 2 && rmd.geometries()[1].positions.size() == 2136) {
		const Common::RMD::Geometry &g = rmd.geometries()[1];
		if (g.declaredTriangleCount != 2010 || g.declaredMaterialCount != 9 ||
				g.indices.size() != 4478 * 3 || g.materialIndices.size() != 4478)
			return 6;
		if (g.skin.inverseBindMatrices.empty() || !near(g.skin.inverseBindMatrices[0].m[1], 1.0f) ||
				!near(g.skin.inverseBindMatrices[0].m[4], 1.0f) ||
				!near(g.skin.inverseBindMatrices[0].m[10], -0.9999997f))
			return 11;
		if (g.materialIndices[0] != 1 || g.materialIndices[1192] != 1 ||
				g.materialIndices[1193] != 2 || g.materialIndices[1571] != 2 ||
				g.materialIndices[1572] != 3 || g.materialIndices[1627] != 3 ||
				g.materialIndices[1628] != 4 || g.materialIndices[2442] != 4 ||
				g.materialIndices[2443] != 5 || g.materialIndices[2481] != 5 ||
				g.materialIndices[2482] != 6 || g.materialIndices[3178] != 6 ||
				g.materialIndices[3179] != 7 || g.materialIndices[4329] != 7 ||
				g.materialIndices[4330] != 8 || g.materialIndices[4431] != 8 ||
				g.materialIndices[4432] != 9)
			return 7;
		if (g.uvs.size() < 3 || !near(g.uvs[0].u, 0.857629f) || !near(g.uvs[0].v, 0.327866f) ||
				rmd.materials().size() <= 4 || rmd.materials()[4].textureName != "bc002_face_t02" ||
				rmd.textures().size() <= 1 || rmd.textures()[1].name != "bc002_face_t02")
			return 8;
		if (textureFailure)
			return 5;
		Common::Array<Graphics::Surface *> textures;
		textures.resize(rmd.textures().size());
		for (uint ti = 0; ti < textures.size(); ++ti)
			textures[ti] = rmd.getTextureSurface(ti);
		Graphics::RMDRenderMesh mesh;
		// engines/smt/smt.cpp's modelAngle (M_PI_2) is a spin-animation start
		// angle, not "front" - it's a side profile. PI is front-facing.
		const float rotY = (float)M_PI;
		Graphics::assembleRMDMesh(rmd, textures, 100, 100, rotY, nullptr, nullptr, mesh);
		if (mesh.triangles.size() <= 1629)
			return 10;
		std::cout << "renderUVFace=";
		for (int k = 0; k < 3; ++k)
			std::cout << (k ? " " : "") << mesh.triangles[1629].v[k].u << "," << mesh.triangles[1629].v[k].v;
		std::cout << "\n";
		if (!near(mesh.triangles[1629].v[0].v, 0.738607f))
			return 10;
	}
	return rmd.ok() ? 0 : 4;
}

int main(int argc, char **argv) {
	if (argc == 4)
		return dumpRMD(argv[1], argv[2], argv[3], /*strictSelfCheck=*/false);
	if (argc == 2 || argc == 3)
		return dumpRMD(argv[1], argc == 3 ? argv[2] : nullptr);
	Common::MemoryReadStream stream(nullptr, 0, DisposeAfterUse::NO);
	Common::RMD rmd(&stream);
	rmd._ok = true;

	Common::RMD::Frame frame;
	frame.transform[0] = 0; frame.transform[1] = -1; frame.transform[2] = 0;
	frame.transform[3] = 1; frame.transform[4] = 0; frame.transform[5] = 0;
	frame.transform[6] = 0; frame.transform[7] = 0; frame.transform[8] = 1;
	rmd._frames.push_back(frame);

	Common::RMD::Material opaque;
	opaque.color = {255, 255, 255, 255};
	Common::RMD::Material translucent;
	translucent.color = {255, 255, 255, 128};
	translucent.alpha = true;
	rmd._materials.push_back(opaque);
	rmd._materials.push_back(translucent);

	Common::RMD::Geometry geometry;
	geometry.positions.push_back({0, 0, 0});
	geometry.positions.push_back({1, 0, 0});
	geometry.positions.push_back({0, 1, 0});
	geometry.positions.push_back({0.5f, 0.5f, 0});
	geometry.indices.push_back(0); geometry.indices.push_back(1); geometry.indices.push_back(2);
	geometry.indices.push_back(0); geometry.indices.push_back(2); geometry.indices.push_back(3);
	geometry.indices.push_back(0); geometry.indices.push_back(3); geometry.indices.push_back(99);
	geometry.materialIndices.push_back(1);
	geometry.materialIndices.push_back(0);
	geometry.materialIndices.push_back(0);
	rmd._geometries.push_back(geometry);
	Common::RMD::Atomic atomic;
	atomic.frame = 0;
	atomic.geometry = 0;
	rmd._atomics.push_back(atomic);

	Common::Array<Graphics::Surface *> textures;
	Graphics::RMDRenderMesh mesh;
	Graphics::assembleRMDMesh(rmd, textures, 100, 100, 0, nullptr, nullptr, mesh);
	if (mesh.triangles.size() != 2 || !mesh.bounds.valid || mesh.triangles[0].alpha || !mesh.triangles[1].alpha)
		return 1;
	for (uint i = 0; i < mesh.triangles.size(); ++i)
		for (int k = 0; k < 3; ++k) {
			const Graphics::RMDRenderVertex &v = mesh.triangles[i].v[k];
			if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
					!std::isfinite(v.nx) || !std::isfinite(v.ny) || !std::isfinite(v.nz) ||
					v.a <= 0.0f || v.r <= 0.0f || v.g <= 0.0f || v.b <= 0.0f)
				return 5;
		}
	const Graphics::RMDRenderTriangle &tri = mesh.triangles[1];
	if (!tri.alpha || !near(tri.v[0].a, 128.0f / 255.0f) || !near(tri.v[0].nx, 0.0f) || !near(tri.v[0].ny, 0.0f) || !near(tri.v[0].nz, 1.0f))
		return 2;
	if (!near(mesh.bounds.minX, 0.0f) || !near(mesh.bounds.maxX, 1.0f) || !near(mesh.bounds.minY, -1.0f) || !near(mesh.bounds.maxY, 0.0f))
		return 3;

	geometry.indices.clear();
	geometry.indices.push_back(0); geometry.indices.push_back(1); geometry.indices.push_back(99);
	rmd._geometries[0] = geometry;
	Graphics::assembleRMDMesh(rmd, textures, 100, 100, 0, nullptr, nullptr, mesh);
	if (!mesh.triangles.empty() || mesh.bounds.valid)
		return 4;

	// Mesh-list material indices are already resolved by the parser.
	geometry.indices.clear();
	geometry.indices.push_back(0); geometry.indices.push_back(1); geometry.indices.push_back(2);
	geometry.materialIndices.clear();
	geometry.materialIndices.push_back(1);
	geometry.materialMap.clear();
	geometry.materialMap.push_back(1);
	geometry.materialMap.push_back(0);
	rmd._geometries[0] = geometry;
	Graphics::assembleRMDMesh(rmd, textures, 100, 100, 0, nullptr, nullptr, mesh);
	if (mesh.triangles.size() != 1 || !mesh.triangles[0].alpha)
		return 6;

	// A skinned atomic may be attached to a different frame than its bones.
	rmd._frames[0].transform[9] = 10.0f;
	Common::RMD::Frame boneFrame;
	rmd._frames.push_back(boneFrame);
	geometry.materialIndices[0] = 0;
	geometry.materialMap.clear();
	geometry.skinned = true;
	geometry.skin.boneCount = 2;
	geometry.skin.influences.resize(geometry.positions.size());
	for (uint i = 0; i < geometry.skin.influences.size(); ++i) {
		geometry.skin.influences[i].indices[0] = 1;
		geometry.skin.influences[i].weights[0] = 1.0f;
	}
	geometry.skin.inverseBindMatrices.resize(2);
	geometry.skin.rendererBoneFrames.push_back(0);
	geometry.skin.rendererBoneFrames.push_back(1);
	rmd._geometries[0] = geometry;
	Graphics::assembleRMDMesh(rmd, textures, 100, 100, 0, nullptr, nullptr, mesh);
	if (!mesh.bounds.valid || !near(mesh.bounds.minX, 0.0f))
		return 7;

	// Regression: the rest-space owner must remain the bind root while the
	// animated bone moves only vertices weighted to that bone.
	geometry.positions.clear();
	geometry.positions.push_back({0, 0, 0});
	geometry.positions.push_back({1, 0, 0});
	geometry.positions.push_back({0, 0, 0});
	geometry.indices.clear();
	geometry.indices.push_back(0); geometry.indices.push_back(1); geometry.indices.push_back(2);
	geometry.materialIndices.clear();
	geometry.materialIndices.push_back(0);
	geometry.skin.influences.clear();
	geometry.skin.influences.resize(3);
	geometry.skin.influences[0].indices[0] = 1;
	geometry.skin.influences[0].weights[0] = 1.0f;
	geometry.skin.influences[1].indices[0] = 1;
	geometry.skin.influences[1].weights[0] = 1.0f;
	geometry.skin.influences[2].indices[0] = 0;
	geometry.skin.influences[2].weights[0] = 1.0f;
	rmd._frames[1].parent = 0;
	rmd._frames[1].transform[0] = 1.0f;
	rmd._frames[1].transform[4] = 1.0f;
	rmd._frames[1].transform[8] = 1.0f;
	rmd._frames[1].transform[9] = 2.0f;
	rmd._geometries[0] = geometry;
	Common::RMD::BonePose pose;
	pose.worldTransforms.resize(2);
	pose.worldTransforms[0] = Common::RMD::Matrix();
	pose.worldTransforms[0].m[12] = 20.0f;
	pose.worldTransforms[1] = Common::RMD::Matrix();
	pose.worldTransforms[1].m[12] = 24.0f;
	Graphics::RMDBounds fixed;
	fixed.valid = true;
	fixed.minX = 0.0f; fixed.maxX = 40.0f;
	fixed.minY = -1.0f; fixed.maxY = 1.0f;
	fixed.minZ = -1.0f; fixed.maxZ = 1.0f;
	Graphics::assembleRMDMesh(rmd, textures, 100, 100, 0, &pose, &fixed, mesh);
	if (mesh.triangles.size() != 1 || !near(mesh.triangles[0].v[0].x, 54.75f) || !near(mesh.triangles[0].v[2].x, 50.0f))
		return 8;

	return 0;
}
