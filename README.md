# **Lycaon**
<img src="https://github.com/Meepst/Lycaon/blob/main/screenshots/sample1.png" alt="Sample Image 1" width="30%"> <img src="https://github.com/Meepst/Lycaon/blob/main/screenshots/sample2.png" alt="Sample Image 2" width="30%">

A real-time Vulkan 1.4 renderer written in C++20, featuring a GPU-driven mesh shading pipeline and ReSTIR-based direct lighting with hardware ray tracing (ray queries).
 
## Features
 
- **Mesh shading geometry pipeline** — scenes are pre-processed into meshlets with [meshoptimizer](https://github.com/zeux/meshoptimizer) and rasterized through task (amplification) + mesh shaders instead of the classic vertex pipeline
- **Deferred shading** — a compact G-buffer (albedo/metallic, octahedral-encoded normals/roughness, emissive, depth) drives the lighting passes
- **ReSTIR direct lighting** — reservoir-based spatiotemporal importance resampling:
  - Initial light candidate generation per pixel
  - Temporal reuse with depth-based reprojection and history validation
  - Spatial reuse across randomized neighbor taps
  - Visibility evaluated with **Vulkan ray queries** (`VK_KHR_ray_query` + `VK_KHR_acceleration_structure`)
- **PBR shading** with ACES filmic tonemapping and sRGB output
- **glTF 2.0 scene loading** via cgltf (Intel Sponza and Barramundi Fish sample assets included)
- **DDS skybox** loading with BC decoding via bcdec
- **Bindless-style resources** using descriptor indexing and SPIRV-Reflect–driven descriptor setup
- **HLSL shaders** (Shader Model 6.x) compiled to SPIR-V with DXC at build time
- **Nuklear immediate-mode UI** overlay with frame statistics
- **PNG screenshots** via fpng
- Optional Tracy profiler integration (commented out in `CMakeLists.txt`)
## Requirements
 
- A GPU + driver with support for:
  - Vulkan 1.4
  - Mesh shaders (`VK_EXT_mesh_shader`)
  - Ray queries (`VK_KHR_ray_query`, `VK_KHR_acceleration_structure`)
- [Vulkan SDK](https://vulkan.lunarg.com/) (the `VULKAN_SDK` environment variable must be set; DXC is located via `$VULKAN_SDK/Bin`)
- CMake 3.14+
- A C++20 compiler (MSVC 2022 is the primary target)
- [Ninja](https://ninja-build.org/) (used by the provided Windows build script)
## Building
 
Clone with submodules — all third-party dependencies live in `dependencies/`:
 
```sh
git clone --recursive https://github.com/Meepst/Lycaon.git
cd Lycaon
```
 
(If you already cloned without `--recursive`, run `git submodule update --init --recursive`.)
 
### Windows (MSVC)
 
```bat
build_msvc.bat
```
 
This loads the VS2022 environment, configures CMake with the Ninja Multi-Config generator, and builds the `Debug` configuration into `build/`. Pass `clean` to wipe the build directory first:
 
```bat
build_msvc.bat clean
```
 
### Manual CMake
 
```sh
cmake -B build -G "Ninja Multi-Config"
cmake --build build --config Release
```
 
Shaders in `src/shaders/` are compiled to SPIR-V automatically as part of the build (output to `build/<Config>/spirv/`).
 
## Running
 
Run the executable from the repository root so the `assets/` folder is found:
 
```sh
build/Debug/Lycaon.exe
```
 
By default it loads the [Sponza scene from intel](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html) and the included sky. You can override both:

***Note: Currently only supports textures in the DDS format***

```sh
Lycaon.exe --scene assets/barramundiFish/glTF/BarramundiFish.gltf --skybox assets/skyboxes/11zon_aristea_wreck_puresky_4k.dds
```
 
A bare (non-flag) argument is also treated as a scene path.
 
### Controls
 
| Input | Action |
|---|---|
| `W` `A` `S` `D` | Move camera |
| Mouse | Look around |
| `T` | Toggle UI overlay |
| `F` | Save screenshot (to `screenshots/`) |
| `Esc` | Quit |
 
## Project Structure
 
```
src/
  main.cpp          # Vulkan setup, render loop, pipelines, UI
  scene.cpp/.h      # glTF loading, meshlet building, acceleration structures
  resources.cpp/.h  # Buffer/image helpers (VMA)
  texture.cpp/.h    # Texture / DDS loading
  swapchain.cpp/.h  # Swapchain management
  shaders/
    task.task.hlsl      # Task (amplification) shader
    mesh.mesh.hlsl      # Mesh shader
    pixel.frag.hlsl     # G-buffer fill
    generate.comp.hlsl  # ReSTIR candidate generation + temporal reuse
    shading.comp.hlsl   # ReSTIR spatial reuse + final shading
    restir.hlsli        # Reservoir/ReSTIR core, BRDF, RNG helpers
    UI/                 # Nuklear UI shaders
dependencies/       # Git submodules (glfw, glm, VMA, volk, vk-bootstrap,
                    # meshoptimizer, cgltf, SPIRV-Reflect, nuklear, fpng,
                    # bcdec, stb)
assets/             # Sample scenes (Sponza, Barramundi Fish) and skyboxes
```
 
## Dependencies
 
All vendored as submodules or headers under `dependencies/`:
 
[GLFW](https://github.com/glfw/glfw) · [GLM](https://github.com/g-truc/glm) · [volk](https://github.com/zeux/volk) · [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) · [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) · [meshoptimizer](https://github.com/zeux/meshoptimizer) · [cgltf](https://github.com/jkuhlmann/cgltf) · [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) · [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) · [fpng](https://github.com/richgel999/fpng) · [bcdec](https://github.com/iOrange/bcdec) · [stb](https://github.com/nothings/stb)
 
## References
 
- Bitterli et al., [*Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting*](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct) (ReSTIR)
- [Vulkan mesh shading](https://www.khronos.org/blog/mesh-shading-for-vulkan) (`VK_EXT_mesh_shader`)
- Zeux, [Niagara Renderer](https://github.com/zeux/niagara)
