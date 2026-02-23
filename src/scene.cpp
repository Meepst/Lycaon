#include "scene.h"
#include <cgltf.h>
#include <iterator>
#include <unordered_map>
#include <iostream>

bool parseScene(const char* path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<Material>& materials,
    std::vector<std::string>& texturePaths){
    cgltf_data *data = nullptr;
    cgltf_options options{};
    cgltf_result result = cgltf_parse_file(&options, path, &data);

    if(result != cgltf_result_success){
        cgltf_free(data);
        return false;
    }

    result = cgltf_load_buffers(&options, data, path);
    if(result != cgltf_result_success){
        cgltf_free(data);
        return false;
    }

    int textureOffset = 1+int(texturePaths.size());
    for(int i=0;i<data->materials_count;i++){
        const cgltf_material& material = data->materials[i];
        Material mat{};

        mat.diffuseFactor = glm::vec4(1.0f);
        if(material.has_pbr_specular_glossiness){
            if(material.pbr_specular_glossiness.diffuse_texture.texture){
                mat.albedoTexture = textureOffset+int(cgltf_texture_index(data,material.pbr_specular_glossiness.diffuse_texture.texture));
            }
            mat.diffuseFactor = glm::vec4(material.pbr_specular_glossiness.diffuse_factor[0],material.pbr_specular_glossiness.diffuse_factor[1],material.pbr_specular_glossiness.diffuse_factor[2],material.pbr_specular_glossiness.diffuse_factor[3]);

            if(material.pbr_specular_glossiness.specular_glossiness_texture.texture){
                mat.specularTexture = textureOffset+int(cgltf_texture_index(data, material.pbr_specular_glossiness.specular_glossiness_texture.texture));
            }
            mat.specularFactor = glm::vec4(material.pbr_specular_glossiness.specular_factor[0], material.pbr_specular_glossiness.specular_factor[1], material.pbr_specular_glossiness.specular_factor[2], material.pbr_specular_glossiness.glossiness_factor);
        }else if(material.has_pbr_metallic_roughness){
            if(material.pbr_metallic_roughness.metallic_roughness_texture.texture){
                mat.specularTexture = textureOffset+int(cgltf_texture_index(data, material.pbr_metallic_roughness.metallic_roughness_texture.texture));
            }
            mat.specularFactor = glm::vec4(1.0f,1.0f,1.0f,1.0f-material.pbr_metallic_roughness.roughness_factor);

            if(material.pbr_metallic_roughness.base_color_texture.texture){
                mat.albedoTexture = textureOffset+int(cgltf_texture_index(data, material.pbr_metallic_roughness.base_color_texture.texture));
            }
            mat.diffuseFactor = glm::vec4(material.pbr_metallic_roughness.base_color_factor[0],material.pbr_metallic_roughness.base_color_factor[1],material.pbr_metallic_roughness.base_color_factor[2],material.pbr_metallic_roughness.base_color_factor[3]);
        }
        if(material.normal_texture.texture){
            mat.normalTexture = textureOffset+int(cgltf_texture_index(data,material.normal_texture.texture));
        }
        if(material.emissive_texture.texture){
            mat.emissiveTexture = textureOffset+int(cgltf_texture_index(data, material.emissive_texture.texture));
        }

        mat.emissiveFactor = glm::vec3(material.emissive_factor[0],material.emissive_factor[1],material.emissive_factor[2]);

        materials.push_back(mat);
    }

    const cgltf_mesh& mesh = data->meshes[0];
    for(uint32_t i=0;i<mesh.primitives_count;i++){
        const cgltf_primitive& primitive = mesh.primitives[i];

        uint64_t vertexCount = 0;
        std::vector<float> scratch;
        const cgltf_accessor* positionPtr = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        assert(cgltf_num_components(positionPtr->type)==3);
        vertexCount = positionPtr->count;
        vertices.resize(vertexCount);
        scratch.resize(vertexCount*3);

        cgltf_accessor_unpack_floats(positionPtr, scratch.data(), vertexCount*3);

        for(size_t v=0;v<vertexCount;v++){
            vertices[v].position = glm::vec3(scratch[v*3+0],scratch[v*3+1],scratch[v*3+2]);
        }

        const cgltf_accessor* normalPtr = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        assert(cgltf_num_components(normalPtr->type)==3);
        cgltf_accessor_unpack_floats(normalPtr, scratch.data(), vertexCount*3);

        for(size_t n=0;n<vertexCount;n++){
            vertices[n].normal=glm::vec3(scratch[n*3+0],scratch[n*3+1],scratch[n*3+2]);
        }

        const cgltf_accessor* uvPtr = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
        assert(cgltf_num_components(uvPtr->type)==2);
        cgltf_accessor_unpack_floats(uvPtr, scratch.data(), vertexCount*2);

        for(size_t t=0;t<vertexCount;t++){
            vertices[t].uv = glm::vec2(scratch[t*2+0],scratch[t*2+1]);
        }

        indices.resize(primitive.indices->count);
        cgltf_accessor_unpack_indices(primitive.indices, indices.data(), 4, indices.size());

    }

    for(size_t i=0;i<data->textures_count;i++){
        const cgltf_texture& texture = data->textures[i];
        cgltf_image* image = texture.image;

        std::string ipath = path;
        std::string::size_type pos = ipath.find_last_of("/\\");
        if(pos==std::string::npos){
            ipath = "";
        }else{
            ipath = ipath.substr(0,pos+1);
        }

        std::string uri = image->uri;
        uri.resize(cgltf_decode_uri(&uri[0]));

        texturePaths.push_back(ipath+uri);
    }

    cgltf_free(data);
    return true;
}
