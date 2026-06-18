#pragma once

#include <cgltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;

enum class ColorSpace : uint8_t {Linear, Srgb};

struct Vertex
{
	uint16_t vx, vy, vz;
	uint16_t tp; // packed tangent: 8-8 octahedral
	uint32_t np; // packed normal: 10-10-10-2 vector + bitangent sign
	uint16_t tu, tv;
};

struct MeshLod
{
	uint32_t indexOffset;
	uint32_t indexCount;
	uint32_t meshletOffset;
	uint32_t meshletCount;
	float error;
};

struct alignas(16) Mesh
{
	vec3 center;
	float radius;
	uint32_t vertexOffset;
	uint32_t vertexCount;
	uint32_t ommIndexData; // 30-bit offset, 2-bit format
	uint32_t ommIndexBase;
	uint32_t lodCount;
	uint32_t padding[3];
	MeshLod lods[8];
};

struct Meshlet
{
	float center[3];
	float radius;
	int8_t cone_axis[3];
	int8_t cone_cutoff;
	uint32_t dataOffset;
	uint32_t baseVertex;
	uint8_t vertexCount;
	uint8_t triangleCount;
	uint8_t shortRefs;
	uint8_t padding;
};

struct alignas(16) Material{
    int albedoTexture    = -1;
    int normalTexture    = -1;
    int specularTexture  = -1;
    int emissiveTexture  = -1;
    vec4 diffuseFactor   = vec4(1.0f);
    vec4 specularFactor  = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    vec3 emissiveFactor  = vec3(0.0f);
    uint32_t alphaMode     = 0;
};

struct alignas(16) MeshDraw
{
	vec3 position;
	float scale;
	quat orientation;
	uint32_t meshIndex;
	uint32_t meshletVisibilityOffset;
	uint32_t postPass;
	uint32_t materialIndex;
};

struct TextureInfo
{
	std::string uri;
	std::string mimeType;
	std::string name;
	int bufferViewIndex = -1;
	std::vector<uint8_t> data;
};

struct SamplerInfo
{
	int magFilter = 9729;
	int minFilter = 9987;
	int wrapS     = 10497;
	int wrapT     = 10497;
};

struct Geometry
{
	std::vector<Vertex>   vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet>  meshlets;
	std::vector<uint32_t> meshletData; // vertex indices + packed triangle indices
};

struct Camera
{
	vec3 position;
	quat orientation;
	float fovY;
	float znear;
};

struct Light{
    vec3 position;
    uint32_t  type;
    vec3  color;
    float intensity;
    vec3 direction;
    float  spotCosInner;
    float   spotCosOuter;
    float   range;
    float   _pad0;
    float   _pad1;
};

struct Scene
{
	Geometry                 geometry;
	Camera                   camera;
	std::vector<Mesh>        meshes;
	std::vector<Material>    materials;
	std::vector<MeshDraw>    draws;
	std::vector<TextureInfo> textures;
	std::vector<SamplerInfo> samplers;
	std::vector<Light>       lights;
	std::vector<ColorSpace> textureCSpaces;
};


/// Load a .gltf or .glb file into a Scene. Returns true on success.
/// maxVerticesPerMeshlet / maxTrianglesPerMeshlet control meshlet sizing.
bool loadGltf(const std::string& filepath, Scene& scene,
              size_t maxVerticesPerMeshlet  = 64,
              size_t maxTrianglesPerMeshlet = 124);

/// Print a summary of the loaded scene to stdout.
void printSceneSummary(const Scene& scene);
