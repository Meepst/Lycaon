#include "scene.h"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <meshoptimizer.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <execution>
#include <limits>
#include <numeric>
#include <iostream>

namespace pack {

// Quantize a float in [0,1] to uint16.
static uint16_t quantizeUnorm16(float v)
{
	return (uint16_t)(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

// Quantize a position component given bounding box to uint16.
static uint16_t quantizePosition(float v, float center, float radius)
{
	if (radius < 1e-12f) return 32768;  // degenerate: map to sphere center
	float normalized = (v - center) / radius;  // now in approximately [-1, 1]
	return quantizeUnorm16(normalized * 0.5f + 0.5f);  // to [0, 1] then to uint16
}

// Octahedral encoding: map unit normal to [0,1]^2.
static void octEncode(float nx, float ny, float nz, float& ou, float& ov)
{
	float l = std::abs(nx) + std::abs(ny) + std::abs(nz);
	if (l > 1e-12f) { nx /= l; ny /= l; nz /= l; }

	if (nz >= 0.0f) {
		ou = nx * 0.5f + 0.5f;
		ov = ny * 0.5f + 0.5f;
	} else {
		ou = (1.0f - std::abs(ny)) * (nx >= 0.0f ? 1.0f : -1.0f) * 0.5f + 0.5f;
		ov = (1.0f - std::abs(nx)) * (ny >= 0.0f ? 1.0f : -1.0f) * 0.5f + 0.5f;
	}
}

// Pack normal into 10-10-10-2 format with bitangent sign in the 2-bit field.
static uint32_t packNormal1010102(float nx, float ny, float nz, float bitangentSign)
{
	auto encode10 = [](float v) -> uint32_t {
		int s = (int)(std::clamp(v, -1.0f, 1.0f) * 511.0f + (v >= 0.0f ? 0.5f : -0.5f));
		return (uint32_t)(s & 0x3FF);
	};
	uint32_t sign = bitangentSign >= 0.0f ? 0u : 3u; // 2-bit sign
	return encode10(nx) | (encode10(ny) << 10) | (encode10(nz) << 20) | (sign << 30);
}

// Pack tangent xy as 8-8 octahedral into uint16.
static uint16_t packTangentOct88(float tx, float ty, float tz)
{
	float ou, ov;
	octEncode(tx, ty, tz, ou, ov);
	uint8_t a = (uint8_t)(std::clamp(ou, 0.0f, 1.0f) * 255.0f + 0.5f);
	uint8_t b = (uint8_t)(std::clamp(ov, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (uint16_t)a | ((uint16_t)b << 8);
}

// Quantize UV to uint16 with wrapping support.
// For UVs outside [0,1], this wraps via frac, which is correct for GL_REPEAT.
static uint16_t quantizeUV(float v)
{
	float f = v - std::floor(v); // frac
	return quantizeUnorm16(f);
}

// Compute bounding sphere (simple Ritter-like: centroid + max distance).
static void boundingSphere(const float* positions, size_t count,
                           vec3& outCenter, float& outRadius)
{
	if (count == 0) {
		outCenter = vec3(0);
		outRadius = 0;
		return;
	}

	vec3 mn(std::numeric_limits<float>::max());
	vec3 mx(std::numeric_limits<float>::lowest());

	for (size_t i = 0; i < count; i++) {
		vec3 p(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
		mn = glm::min(mn, p);
		mx = glm::max(mx, p);
	}

	outCenter = (mn + mx) * 0.5f;
	outRadius = 0;

	for (size_t i = 0; i < count; i++) {
		vec3 p(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
		outRadius = std::max(outRadius, glm::length(p - outCenter));
	}
}

} // namespace pack

namespace cgltf_util {

template <typename T>
static int indexOf(const T* item, const T* array, cgltf_size count)
{
	if (!item || !array) return -1;
	ptrdiff_t idx = item - array;
	if (idx < 0 || idx >= (ptrdiff_t)count) return -1;
	return (int)idx;
}

static std::vector<float> unpackFloats(const cgltf_accessor* acc)
{
	if (!acc) return {};
	cgltf_size nc = cgltf_num_components(acc->type);
	std::vector<float> out(acc->count * nc);
	cgltf_accessor_unpack_floats(acc, out.data(), out.size());
	return out;
}

static std::vector<uint32_t> unpackIndices(const cgltf_accessor* acc)
{
	if (!acc) return {};
	std::vector<uint32_t> out(acc->count);
	for (cgltf_size i = 0; i < acc->count; i++)
		out[i] = (uint32_t)cgltf_accessor_read_index(acc, i);
	return out;
}

} // namespace cgltf_util


namespace {

struct PrimitiveResult {
	// Local geometry
	std::vector<Vertex>   vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet>  meshlets;
	std::vector<uint32_t> meshletData;

	// Per-meshlet local dataOffset
	std::vector<uint32_t> meshletLocalDataOffset;

	// Mesh record; offsets inside are local and fixed up at merge time.
	Mesh mesh{};

	// Per-LOD local offsets (indices into `indices` / `meshlets`).
	struct LodLocal {
		uint32_t localIndexOffset   = 0;
		uint32_t localMeshletOffset = 0;
	};
	LodLocal lodLocal[8]{};

	uint32_t materialIndex = 0;
	bool     valid         = false;
};

struct PrimitiveJob {
	cgltf_size meshIdx;
	cgltf_size primIdx;
	size_t     resultIdx;
};

// Process a single glTF primitive into a PrimitiveResult
static PrimitiveResult processPrimitive(const cgltf_primitive& prim,
                                        const cgltf_data*      data,
                                        size_t maxVerticesPerMeshlet,
                                        size_t maxTrianglesPerMeshlet)
{
	using namespace cgltf_util;
	using namespace pack;

	PrimitiveResult r;

	// get raw attributes
	std::vector<float> rawPositions, rawNormals, rawTangents, rawUVs;

	for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
		const auto& attr = prim.attributes[ai];
		switch (attr.type) {
			case cgltf_attribute_type_position:
				rawPositions = unpackFloats(attr.data);
				break;
			case cgltf_attribute_type_normal:
				rawNormals = unpackFloats(attr.data);
				break;
			case cgltf_attribute_type_tangent:
				rawTangents = unpackFloats(attr.data);
				break;
			case cgltf_attribute_type_texcoord:
				if (attr.index == 0) rawUVs = unpackFloats(attr.data);
				break;
			default: break;
		}
	}

	if (rawPositions.empty()) return r; // invalid, not marked valid
	size_t vertCount = rawPositions.size() / 3;

	// get indices
	std::vector<uint32_t> rawIndices;
	if (prim.indices) {
		rawIndices = unpackIndices(prim.indices);
	} else {
		rawIndices.resize(vertCount);
		std::iota(rawIndices.begin(), rawIndices.end(), 0u);
	}

	std::vector<meshopt_Stream> streams;
    streams.push_back({ rawPositions.data(), sizeof(float) * 3, sizeof(float) * 3 });
    if (!rawNormals.empty())
        streams.push_back({ rawNormals.data(), sizeof(float) * 3, sizeof(float) * 3 });
    if (!rawUVs.empty())
        streams.push_back({ rawUVs.data(), sizeof(float) * 2, sizeof(float) * 2 });
    if (!rawTangents.empty())
        streams.push_back({ rawTangents.data(), sizeof(float) * 4, sizeof(float) * 4 });

	// meshoptimizer <3
	std::vector<uint32_t> remap(vertCount);
	size_t uniqueVerts = meshopt_generateVertexRemapMulti(
		remap.data(), rawIndices.data(), rawIndices.size(),
		vertCount, streams.data(), streams.size());

	std::vector<uint32_t> optIndices(rawIndices.size());
	meshopt_remapIndexBuffer(optIndices.data(), rawIndices.data(),
	                         rawIndices.size(), remap.data());

	std::vector<float> optPositions(uniqueVerts * 3);
	meshopt_remapVertexBuffer(optPositions.data(), rawPositions.data(),
	                          vertCount, sizeof(float) * 3, remap.data());

	std::vector<float> optNormals(uniqueVerts * 3, 0.0f);
	if (!rawNormals.empty())
		meshopt_remapVertexBuffer(optNormals.data(), rawNormals.data(),
		                          vertCount, sizeof(float) * 3, remap.data());

	std::vector<float> optTangents(uniqueVerts * 4, 0.0f);
	if (!rawTangents.empty())
		meshopt_remapVertexBuffer(optTangents.data(), rawTangents.data(),
		                          vertCount, sizeof(float) * 4, remap.data());

	std::vector<float> optUVs(uniqueVerts * 2, 0.0f);
	if (!rawUVs.empty())
		meshopt_remapVertexBuffer(optUVs.data(), rawUVs.data(),
		                          vertCount, sizeof(float) * 2, remap.data());

	vertCount = uniqueVerts;

	// Vertex cache + overdraw optimization
	meshopt_optimizeVertexCache(optIndices.data(), optIndices.data(),
	                            optIndices.size(), vertCount);
	meshopt_optimizeOverdraw(optIndices.data(), optIndices.data(),
	                         optIndices.size(), optPositions.data(),
	                         vertCount, sizeof(float) * 3, 1.05f);

	// Vertex fetch optimization
	std::vector<uint32_t> fetchRemap(vertCount);
	meshopt_optimizeVertexFetchRemap(fetchRemap.data(), optIndices.data(),
	                                 optIndices.size(), vertCount);
	meshopt_remapIndexBuffer(optIndices.data(), optIndices.data(),
	                         optIndices.size(), fetchRemap.data());

	// Apply fetch remap to all attribute arrays
	{
		std::vector<float> tmp;

		tmp.resize(vertCount * 3);
		meshopt_remapVertexBuffer(tmp.data(), optPositions.data(),
		                          vertCount, sizeof(float) * 3, fetchRemap.data());
		optPositions.swap(tmp);

		tmp.resize(vertCount * 3);
		meshopt_remapVertexBuffer(tmp.data(), optNormals.data(),
		                          vertCount, sizeof(float) * 3, fetchRemap.data());
		optNormals.swap(tmp);

		tmp.resize(vertCount * 4);
		meshopt_remapVertexBuffer(tmp.data(), optTangents.data(),
		                          vertCount, sizeof(float) * 4, fetchRemap.data());
		optTangents.swap(tmp);

		tmp.resize(vertCount * 2);
		meshopt_remapVertexBuffer(tmp.data(), optUVs.data(),
		                          vertCount, sizeof(float) * 2, fetchRemap.data());
		optUVs.swap(tmp);
	}

	// bounding sphere
	vec3  meshCenter;
	float meshRadius;
	boundingSphere(optPositions.data(), vertCount, meshCenter, meshRadius);
	meshRadius *= 1.0001f;

	r.vertices.resize(vertCount);
	for (size_t v = 0; v < vertCount; ++v) {
		Vertex& vtx = r.vertices[v];

		vtx.vx = meshopt_quantizeHalf(optPositions[v * 3 + 0]);
		vtx.vy = meshopt_quantizeHalf(optPositions[v * 3 + 1]);
		vtx.vz = meshopt_quantizeHalf(optPositions[v * 3 + 2]);

		float nx = optNormals[v * 3 + 0];
		float ny = optNormals[v * 3 + 1];
		float nz = optNormals[v * 3 + 2];

		float tx = optTangents[v * 4 + 0];
		float ty = optTangents[v * 4 + 1];
		float tz = optTangents[v * 4 + 2];
		float tw = optTangents[v * 4 + 3]; // bitangent sign

		vtx.np = packNormal1010102(nx, ny, nz, tw);
		vtx.tp = packTangentOct88(tx, ty, tz);

		vtx.tu = meshopt_quantizeHalf(optUVs[v * 2 + 0]);
		vtx.tv = meshopt_quantizeHalf(optUVs[v * 2 + 1]);
	}

	r.mesh.center       = meshCenter;
	r.mesh.radius       = meshRadius;
	r.mesh.vertexOffset = 0;              // rebased at merge time
	r.mesh.vertexCount  = (uint32_t)vertCount;
	r.mesh.ommIndexData = 0;
	r.mesh.ommIndexBase = 0;
	r.mesh.lodCount     = 1;
	std::memset(r.mesh.padding, 0, sizeof(r.mesh.padding));
	std::memset(r.mesh.lods,    0, sizeof(r.mesh.lods));

	// lod 0 indices
	uint32_t localIndexOffset = 0;
	r.indices.insert(r.indices.end(), optIndices.begin(), optIndices.end());

	// build meshlets for a given index buffer and append them into
	// r.meshlets / r.meshletData, returning the local meshlet offset and count.
	auto buildMeshletsLocal = [&](const uint32_t* idxData, size_t idxCount,
	                              uint32_t& outLocalMeshletOffset,
	                              uint32_t& outMeshletCount)
	{
		size_t maxMeshlets = meshopt_buildMeshletsBound(
			idxCount, maxVerticesPerMeshlet, maxTrianglesPerMeshlet);

		std::vector<meshopt_Meshlet> rawMeshlets(maxMeshlets);
		std::vector<uint32_t> mlVertices(maxMeshlets * maxVerticesPerMeshlet);
		std::vector<uint8_t>  mlTriangles(maxMeshlets * maxTrianglesPerMeshlet * 3);

		size_t meshletCount = meshopt_buildMeshlets(
			rawMeshlets.data(), mlVertices.data(), mlTriangles.data(),
			idxData, idxCount,
			optPositions.data(), vertCount, sizeof(float) * 3,
			maxVerticesPerMeshlet, maxTrianglesPerMeshlet, 0.0f);

		rawMeshlets.resize(meshletCount);

		outLocalMeshletOffset = (uint32_t)r.meshlets.size();
		outMeshletCount       = (uint32_t)meshletCount;

		for (size_t mli = 0; mli < meshletCount; ++mli) {
			const auto& s = rawMeshlets[mli];

			meshopt_Bounds b = meshopt_computeMeshletBounds(
				&mlVertices[s.vertex_offset],
				&mlTriangles[s.triangle_offset],
				s.triangle_count,
				optPositions.data(), vertCount, sizeof(float) * 3);

			Meshlet ml{};

			ml.center[0] = b.center[0];
			ml.center[1] = b.center[1];
			ml.center[2] = b.center[2];
			ml.radius = b.radius;

			ml.cone_axis[0] = (int8_t)b.cone_axis_s8[0];
			ml.cone_axis[1] = (int8_t)b.cone_axis_s8[1];
			ml.cone_axis[2] = (int8_t)b.cone_axis_s8[2];
			ml.cone_cutoff  = (int8_t)b.cone_cutoff_s8;

			// local offsets rebased at merge time.
			uint32_t localDataOffset = (uint32_t)r.meshletData.size();
			ml.dataOffset    = localDataOffset;
			ml.baseVertex    = 0; // rebased at merge time
			ml.vertexCount   = (uint8_t)s.vertex_count;
			ml.triangleCount = (uint8_t)s.triangle_count;
			ml.shortRefs     = 0;
			ml.padding       = 0;

			// Store vertex indices
			for (uint32_t vi = 0; vi < s.vertex_count; ++vi)
				r.meshletData.push_back(mlVertices[s.vertex_offset + vi]);

			// Pack triangle indices (4 bytes per uint32)
			const uint8_t* tris = &mlTriangles[s.triangle_offset];
			uint32_t triIndexCount = s.triangle_count * 3;
			uint32_t packedCount = (triIndexCount + 3) / 4;
			for (uint32_t ti = 0; ti < packedCount; ++ti) {
				uint32_t packed = 0;
				for (uint32_t b2 = 0; b2 < 4; ++b2) {
					uint32_t idx = ti * 4 + b2;
					if (idx < triIndexCount)
						packed |= (uint32_t)tris[idx] << (b2 * 8);
				}
				r.meshletData.push_back(packed);
			}

			r.meshlets.push_back(ml);
			r.meshletLocalDataOffset.push_back(localDataOffset);
		}
	};

	// Build LOD 0 meshlets
	uint32_t lod0MeshletOffset = 0;
	uint32_t lod0MeshletCount  = 0;
	buildMeshletsLocal(optIndices.data(), optIndices.size(),
	                   lod0MeshletOffset, lod0MeshletCount);

	r.lodLocal[0].localIndexOffset   = localIndexOffset;
	r.lodLocal[0].localMeshletOffset = lod0MeshletOffset;
	r.mesh.lods[0].indexOffset   = localIndexOffset; // rebased later
	r.mesh.lods[0].indexCount    = (uint32_t)optIndices.size();
	r.mesh.lods[0].meshletOffset = lod0MeshletOffset; // rebased later
	r.mesh.lods[0].meshletCount  = lod0MeshletCount;
	r.mesh.lods[0].error         = 0.0f;

	// Generate simplified LOD
	{
		std::vector<uint32_t> lodIndices = optIndices;

		for (uint32_t lod = 1; lod < 8; ++lod) {
			size_t targetIndexCount = (lodIndices.size() / 2 / 3) * 3;
			if (targetIndexCount < 3 * 3) break;

			float lodError = 0.0f;
			std::vector<uint32_t> simplified(lodIndices.size());
			size_t simplifiedCount = meshopt_simplify(
				simplified.data(), lodIndices.data(), lodIndices.size(),
				optPositions.data(), vertCount, sizeof(float) * 3,
				targetIndexCount, 0.02f, 0, &lodError);

			if (simplifiedCount == lodIndices.size()) break;
			simplified.resize(simplifiedCount);

			meshopt_optimizeVertexCache(simplified.data(), simplified.data(),
			                            simplifiedCount, vertCount);

			uint32_t localLodIndexOffset = (uint32_t)r.indices.size();
			r.indices.insert(r.indices.end(), simplified.begin(), simplified.end());

			uint32_t lodMeshletOffset = 0;
			uint32_t lodMeshletCount  = 0;
			buildMeshletsLocal(simplified.data(), simplifiedCount,
			                   lodMeshletOffset, lodMeshletCount);

			r.lodLocal[lod].localIndexOffset   = localLodIndexOffset;
			r.lodLocal[lod].localMeshletOffset = lodMeshletOffset;
			r.mesh.lods[lod].indexOffset   = localLodIndexOffset;
			r.mesh.lods[lod].indexCount    = (uint32_t)simplifiedCount;
			r.mesh.lods[lod].meshletOffset = lodMeshletOffset;
			r.mesh.lods[lod].meshletCount  = lodMeshletCount;
			r.mesh.lods[lod].error         = lodError;
			r.mesh.lodCount++;

			lodIndices = simplified;
		}
	}

	if (!prim.material) {
        printf("Primitive with no material! mesh primitive_index=%zu\n",
            &prim - data->meshes[0].primitives); // or whatever
	}

	// Material resolution
	// r.materialIndex = (uint32_t)std::max(0,
	// 	cgltf_util::indexOf(prim.material, data->materials, data->materials_count));

	if(prim.material){
	    int matid = cgltf_util::indexOf(prim.material, data->materials, data->materials_count);
		r.materialIndex = (uint32_t)(matid + 1);
	}else{
	    r.materialIndex = 0;
	}

	r.valid = true;
	return r;
}

} // anonymous namespace

bool loadGltf(const std::string& filepath, Scene& scene,
              size_t maxVerticesPerMeshlet,
              size_t maxTrianglesPerMeshlet)
{
	using namespace cgltf_util;

	cgltf_options options{};
	cgltf_data* data = nullptr;

	if (cgltf_parse_file(&options, filepath.c_str(), &data) != cgltf_result_success) {
		std::fprintf(stderr, "[gltf] Failed to parse: %s\n", filepath.c_str());
		return false;
	}

	if (cgltf_load_buffers(&options, data, filepath.c_str()) != cgltf_result_success) {
		std::fprintf(stderr, "[gltf] Failed to load buffers: %s\n", filepath.c_str());
		cgltf_free(data);
		return false;
	}

	scene.camera = {};
	for(cgltf_size i=0;i<data->nodes_count;i++){
	    const cgltf_node& node = data->nodes[i];
		if(!node.camera){
		    continue;
		}

		cgltf_float m[16];
		cgltf_node_transform_world(&node, m);
		glm::mat4 world = glm::make_mat4(m);

		scene.camera.position = glm::vec3(world[3]);

		glm::mat3 rot = glm::mat3(world);
		rot[0] = glm::normalize(rot[0]);
        rot[1] = glm::normalize(rot[1]);
        rot[2] = glm::normalize(rot[2]);
        scene.camera.orientation = glm::quat_cast(rot);

        const cgltf_camera* cam = node.camera;
        if(cam->type == cgltf_camera_type_perspective){
            scene.camera.fovY = cam->data.perspective.yfov;
            scene.camera.znear = cam->data.perspective.znear;
        }else{
            scene.camera.fovY = glm::radians(45.0f);
            scene.camera.znear = cam->data.orthographic.znear;
        }

        break;
	}

	scene.samplers.resize(data->samplers_count);
	for (cgltf_size i = 0; i < data->samplers_count; i++) {
		auto& src = data->samplers[i];
		auto& dst = scene.samplers[i];
		dst.magFilter = (int)src.mag_filter;
		dst.minFilter = (int)src.min_filter;
		dst.wrapS     = (int)src.wrap_s;
		dst.wrapT     = (int)src.wrap_t;
	}

	scene.lights.reserve(data->lights_count);

	for(cgltf_size i=0;i<data->nodes_count;i++){
	    const cgltf_node& node = data->nodes[i];
		if(!node.light){
		    continue;
		}

		float worldMatrix[16];
		cgltf_node_transform_world(&node, worldMatrix);

		vec3 position(worldMatrix[12],worldMatrix[13],worldMatrix[14]);
		vec3 direction = glm::normalize(vec3(-worldMatrix[8],-worldMatrix[9],-worldMatrix[10]));

		Light newLight{};
		const cgltf_light* light = node.light;

		cgltf_light_type ltype = light->type;

		float innerCone = 1.f;
        float outerCone = 1.f;
        float spotCosOuter = 1.f;
        float spotCosInner = 1.f;
		if(ltype == cgltf_light_type_spot){
		    innerCone = light->spot_inner_cone_angle;
			outerCone = light->spot_outer_cone_angle;
			spotCosOuter = glm::cos(outerCone);
			spotCosInner = glm::cos(innerCone);
		}

		newLight.position = position;
		newLight.color[0] = light->color[0];
		newLight.color[1] = light->color[1];
		newLight.color[2] = light->color[2];
		newLight.intensity = light->intensity;
		newLight.range = light->range;
		newLight.type = ltype;
		newLight.spotCosOuter = spotCosOuter;
		newLight.spotCosInner = spotCosInner;
		newLight.direction = direction;
		newLight.radius = 0.13;

		scene.lights.push_back(newLight);
	}

	scene.textures.resize(data->images_count);
	for (cgltf_size i = 0; i < data->images_count; i++) {
		auto& src = data->images[i];
		auto& dst = scene.textures[i];
		dst.uri             = src.uri ? src.uri : "";
		dst.mimeType        = src.mime_type ? src.mime_type : "";
		dst.name            = src.name ? src.name : "";
		dst.bufferViewIndex = indexOf(src.buffer_view,
		                              data->buffer_views,
		                              data->buffer_views_count);
		if (src.buffer_view) {
			const cgltf_buffer_view* bv = src.buffer_view;
			if (bv->buffer && bv->buffer->data) {
				const uint8_t* ptr = (const uint8_t*)bv->buffer->data + bv->offset;
				dst.data.assign(ptr, ptr + bv->size);
			} else {
				std::fprintf(stderr, "[gltf] Image %zu: buffer view has no data\n", i);
			}
		}
	}

	auto resolveTexture = [&](const cgltf_texture_view& view) -> int {
		if (!view.texture) return -1;
		int texIdx = indexOf(view.texture, data->textures, data->textures_count);
		if (texIdx < 0) return -1;
		const cgltf_texture& tex = data->textures[texIdx];
		return indexOf(tex.image, data->images, data->images_count);
	};

	auto markSrgb = [&](const cgltf_texture_view& view){
	    if(!view.texture || !view.texture->image){
			return;
		}
		size_t id = view.texture->image-data->images;
		scene.textureCSpaces[id] = ColorSpace::Srgb;
	};

	scene.textureCSpaces.resize(data->images_count, ColorSpace::Linear);
	scene.materials.resize(data->materials_count+1);
	scene.materials[0].albedoTexture = -1;
	scene.materials[0].normalTexture = -1;
	scene.materials[0].specularTexture = -1;
	scene.materials[0].emissiveTexture = -1;
	scene.materials[0].diffuseFactor = vec4(1.0f);
	scene.materials[0].specularFactor = vec4(0.04f, 0.04f, 0.04f, 0.5f);
	scene.materials[0].emissiveFactor = vec3(0.0f);
	scene.materials[0].alphaMode = 0;

	for (cgltf_size i = 0; i < data->materials_count; i++) {
		const auto& src = data->materials[i];
		auto& dst       = scene.materials[i+1];

		dst.albedoTexture   = -1;
		dst.normalTexture   = -1;
		dst.specularTexture = -1;
		dst.emissiveTexture = -1;
		dst.diffuseFactor   = vec4(1.0f);
		dst.specularFactor  = vec4(0.0f, 0.0f, 0.0f, 1.0f);
		dst.emissiveFactor  = vec3(0.0f);
		dst.alphaMode         = 0;

		if (src.has_pbr_metallic_roughness) {
			const auto& pbr = src.pbr_metallic_roughness;
			markSrgb(pbr.base_color_texture);

			dst.diffuseFactor = vec4(
				pbr.base_color_factor[0], pbr.base_color_factor[1],
				pbr.base_color_factor[2], pbr.base_color_factor[3]);

			float metallic = pbr.metallic_factor;
			dst.specularFactor = vec4(
				0.04f + metallic * 0.96f,
				0.04f + metallic * 0.96f,
				0.04f + metallic * 0.96f,
				pbr.roughness_factor);

			dst.albedoTexture   = resolveTexture(pbr.base_color_texture);
			dst.specularTexture = resolveTexture(pbr.metallic_roughness_texture);
		}

		markSrgb(src.emissive_texture);

		dst.normalTexture   = resolveTexture(src.normal_texture);
		dst.emissiveTexture = resolveTexture(src.emissive_texture);
		dst.emissiveFactor  = vec3(
			src.emissive_factor[0],
			src.emissive_factor[1],
			src.emissive_factor[2]);
		dst.alphaMode = src.alpha_mode;
	}

	std::vector<PrimitiveJob> jobs;
	{
		size_t total = 0;
		for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
			total += data->meshes[mi].primitives_count;
		jobs.reserve(total);

		for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
            const auto& mesh = data->meshes[mi];
            if (mesh.name && std::strcmp(mesh.name, "master_material") == 0) {
                continue;  // skip 3ds Max material-keepalive helper
            }
			for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count; ++pi) {
				jobs.push_back({mi, pi, jobs.size()});
			}
		}
	}

	std::vector<PrimitiveResult> results(jobs.size());

	std::for_each(std::execution::par, jobs.begin(), jobs.end(),
		[&](const PrimitiveJob& job) {
			const cgltf_primitive& prim =
				data->meshes[job.meshIdx].primitives[job.primIdx];

			if (!prim.material) {
            const char* meshName = data->meshes[job.meshIdx].name
                ? data->meshes[job.meshIdx].name : "(unnamed)";
            fprintf(stderr, "NULL MATERIAL: mesh[%zu] '%s' primitive[%zu]\n",
                    job.meshIdx, meshName, job.primIdx);
            fflush(stderr);
        }
			results[job.resultIdx] = processPrimitive(
				prim, data,
				maxVerticesPerMeshlet, maxTrianglesPerMeshlet);
		});

	{
		auto& globalVerts       = scene.geometry.vertices;
		auto& globalIndices     = scene.geometry.indices;
		auto& globalMeshlets    = scene.geometry.meshlets;
		auto& globalMeshletData = scene.geometry.meshletData;

		// Reserve up front to avoid N reallocations during the merge.
		size_t sumV = 0, sumI = 0, sumM = 0, sumD = 0;
		for (const auto& r : results) {
			if (!r.valid) continue;
			sumV += r.vertices.size();
			sumI += r.indices.size();
			sumM += r.meshlets.size();
			sumD += r.meshletData.size();
		}
		globalVerts.reserve(globalVerts.size() + sumV);
		globalIndices.reserve(globalIndices.size() + sumI);
		globalMeshlets.reserve(globalMeshlets.size() + sumM);
		globalMeshletData.reserve(globalMeshletData.size() + sumD);
		scene.meshes.reserve(scene.meshes.size() + results.size());
		scene.draws.reserve(scene.draws.size() + results.size());

		// Track the first draw emitted for each glTF mesh, so the node
		// transform pass below can patch the right draws.
		std::vector<uint32_t> meshToFirstDraw(data->meshes_count, UINT32_MAX);

		for (size_t j = 0; j < jobs.size(); ++j) {
			const PrimitiveJob&   job = jobs[j];
			const PrimitiveResult& r  = results[j];

			if (!r.valid) {
				continue;
			}

			uint32_t vBase = (uint32_t)globalVerts.size();
			uint32_t iBase = (uint32_t)globalIndices.size();
			uint32_t mBase = (uint32_t)globalMeshlets.size();
			uint32_t dBase = (uint32_t)globalMeshletData.size();

			// Append vertex / index / meshlet-data
			globalVerts.insert(globalVerts.end(),
				r.vertices.begin(), r.vertices.end());
			globalIndices.insert(globalIndices.end(),
				r.indices.begin(), r.indices.end());
			globalMeshletData.insert(globalMeshletData.end(),
				r.meshletData.begin(), r.meshletData.end());

			// Append meshlets with rebased offsets.
			for (size_t k = 0; k < r.meshlets.size(); ++k) {
				Meshlet ml = r.meshlets[k];
				ml.baseVertex = vBase;
				ml.dataOffset = r.meshletLocalDataOffset[k] + dBase;
				globalMeshlets.push_back(ml);
			}

			// Build final mesh record with rebased LOD offsets
			Mesh mesh = r.mesh;
			mesh.vertexOffset = vBase;
			for (uint32_t l = 0; l < mesh.lodCount; ++l) {
				mesh.lods[l].indexOffset   = r.lodLocal[l].localIndexOffset   + iBase;
				mesh.lods[l].meshletOffset = r.lodLocal[l].localMeshletOffset + mBase;
			}

			uint32_t meshIndex = (uint32_t)scene.meshes.size();
			scene.meshes.push_back(mesh);

			// Emit one draw per primitive
			MeshDraw draw{};
			draw.position                = vec3(0.0f);
			draw.scale                   = 1.0f;
			draw.orientation             = quat(1.0f, 0.0f, 0.0f, 0.0f);
			draw.meshIndex               = meshIndex;
			draw.meshletVisibilityOffset = 0;
			draw.postPass                = 0;
			draw.materialIndex           = r.materialIndex;

			uint32_t drawIndex = (uint32_t)scene.draws.size();
			scene.draws.push_back(draw);

			if (meshToFirstDraw[job.meshIdx] == UINT32_MAX)
				meshToFirstDraw[job.meshIdx] = drawIndex;
		}

		for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
			const auto& node = data->nodes[ni];
			if (!node.mesh) continue;

			int mi = indexOf(node.mesh, data->meshes, data->meshes_count);
			if (mi < 0 || meshToFirstDraw[mi] == UINT32_MAX) continue;

			float matrix[16];
			cgltf_node_transform_world(&node, matrix);

			vec3 translation(matrix[12], matrix[13], matrix[14]);

			vec3 scaleX(matrix[0], matrix[1], matrix[2]);
			vec3 scaleY(matrix[4], matrix[5], matrix[6]);
			vec3 scaleZ(matrix[8], matrix[9], matrix[10]);
			float uniformScale = glm::length(scaleX);

			glm::mat3 rotMat(
				scaleX / uniformScale,
				scaleY / uniformScale,
				scaleZ / uniformScale);
			quat rotation = glm::quat_cast(rotMat);

			// Count how many valid primitives this mesh produced.
			uint32_t validPrims = 0;
			for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
				// Find the job for (mi, pi) jobs are in meshIdx-major order
				(void)pi;
			}
			(void)validPrims;

			uint32_t firstDraw = meshToFirstDraw[mi];
			// Primitives of this mesh occupy consecutive draw slots, but only
			// the valid ones were emitted. Count how many valid results exist
			// between the first draw of this mesh and the next mesh's first
			// draw.
			uint32_t nextFirst = UINT32_MAX;
			for (cgltf_size k = (cgltf_size)mi + 1; k < data->meshes_count; ++k) {
				if (meshToFirstDraw[k] != UINT32_MAX) {
					nextFirst = meshToFirstDraw[k];
					break;
				}
			}
			uint32_t lastDraw = (nextFirst == UINT32_MAX)
				? (uint32_t)scene.draws.size()
				: nextFirst;

			for (uint32_t di = firstDraw; di < lastDraw; ++di) {
				scene.draws[di].position    = translation;
				scene.draws[di].scale       = uniformScale;
				scene.draws[di].orientation = rotation;
			}
		}
	}

	cgltf_free(data);
	return true;
}

void printSceneSummary(const Scene& scene)
{
	std::printf("=== Scene Summary ===\n");
	std::printf("  Meshes:    %zu\n", scene.meshes.size());
	std::printf("  Materials: %zu\n", scene.materials.size());
	std::printf("  Draws:     %zu\n", scene.draws.size());
	std::printf("  Textures:  %zu\n", scene.textures.size());
	std::printf("\n");
	std::printf("  Geometry:\n");
	std::printf("    Vertices:     %zu  (%.1f KB)\n",
	            scene.geometry.vertices.size(),
	            scene.geometry.vertices.size() * sizeof(Vertex) / 1024.0f);
	std::printf("    Indices:      %zu  (%.1f KB)\n",
	            scene.geometry.indices.size(),
	            scene.geometry.indices.size() * sizeof(uint32_t) / 1024.0f);
	std::printf("    Meshlets:     %zu  (%.1f KB)\n",
	            scene.geometry.meshlets.size(),
	            scene.geometry.meshlets.size() * sizeof(Meshlet) / 1024.0f);
	std::printf("    MeshletData:  %zu  (%.1f KB)\n",
	            scene.geometry.meshletData.size(),
	            scene.geometry.meshletData.size() * sizeof(uint32_t) / 1024.0f);
	std::printf("\n");

	for (size_t i = 0; i < scene.meshes.size(); i++) {
		const auto& m = scene.meshes[i];
		std::printf("  Mesh[%zu]  verts=%u  center=(%.2f,%.2f,%.2f)  radius=%.3f  lods=%u\n",
		            i, m.vertexCount, m.center.x, m.center.y, m.center.z,
		            m.radius, m.lodCount);
		for (uint32_t l = 0; l < m.lodCount; ++l) {
			const auto& lod = m.lods[l];
			std::printf("    LOD%u  tris=%u  meshlets=%u  error=%.4f\n",
			            l, lod.indexCount / 3, lod.meshletCount, lod.error);
		}
	}

	for (size_t i = 0; i < scene.materials.size(); i++) {
		const auto& mt = scene.materials[i];
		std::printf("  Material[%zu]  albedo=%d  normal=%d  spec=%d  emissive=%d\n",
		            i, mt.albedoTexture, mt.normalTexture,
		            mt.specularTexture, mt.emissiveTexture);
		std::printf("    diffuse=(%.2f,%.2f,%.2f,%.2f)  roughness=%.2f\n",
		            mt.diffuseFactor.x, mt.diffuseFactor.y,
		            mt.diffuseFactor.z, mt.diffuseFactor.w,
		            mt.specularFactor.w);
	}

	for (size_t i = 0; i < scene.draws.size(); i++) {
		const auto& d = scene.draws[i];
		std::printf("  Draw[%zu]  mesh=%u  mat=%u  pos=(%.2f,%.2f,%.2f)  scale=%.3f\n",
		            i, d.meshIndex, d.materialIndex,
		            d.position.x, d.position.y, d.position.z, d.scale);
	}

	for (size_t i = 0; i < scene.lights.size(); i++) {
    const auto& l = scene.lights[i];
    std::printf("   Light[%zu] pos=(%.2f,%.2f,%.2f) intensity=%.2f "
                "color=(%.2f,%.2f,%.2f) type=%d inner_cos=%.2f "
                "outer_cos=%.2f range=%.2f\n",
        i,
        l.position.x, l.position.y, l.position.z,
        l.intensity,
        l.color[0], l.color[1], l.color[2],
        (int)l.type,
        l.spotCosInner, l.spotCosOuter, l.range);
	}
}
