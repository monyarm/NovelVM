#include "graphics/gmo_mesh.h"

#include "math/matrix4.h"
#include "math/quat.h"
#include "math/vector4d.h"

namespace Graphics {

void decodeGMOTextures(const Common::GMO &gmo, Common::Array<Graphics::Surface *> &out) {
	out.clear();
	// Keep the array 1:1 with GMO texture indices: material layers reference
	// textures by index, so skipping an empty/failed texture here would shift
	// every later index and bind the wrong texture to later parts.
	out.resize(gmo.textures().size());
	for (uint t = 0; t < gmo.textures().size(); t++) {
		Graphics::Surface *s = gmo.getTextureSurface((int)t);
		if (s && s->w > 0 && s->h > 0) {
			out[t] = s;
		} else {
			out[t] = nullptr;
			if (s) {
				s->free();
				delete s;
			}
		}
	}
}

void freeGMOTextures(Common::Array<Graphics::Surface *> &textures) {
	for (uint t = 0; t < textures.size(); t++) {
		if (!textures[t])
			continue;
		textures[t]->free();
		delete textures[t];
	}
	textures.clear();
}

void buildGMOMesh(const Common::GMO &gmo, const Common::Array<Graphics::Surface *> &textures,
                  int width, int height, float rotY,
                  const Common::GMO::BonePose *bonePose, const Common::GMO::MaterialPose *materialPose,
                  const GMOBounds *fixedBounds, GMOBounds *outBounds,
                  Common::Array<Tri> &outTris) {
	const int W = width, H = height;
	outTris.clear();

	if (gmo.parts().empty())
		return;

	int nb = gmo.bones().size();

	// Per-bone local TRS composed with the parent chain. Row-vector
	// convention (Matrix4::transform does v*M): the reference importer
	// builds local = R*S with the translation placed in the matrix's
	// translation row, i.e. exactly `R*S*T` here (v*R*S*T = v*R*S + t),
	// and world = local * parentWorld. Do NOT apply the importer's
	// RotateBy90 root tweak: that only compensates for 3ds Max's Z-up
	// scene, while this renderer is already Y-up, and folding it in bends
	// the model (verified against real ps096/npc_04).
	Common::Array<Math::Matrix4> boneMats, bindMats;
	boneMats.resize(nb);
	bindMats.resize(nb);
	// Build the bone world matrices for either the static bind pose
	// (`pose == nullptr`) or an animated pose, in the row-vector convention
	// described above. The static set is also needed as the skinning
	// inverse bind: the deformation for vertex v is
	//   owningWorld * Σ w * (bindWorld_j⁻¹ * posedWorld_j) * v
	// which is exactly the bind pose when no animation overrides apply.
	auto buildMats = [&](const Common::GMO::BonePose *pose, Common::Array<Math::Matrix4> &mats) {
		for (int i = 0; i < nb; i++) {
			const Common::GMO::Bone &b = gmo.bones()[i];
			bool hasT = b.hasTranslate, hasR = b.hasRotate, hasS = b.hasScale;
			byte rtype = b.rotateType;
			float tr[3] = {b.translate[0], b.translate[1], b.translate[2]};
			float rr[4] = {b.rotate[0], b.rotate[1], b.rotate[2], b.rotate[3]};
			float sc[3] = {b.scale[0], b.scale[1], b.scale[2]};
			if (pose && i < pose->boneCount) {
				if (pose->hasTranslate[i]) {
					hasT = true;
					for (int k = 0; k < 3; k++) tr[k] = pose->translate[i * 3 + k];
				}
				if (pose->hasRotate[i]) {
					hasR = true;
					rtype = pose->rotateType[i];
					for (int k = 0; k < 4; k++) rr[k] = pose->rotate[i * 4 + k];
				}
				if (pose->hasScale[i]) {
					hasS = true;
					for (int k = 0; k < 3; k++) sc[k] = pose->scale[i * 3 + k];
				}
			}
			Math::Matrix4 local;
			local.setToIdentity();
			// GMO composes local = S*R*T (scale, then rotate, then
			// translate), confirmed against both reference renderers.
			if (hasS) {
				Math::Matrix4 s;
				s.setToIdentity();
				s(0, 0) = sc[0];
				s(1, 1) = sc[1];
				s(2, 2) = sc[2];
				local = local * s;
			}
			if (hasR) {
				Math::Matrix4 r;
				r.setToIdentity();
				if (rtype <= 1) {
					Math::Angle ax = Math::Angle::fromRadians(rr[0]);
					Math::Angle ay = Math::Angle::fromRadians(rr[1]);
					Math::Angle az = Math::Angle::fromRadians(rr[2]);
					// buildFromEuler takes its angles in axis order (the
					// first argument is the axis named first by the order),
					// while GMO stores the euler triple as (x, y, z).
					if (rtype == 0)
						r.buildFromEuler(az, ay, ax, Math::EO_ZYX);
					else
						r.buildFromEuler(ay, ax, az, Math::EO_YXZ);
					// buildFromEuler/toMatrix produce standard (column-vector)
					// rotation matrices, but this renderer composes with
					// Matrix4::transform/operator*, which are row-vector.
					// Applying v*R therefore rotates by R^-1, so transposing
					// gives the correct orientation. Verified: with this
					// transpose every rest bone world matches the Noesis glTF
					// export exactly.
					r.transpose();
				} else {
					// GMO stores quaternions as (x, y, z, w); Math::Quaternion's
					// constructor also takes (x, y, z, w).
					Math::Quaternion q(rr[0], rr[1], rr[2], rr[3]);
					r = q.toMatrix();
					r.transpose();
				}
				local = local * r;
			}
			if (hasT) {
				Math::Matrix4 t;
				t.setToIdentity();
				t(3, 0) = tr[0];
				t(3, 1) = tr[1];
				t(3, 2) = tr[2];
				local = local * t;
			}
			if (b.pivot[0] != 0.0f || b.pivot[1] != 0.0f || b.pivot[2] != 0.0f) {
				Math::Matrix4 toPivot, fromPivot;
				toPivot.setToIdentity(); fromPivot.setToIdentity();
				toPivot(3, 0) = -b.pivot[0]; toPivot(3, 1) = -b.pivot[1]; toPivot(3, 2) = -b.pivot[2];
				fromPivot(3, 0) = b.pivot[0]; fromPivot(3, 1) = b.pivot[1]; fromPivot(3, 2) = b.pivot[2];
				local = toPivot * local * fromPivot;
			}
			if (b.parentIndex >= 0 && b.parentIndex < nb)
				mats[i] = local * mats[b.parentIndex];
			else
				mats[i] = local;
		}
	};
	buildMats(nullptr, bindMats);
	buildMats(bonePose, boneMats);

	// Per-part owning bone + skinning (BLEND_BONES/BLEND_OFFSETS/WEIGHTS).
	Common::Array<int> partBone;
	partBone.resize(gmo.parts().size());
	for (uint pi = 0; pi < gmo.parts().size(); pi++) {
		partBone[pi] = 0;
		for (int bi = 0; bi < nb; bi++)
			for (uint dj = 0; dj < gmo.bones()[bi].drawParts.size(); dj++)
				if (gmo.bones()[bi].drawParts[dj].index == (int)pi)
					partBone[pi] = bi;
	}

	float vOff[3] = {0, 0, 0}, vScale[3] = {1, 1, 1};
	bool hasVOff = gmo.getVertexOffset(vOff, vScale);

	// Model-level ARRAY_OFFSET texture variant (TEXTURE_FLOAT): the same
	// "data = data * scale + offset" transform as the vertex offset, but
	// applied to every texcoord. Without this, models that carry one get
	// completely wrong UVs (wrong texture region on every part).
	float tOff[2] = {0, 0}, tScale[2] = {1, 1};
	bool hasTOff = gmo.getTextureOffset(tOff, tScale);

	// Spin around the up (Y) axis, then turn 180 degrees about Z; together with the y-down screen
	// mapping below that views the model from -z, the side models face, without mirroring it
	// (checked against Noesis on real P3P models: asymmetric ones like Athena's spear hand).
	Math::Matrix4 worldRot;
	worldRot.setToIdentity();
	if (rotY != 0.0f) {
		float c = cosf(rotY), s = sinf(rotY);
		worldRot(0, 0) = c; worldRot(0, 2) = s;
		worldRot(2, 0) = -s; worldRot(2, 2) = c;
	}
	Math::Matrix4 z180;
	z180.setToIdentity();
	z180(0, 0) = -1; z180(1, 1) = -1;
	worldRot = worldRot * z180;

	// Bounding box of the ASSEMBLED vertices, AFTER the world rotation, so
	// the auto-framing stays correct while the model spins. When the caller
	// supplies a fixed bounds (the union over a motion) it is used verbatim
	// so the zoom stays constant through the animation.
	float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	int totalVerts = 0;
	if (fixedBounds && fixedBounds->valid) {
		minX = fixedBounds->minX; maxX = fixedBounds->maxX;
		minY = fixedBounds->minY; maxY = fixedBounds->maxY;
		minZ = fixedBounds->minZ; maxZ = fixedBounds->maxZ;
		totalVerts = 1;
	} else {
		for (uint pi = 0; pi < gmo.parts().size(); pi++) {
			const Common::GMO::Part &p = gmo.parts()[pi];
			const Math::Matrix4 &pm = boneMats[partBone[pi]];
			for (uint ai = 0; ai < p.arrays.size(); ai++) {
				const Common::GMO::Arrays &a = p.arrays[ai];
				for (uint v = 0; v + 2 < a.positions.size(); v += 3) {
					float x = a.positions[v], y = a.positions[v + 1], z = a.positions[v + 2];
					if (hasVOff) { x = x * vScale[0] + vOff[0]; y = y * vScale[1] + vOff[1]; z = z * vScale[2] + vOff[2]; }
					Math::Vector4d tp = pm.transform(Math::Vector4d(x, y, z, 1.0f));
					tp = worldRot.transform(tp);
					if (tp.x() < minX) minX = tp.x();
					if (tp.x() > maxX) maxX = tp.x();
					if (tp.y() < minY) minY = tp.y();
					if (tp.y() > maxY) maxY = tp.y();
					if (tp.z() < minZ) minZ = tp.z();
					if (tp.z() > maxZ) maxZ = tp.z();
					totalVerts++;
				}
			}
		}
	}
	if (totalVerts == 0)
		return;

	if (outBounds) {
		outBounds->minX = minX; outBounds->maxX = maxX;
		outBounds->minY = minY; outBounds->maxY = maxY;
		outBounds->minZ = minZ; outBounds->maxZ = maxZ;
		outBounds->valid = totalVerts > 0;
	}

	float cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
	// Use the larger horizontal extent so the framing is stable while the
	// model spins about Y.
	float spanX = MAX(maxX - minX, maxZ - minZ), spanY = maxY - minY;
	if (spanX <= 0.0001f) spanX = 1;
	if (spanY <= 0.0001f) spanY = 1;
	float scale = MIN(H * 0.85f / spanY, W * 0.95f / spanX);

	for (uint pi = 0; pi < gmo.parts().size(); pi++) {
		const Common::GMO::Part &p = gmo.parts()[pi];
		const int owningBone = partBone[pi];
		const Common::GMO::Bone &own = gmo.bones()[owningBone];
		// VISIBILITY: skip parts drawn by a bone flagged invisible.
		if (own.visibility == 0)
			continue;
		const Math::Matrix4 &m = boneMats[owningBone];

		for (uint ai = 0; ai < p.arrays.size(); ai++) {
			const Common::GMO::Arrays &a = p.arrays[ai];
			if (a.vertexCount == 0 || a.positions.empty() || a.positions.size() < a.vertexCount * 3)
				continue;
			int weightCount = a.vertexCount ? (int)(a.weights.size() / a.vertexCount) : 0;

			for (uint mi = 0; mi < p.meshes.size(); mi++) {
				const Common::GMO::Mesh &mesh = p.meshes[mi];

				float matDiff[3] = {0.85f, 0.85f, 0.85f};
				float matAlpha = 1.0f;
				float matAmb[3] = {0.0f, 0.0f, 0.0f};
				float matEmi[3] = {0.0f, 0.0f, 0.0f};
				float matSpec[3] = {1.0f, 1.0f, 1.0f};
				float specPower = 0.0f, reflection = 0.0f, refraction = 0.0f, bump = 0.0f;
				float matMapFactor = 1.0f;
				uint32 texFunc = kGuTfxModulate, texComp = kGuTccRgb;
				uint32 filterMin = kGuNearest, filterMag = kGuNearest;
				uint32 wrapU = kGuRepeat, wrapV = kGuRepeat;
				uint32 mapType = 0, texGen = 0;
				float crop0 = 0.0f, crop1 = 0.0f, crop2 = 1.0f, crop3 = 1.0f;
				float texMtx[4] = {1, 0, 0, 1};
				float texMtxTrans[2] = {0, 0};
				uint32 blendMode = kGuAdd, blendSrc = kGuSrcAlpha, blendDst = kGuOneMinusSrcAlpha;
				bool blendEnabled = false;
				uint32 matTransAlgo = 0;
				int texIdx = -1;

				if (mesh.material.index >= 0 && mesh.material.index < (int)gmo.materials().size()) {
					const Common::GMO::Material &mat = gmo.materials()[mesh.material.index];
					if (mat.hasDiffuse) {
						for (int i = 0; i < 3; i++) matDiff[i] = CLIP(mat.diffuse[i], 0.0f, 1.0f);
						matAlpha = CLIP(mat.diffuse[3], 0.0f, 1.0f);
					}
					if (mat.hasAmbient) for (int i = 0; i < 3; i++) matAmb[i] = CLIP(mat.ambient[i], 0.0f, 1.0f);
					if (mat.hasEmission) for (int i = 0; i < 3; i++) matEmi[i] = CLIP(mat.emission[i], 0.0f, 1.0f);
					if (mat.hasSpecular) {
						for (int i = 0; i < 3; i++) matSpec[i] = CLIP(mat.specular[i], 0.0f, 1.0f);
						specPower = CLIP(mat.specularPower, 0.0f, 128.0f);
					}
					reflection = CLIP(mat.reflection, 0.0f, 1.0f);
					refraction = CLIP(mat.refraction, 0.0f, 1.0f);
					bump = mat.bump;
					matTransAlgo = mat.transAlgo;
					if (!mat.layers.empty()) {
						const Common::GMO::Layer &L = mat.layers[0];
						texIdx = L.texture.index;
						matMapFactor = CLIP(L.mapFactor, 0.0f, 1.0f);
						texFunc = L.texFunc; texComp = L.texComp;
						filterMin = L.texFilterMin; filterMag = L.texFilterMag;
						wrapU = L.texWrapU; wrapV = L.texWrapV;
						mapType = L.mapType; texGen = L.texGen;
						crop0 = L.texCrop[0]; crop1 = L.texCrop[1]; crop2 = L.texCrop[2]; crop3 = L.texCrop[3];
						texMtx[0] = L.texMatrix[0]; texMtx[1] = L.texMatrix[1];
						texMtx[2] = L.texMatrix[4]; texMtx[3] = L.texMatrix[5];
						texMtxTrans[0] = L.texMatrix[12]; texMtxTrans[1] = L.texMatrix[13];
						blendMode = L.blendMode; blendSrc = L.blendSrc; blendDst = L.blendDst;
						blendEnabled = L.hasBlendFunc;
					}
				}
				if (materialPose && mesh.material.index >= 0 &&
				    mesh.material.index < materialPose->materialCount) {
					int mi = mesh.material.index;
					if (materialPose->hasDiffuse[mi]) {
						for (int i = 0; i < 3; i++) matDiff[i] = CLIP(materialPose->diffuse[mi * 4 + i], 0.0f, 1.0f);
						matAlpha = CLIP(materialPose->diffuse[mi * 4 + 3], 0.0f, 1.0f);
					}
					if (materialPose->hasEmission[mi])
						for (int i = 0; i < 3; i++) matEmi[i] = CLIP(materialPose->emission[mi * 4 + i], 0.0f, 1.0f);
					if (materialPose->hasAmbient[mi])
						for (int i = 0; i < 3; i++) matAmb[i] = CLIP(materialPose->ambient[mi * 4 + i], 0.0f, 1.0f);
					if (materialPose->hasSpecular[mi])
						for (int i = 0; i < 3; i++) matSpec[i] = CLIP(materialPose->specular[mi * 4 + i], 0.0f, 1.0f);
					// Animated TEXCROP (P3P field objects scroll their textures this way).
					if (mi < (int)materialPose->hasTexCrop.size() && materialPose->hasTexCrop[mi]) {
						crop0 = materialPose->texCrop[mi * 4 + 0];
						crop1 = materialPose->texCrop[mi * 4 + 1];
						crop2 = materialPose->texCrop[mi * 4 + 2];
						crop3 = materialPose->texCrop[mi * 4 + 3];
					}
				}
				if (texIdx < 0)
					texIdx = 0;

				for (uint di = 0; di < mesh.draws.size(); di++) {
					const Common::GMO::DrawInfo &d = mesh.draws[di];
					if (d.arrays.index != (int)ai)
						continue;
					int modeLo = d.mode & 0xFF;
					bool seq = (d.mode & 0x100) != 0;

					for (uint pr = 0; pr < d.primitiveCount; pr++) {
						if (seq && d.startVertex + pr * d.vertexCount >= a.vertexCount)
							continue;

						auto idx = [&](unsigned k) -> unsigned {
							if (seq)
								return d.startVertex + pr * d.vertexCount + k;
							unsigned off = pr * d.vertexCount + k;
							return off < d.indices.size() ? d.indices[off] : 0;
						};

						// Transform one vertex to world space, applying
						// skinning when the part has blend bones/weights.
						auto worldVert = [&](unsigned v) -> Math::Vector4d {
							const float *pnt = &a.positions[v * 3];
							float wx = pnt[0], wy = pnt[1], wz = pnt[2];
							if (hasVOff) {
								wx = wx * vScale[0] + vOff[0];
								wy = wy * vScale[1] + vOff[1];
								wz = wz * vScale[2] + vOff[2];
							}
							Math::Vector4d lv(wx, wy, wz, 1.0f);
							// Linear-blend skinning. Confirmed against a
							// Noesis export of the same model: the bind-pose
							// vertices are the MODEL-SPACE ARRAYS positions
							// (vertex offset applied), with no owning-bone
							// transform. So the inverse bind is recomputed
							// from the static bone pose (importers do this;
							// the stored BLEND_OFFSETS are the runtime's own
							// inverse binds and do not match our skeleton):
							//   p = SUM_i w_i * ( v * bindWorld_i^-1 * posedWorld_i )
							// At the bind pose posedWorld_i == bindWorld_i, so
							// every term is identity and p == v.
							if (weightCount > 0 && !own.blendBones.empty()) {
								// Rest WORLD position of this vertex. The
								// model-space ARRAYS vertex is in the owning
								// bone's bind space, so the owning bind world
								// lifts it into world space (this is exactly
								// the rigid path, and matches the Noesis
								// export's bind mesh).
								Math::Vector4d r = bindMats[owningBone].transform(lv);
								float accX = 0, accY = 0, accZ = 0, wsum = 0.0f;
								for (int k = 0; k < weightCount; k++) {
									float w = a.weights[(uint)v * weightCount + k];
									if (w == 0.0f)
										continue;
									unsigned idx = (k < (int)mesh.blendSubset.size()) ? mesh.blendSubset[k] : (unsigned)k;
									if (idx >= own.blendBones.size())
										continue;
									int bj = own.blendBones[idx].index;
									if (bj < 0 || bj >= nb)
										continue;
									// r * bindWorld_j^-1 * posedWorld_j
									Math::Matrix4 inv = bindMats[bj];
									if (!inv.inverse())
										continue; // singular bind (e.g. zero scale)
									Math::Vector4d t = (inv * boneMats[bj]).transform(r);
									accX += t.x() * w;
									accY += t.y() * w;
									accZ += t.z() * w;
									wsum += w;
								}
								if (wsum > 0.0001f)
									return Math::Vector4d(accX, accY, accZ, 1.0f);
							}
							return m.transform(lv);
						};

						auto proj = [&](unsigned v, const Math::Vector4d &tp, float &sx, float &sy, float &sz) {
							(void)v;
							// Model is Y-up: X right, Y up.
							sx = W / 2 + (tp.x() - cx) * scale;
							sy = H / 2 + (tp.y() - cy) * scale;
							sz = tp.z();
						};

						auto emit = [&](unsigned a0, unsigned a1, unsigned a2) {
							if (a0 >= a.vertexCount || a1 >= a.vertexCount || a2 >= a.vertexCount)
								return;
							Math::Vector4d w0 = worldVert(a0), w1 = worldVert(a1), w2 = worldVert(a2);
							w0 = worldRot.transform(w0);
							w1 = worldRot.transform(w1);
							w2 = worldRot.transform(w2);
							Tri t;
							proj(a0, w0, t.x[0], t.y[0], t.z[0]);
							proj(a1, w1, t.x[1], t.y[1], t.z[1]);
							proj(a2, w2, t.x[2], t.y[2], t.z[2]);

							// Per-vertex normals, transformed by the world
							// rotation (rotation-only; translation ignored).
							t.hasNormal = (a.normals.size() >= a.vertexCount * 3);
							unsigned ids[3] = { a0, a1, a2 };
							if (t.hasNormal) {
								for (int k = 0; k < 3; k++) {
									Math::Vector4d n(a.normals[ids[k] * 3], a.normals[ids[k] * 3 + 1], a.normals[ids[k] * 3 + 2], 0.0f);
									n = worldRot.transform(n);
									t.nx[k] = n.x(); t.ny[k] = n.y(); t.nz[k] = n.z();
								}
							}

							// Texture coordinate source: TEX_GEN / MAP_TYPE
							// (0 = texcoords, 1 = normals, 2 = position),
							// then TEX_CROP sub-rect and TEX_MATRIX.
							bool hasUv = a.texcoords.size() >= a.vertexCount * 2;
							bool hasNorm = a.normals.size() >= a.vertexCount * 3;
							uint32 gen = texGen;
							if (mapType != 0 && gen == 0)
								gen = mapType;
							t.hasUv = hasUv || (gen == 1 && hasNorm) || gen == 2;
							for (int k = 0; k < 3; k++) {
								float uu = 0, vv = 0;
								if (gen == 1 && hasNorm) {
									uu = a.normals[ids[k] * 3];
									vv = a.normals[ids[k] * 3 + 1];
								} else if (gen == 2) {
									const float *pnt = &a.positions[ids[k] * 3];
									uu = pnt[0]; vv = pnt[1];
								} else if (hasUv) {
									// MORPH UV OFFSETS: when present these
									// replace the static texcoords (reference
									// importer's `NewAr.UvOffset` behavior).
									if (a.uvOffsets.size() >= a.vertexCount * 2) {
										uu = (float)a.uvOffsets[ids[k] * 2] / 32768.0f;
										vv = (float)a.uvOffsets[ids[k] * 2 + 1] / 32768.0f;
									} else {
										uu = a.texcoords[ids[k] * 2];
										vv = a.texcoords[ids[k] * 2 + 1];
									}
								}
								// Model-level texture ARRAY_OFFSET, applied to
								// the source texcoord before crop/matrix.
								if (hasTOff) {
									uu = uu * tScale[0] + tOff[0];
									vv = vv * tScale[1] + tOff[1];
								}
								// TEX_CROP: uv in the crop sub-rect.
								if (crop2 != 1.0f || crop3 != 1.0f || crop0 != 0.0f || crop1 != 0.0f) {
									uu = crop0 + uu * crop2;
									vv = crop1 + vv * crop3;
								}
								// TEX_MATRIX: 2D uv transform (identity in all
								// real P3P samples).
								if (texMtx[0] != 1.0f || texMtx[1] != 0.0f || texMtx[2] != 0.0f || texMtx[3] != 1.0f ||
								    texMtxTrans[0] != 0.0f || texMtxTrans[1] != 0.0f) {
									float nu = uu * texMtx[0] + vv * texMtx[2] + texMtxTrans[0];
									float nv = uu * texMtx[1] + vv * texMtx[3] + texMtxTrans[1];
									uu = nu; vv = nv;
								}
								t.u[k] = uu;
								t.v[k] = vv;
							}
							t.tex = (texIdx >= 0 && texIdx < (int)textures.size()) ? textures[texIdx] : nullptr;
							t.mapFactor = matMapFactor;
							t.texFunc = texFunc; t.texComp = texComp;
							t.texFilterMin = filterMin; t.texFilterMag = filterMag;
							t.wrapU = wrapU; t.wrapV = wrapV;
							t.mapType = mapType; t.texGen = texGen;
							t.blendMode = blendMode; t.blendSrc = blendSrc; t.blendDst = blendDst;
							t.blendEnabled = blendEnabled;
							t.transAlgo = matTransAlgo;
							t.specPower = specPower; t.reflection = reflection; t.refraction = refraction; t.bump = bump;
							for (int i = 0; i < 3; i++) {
								t.diffuse[i] = matDiff[i]; t.ambient[i] = matAmb[i];
								t.emission[i] = matEmi[i]; t.specular[i] = matSpec[i];
							}
							t.alpha = matAlpha;

							t.hasCol = a.colors.size() >= a.vertexCount * 4;
							for (int k = 0; k < 3; k++) {
								if (t.hasCol) {
									t.cr[k] = CLIP(a.colors[ids[k] * 4 + 0], 0.0f, 1.0f);
									t.cg[k] = CLIP(a.colors[ids[k] * 4 + 1], 0.0f, 1.0f);
									t.cb[k] = CLIP(a.colors[ids[k] * 4 + 2], 0.0f, 1.0f);
									t.ca[k] = CLIP(a.colors[ids[k] * 4 + 3], 0.0f, 1.0f);
								} else {
									t.cr[k] = t.cg[k] = t.cb[k] = 1.0f; t.ca[k] = 1.0f;
								}
							}

							t.r = (byte)(matDiff[0] * 255.0f);
							t.g = (byte)(matDiff[1] * 255.0f);
							t.b = (byte)(matDiff[2] * 255.0f);
							outTris.push_back(t);
						};

						if (modeLo == 3) {
							for (unsigned k = 0; k + 2 < d.vertexCount; k += 3)
								emit(idx(k), idx(k + 1), idx(k + 2));
						} else if (modeLo == 4) {
							for (unsigned k = 0; k + 2 < d.vertexCount; k++) {
								unsigned a0 = idx(k), a1 = idx(k + 1), a2 = idx(k + 2);
								if (k & 1) { unsigned tmp = a0; a0 = a1; a1 = tmp; }
								emit(a0, a1, a2);
							}
						} else if (modeLo == 5) {
							for (unsigned k = 2; k < d.vertexCount; k++)
								emit(idx(0), idx(k - 1), idx(k));
						}
					}
				}
			}
		}
	}

	// MATERIAL RENDER ORDER: transAlgo controls the draw order of a material
	// (Amicitia wiki "GMO" -> TransAlgo). Stable-sort triangles so higher
	// render-order materials are drawn later.
	for (uint i = 1; i < outTris.size(); i++) {
		Tri key = outTris[i];
		int j = (int)i - 1;
		while (j >= 0 && outTris[j].transAlgo > key.transAlgo) {
			outTris[j + 1] = outTris[j];
			j--;
		}
		outTris[j + 1] = key;
	}
}

} // End of namespace Graphics
