cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_DEBUG DEBUG)
endif()

project(Lycaon)

file(GLOB_RECURSE CPP_SOURCE_FILES "src/*.h" "src/*.cpp")
file(GLOB_RECURSE GLSL_SOURCE_FILES "src/shaders/*.glsl")
file(GLOB_RECURSE GLSL_HEADER_FILES "src/shaders/*.h")

add_executable(Lycaon ${CPP_SOURCE_FILES})

set_target_properties(Lycaon PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

set_target_properties(Lycaon PROPERTIES ENVIRONMENT "DISABLE_VULKAN_OBS_CAPTURE=1")

target_compile_definitions(Lycaon PRIVATE GLFW_INCLUDE_VULKAN GLM_FORCE_XYZW_ONLY GLM_FORCE_QUAT_DATA_XYZW GLM_FORCE_QUAT_CTOR_XYZW)
target_include_directories(Lycaon PRIVATE
     dependencies/glm dependencies/cgltf dependencies/stb "$ENV{VULKAN_SDK}/Include")
target_link_directories(Lycaon PRIVATE "$ENV{VULKAN_SDK}/Lib")

if(WIN32)
    target_compile_definitions(Lycaon PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
    target_compile_definitions(Lycaon PRIVATE WIN32_LEAN_and_MEAN NOMINMAX)
endif()

add_subdirectory(dependencies/vk-bootstrap)
target_link_libraries(Lycaon PRIVATE vk-bootstrap::vk-bootstrap)

add_subdirectory(dependencies/vma)
target_link_libraries(Lycaon PRIVATE VulkanMemoryAllocator)

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

if(UNIX)
    if(DEFINED ENV{VULKAN_SDK})
        set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/bin/glslangValidator")
    else()
        set(GLSL_VALIDATOR "glslangValidator")
    endif()
elseif(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
    set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
else()
    set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
endif()



set(SPIRV_OUTPUT_DIR "${PROJECT_BINARY_DIR}/$<CONFIG>/spirv/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>")
foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(STEM ${GLSL} NAME_WLE)
    set(SPIRV "${SPIRV_OUTPUT_DIR}${STEM}.spv")
    add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_OUTPUT_DIR}"
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL_FLAGS} --quiet ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL} ${GLSL_HEADER_FILES})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(Lycaon compile_shaders)
