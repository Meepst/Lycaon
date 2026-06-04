cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(_DEBUG DEBUG)
endif()

project(Lycaon)

file(GLOB_RECURSE CPP_SOURCE_FILES "src/*.h" "src/*.cpp")
file(GLOB_RECURSE HLSL_SOURCE_FILES "src/shaders/*.hlsl")
file(GLOB_RECURSE HLSL_HEADER_FILES "src/shaders/*.hlsli")

add_executable(Lycaon ${CPP_SOURCE_FILES})

set_target_properties(Lycaon PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

set_target_properties(Lycaon PROPERTIES ENVIRONMENT "DISABLE_VULKAN_OBS_CAPTURE=1")

target_compile_definitions(Lycaon PRIVATE GLFW_INCLUDE_VULKAN GLM_FORCE_XYZW_ONLY GLM_FORCE_QUAT_DATA_XYZW GLM_FORCE_QUAT_CTOR_XYZW GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(Lycaon PRIVATE
     dependencies/glm dependencies/cgltf dependencies/stb dependencies/bcdec "$ENV{VULKAN_SDK}/Include")
target_link_directories(Lycaon PRIVATE "$ENV{VULKAN_SDK}/Lib")

if(WIN32)
  target_compile_definitions(Lycaon PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
  target_compile_definitions(Lycaon PRIVATE WIN32_LEAN_and_MEAN NOMINMAX)
endif()

add_subdirectory(dependencies/vk-bootstrap)
target_link_libraries(Lycaon PRIVATE vk-bootstrap::vk-bootstrap)

add_subdirectory(dependencies/vma)
target_link_libraries(Lycaon PRIVATE VulkanMemoryAllocator)

add_subdirectory(dependencies/volk)
target_link_libraries(Lycaon PRIVATE volk_headers)

add_subdirectory(dependencies/meshoptimizer)
target_link_libraries(Lycaon PRIVATE meshoptimizer)

set(KTX_FEATURE_TESTS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_STATIC_LIB OFF  CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD  ON  CACHE BOOL "" FORCE)
add_subdirectory(dependencies/KTX-Software)
target_link_libraries(Lycaon PRIVATE ktx)

set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
add_subdirectory(dependencies/SPIRV-Reflect)
target_link_libraries(Lycaon PRIVATE spirv-reflect-static)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(dependencies/glfw)

if(WIN32)
  target_link_libraries(Lycaon PRIVATE glfw vulkan-1)
else()
  target_link_libraries(Lycaon PRIVATE glfw vulkan)
endif()

target_include_directories(Lycaon PRIVATE include dependencies/SPIRV-Headers)


add_custom_command(TARGET Lycaon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:Lycaon>
        $<TARGET_RUNTIME_DLLS:Lycaon>
    COMMAND_EXPAND_LISTS)

find_program(DXC dxc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED)


set(SPIRV_OUTPUT_DIR "${PROJECT_BINARY_DIR}/$<CONFIG>/spirv/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>")

function(get_hlsl_profile FILENAME OUT_PROFILE OUT_ENTRY)
  get_filename_component(NAME_ONLY ${FILENAME} NAME)
  if(NAME_ONLY MATCHES "\\.vert\\.")
    set(${OUT_PROFILE} "vs_6_6" PARENT_SCOPE)
    set(${OUT_ENTRY} "main" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.frag\\." OR NAME_ONLY MATCHES "\\.pixel\\.")
    set(${OUT_PROFILE} "ps_6_6" PARENT_SCOPE)
    set(${OUT_ENTRY} "main" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.mesh\\.")
    set(${OUT_PROFILE} "ms_6_5" PARENT_SCOPE)
    set(${OUT_ENTRY} "main" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.comp\\.")
    set(${OUT_PROFILE} "cs_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.geom\\.")
    set(${OUT_PROFILE} "gs_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.tesc\\.")
    set(${OUT_PROFILE} "hs_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.tese\\.")
    set(${OUT_PROFILE} "ds_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.task\\.")
    set(${OUT_PROFILE} "as_6_5" PARENT_SCOPE)
    set(${OUT_ENTRY} "main" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rgen\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rmiss\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rchit\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rahit\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rint\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  elseif(NAME_ONLY MATCHES "\\.rcall\\.")
    set(${OUT_PROFILE} "lib_6_6" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "Cannot determine shader profile for: ${FILENAME}")
  endif()
endfunction()

foreach(HLSL ${HLSL_SOURCE_FILES})
  get_filename_component(STEM ${HLSL} NAME_WLE)
  set(SPIRV "${SPIRV_OUTPUT_DIR}/${STEM}.spv")
  get_hlsl_profile(${HLSL} PROFILE ENTRY)

  if(PROFILE STREQUAL "lib_6_6")
    set(DXC_ENTRY_FLAG "")
  else()
    set(DXC_ENTRY_FLAG -E ${ENTRY})
  endif()

  add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_OUTPUT_DIR}"
        COMMAND ${DXC} -spirv -HV 2021 -Zi -Qembed_debug -fspv-extension=SPV_EXT_mesh_shader
        -fspv-extension=SPV_EXT_descriptor_indexing -fspv-debug=vulkan-with-source
        -fspv-extension=SPV_KHR_physical_storage_buffer -fspv-target-env=vulkan1.3
        -T ${PROFILE} ${DXC_ENTRY_FLAG} ${HLSL} -Fo ${SPIRV}
        DEPENDS ${HLSL} ${HLSL_HEADER_FILES})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(Lycaon compile_shaders)
