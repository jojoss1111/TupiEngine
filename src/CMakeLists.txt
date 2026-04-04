cmake_minimum_required(VERSION 3.16)
project(Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Seleção de backend gráfico
#
# Passe -DBACKEND=<valor> ao invocar cmake:
#   cmake -DBACKEND=GL  ..    → OpenGL 2.1 + X11    (padrão em Linux)
#   cmake -DBACKEND=DX11 ..   → Direct3D 11          (somente Windows)
#   cmake -DBACKEND=VK ..     → Vulkan                (Linux / Windows)
#
# Se BACKEND não for definido, GL é usado em Linux e DX11 em Windows.
# =============================================================================
if(NOT DEFINED BACKEND)
    if(WIN32)
        set(BACKEND "DX11" CACHE STRING "Backend gráfico (GL|DX11|VK)")
    else()
        set(BACKEND "GL"  CACHE STRING "Backend gráfico (GL|DX11|VK)")
    endif()
endif()

string(TOUPPER "${BACKEND}" BACKEND_UPPER)
message(STATUS "Engine backend: ${BACKEND_UPPER}")

# =============================================================================
# Fontes comuns — compiladas em todos os backends
# =============================================================================
set(ENGINE_SRCS Engine_renderer.cpp)

find_package(Threads REQUIRED)

# =============================================================================
# Backend: OpenGL 2.1 + X11
# =============================================================================
if(BACKEND_UPPER STREQUAL "GL")
    add_compile_definitions(ENGINE_BACKEND_GL)

    find_package(OpenGL REQUIRED)
    find_package(X11    REQUIRED)
    find_package(PNG    REQUIRED)

    set(ENGINE_LIBS
        OpenGL::GL
        ${X11_LIBRARIES}
        PNG::PNG
        Threads::Threads
    )

# =============================================================================
# Backend: Direct3D 11 (Windows apenas)
# =============================================================================
elseif(BACKEND_UPPER STREQUAL "DX11")
    if(NOT WIN32)
        message(FATAL_ERROR "Backend DX11 só é suportado no Windows.")
    endif()
    add_compile_definitions(ENGINE_BACKEND_DX11)
    list(APPEND ENGINE_SRCS RendererDX11.cpp)

    find_package(PNG REQUIRED)

    set(ENGINE_LIBS
        d3d11 dxgi d3dcompiler
        PNG::PNG
        Threads::Threads
    )

# =============================================================================
# Backend: Vulkan + SDL3
#
# Dependências necessárias:
#   Arch Linux:
#     sudo pacman -S vulkan-icd-loader vulkan-headers sdl3 glslang shaderc
#     + driver da GPU: vulkan-intel | vulkan-radeon | nvidia-utils
#   Ubuntu 24.04+:
#     sudo apt install libvulkan-dev libsdl3-dev glslang-dev libglslang-dev \
#                      spirv-tools shaderc
#   Windows:
#     Instale o Vulkan SDK (https://vulkan.lunarg.com) e SDL3 via vcpkg:
#       vcpkg install sdl3 glslang
#
# vk_mem_alloc.h (VMA):
#   Arquivo single-header da AMD GPUOpen (licença MIT).
#   Coloque-o em qualquer diretório do include path, por exemplo:
#     cp vk_mem_alloc.h <raiz_do_projeto>/
#   Download: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
#
# Shaders pré-compilados:
#   Execute ./compile_shaders.sh UMA VEZ para gerar shaders_embedded.hpp.
#   Depois commite o arquivo gerado — outros usuários não precisarão do glslc.
#   O glslang em runtime continua disponível APENAS para shader_create().
# =============================================================================
elseif(BACKEND_UPPER STREQUAL "VK")
    add_compile_definitions(ENGINE_BACKEND_VK)
    list(APPEND ENGINE_SRCS RendererVK.cpp)

    # --- Vulkan ---------------------------------------------------------------
    find_package(Vulkan REQUIRED)

    # --- SDL3 -----------------------------------------------------------------
    # Tenta primeiro via CMake config (instalação moderna do SDL3),
    # depois via pkg-config como fallback.
    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)
        # Cria target compatível para uso uniforme abaixo
        add_library(SDL3::SDL3 INTERFACE IMPORTED)
        target_include_directories(SDL3::SDL3 INTERFACE ${SDL3_INCLUDE_DIRS})
        target_link_libraries(SDL3::SDL3 INTERFACE ${SDL3_LIBRARIES})
        target_compile_options(SDL3::SDL3 INTERFACE ${SDL3_CFLAGS_OTHER})
    endif()

    # --- glslang (para shader_create em runtime) ------------------------------
    # Procura as libs com nomes que variam entre distros e versões.
    find_library(GLSLANG_LIB    NAMES glslang    glslang-default-resource-limits)
    find_library(SPIRV_LIB      NAMES SPIRV      SPIRVd)
    find_library(GLSLANG_OS_LIB NAMES MachineIndependent OSDependent glslang-default-resource-limits QUIET)

    if(NOT GLSLANG_LIB OR NOT SPIRV_LIB)
        message(WARNING
            "glslang ou SPIRV não encontrado — shader_create() não funcionará.\n"
            "  Arch: sudo pacman -S glslang\n"
            "  Ubuntu: sudo apt install libglslang-dev")
        set(GLSLANG_LIBS "")
    else()
        set(GLSLANG_LIBS ${GLSLANG_LIB} ${SPIRV_LIB})
        if(GLSLANG_OS_LIB)
            list(APPEND GLSLANG_LIBS ${GLSLANG_OS_LIB})
        endif()
        message(STATUS "glslang encontrado: ${GLSLANG_LIBS}")
    endif()

    # --- PNG (para carregamento de texturas) ----------------------------------
    find_package(PNG REQUIRED)

    # --- VMA (vk_mem_alloc.h — single-header, sem lib a linkar) --------------
    # Procura na raiz do projeto e no include path padrão do sistema.
    find_path(VMA_INCLUDE_DIR vk_mem_alloc.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            /usr/include
            /usr/local/include
    )
    if(NOT VMA_INCLUDE_DIR)
        message(FATAL_ERROR
            "vk_mem_alloc.h não encontrado.\n"
            "  Baixe em: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator\n"
            "  Coloque vk_mem_alloc.h na raiz do projeto ou em third_party/")
    endif()
    message(STATUS "VMA encontrado em: ${VMA_INCLUDE_DIR}")

    # --- Verifica shaders_embedded.hpp ---------------------------------------
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders_embedded.hpp")
        message(WARNING
            "shaders_embedded.hpp não encontrado!\n"
            "  Execute: ./compile_shaders.sh\n"
            "  Dependência: sudo pacman -S shaderc  (Arch) ou\n"
            "               sudo apt install glslang-tools  (Ubuntu)")
    endif()

    set(ENGINE_LIBS
        Vulkan::Vulkan
        SDL3::SDL3
        PNG::PNG
        Threads::Threads
        ${GLSLANG_LIBS}
    )

    # VMA é só header, só precisamos do include path
    list(APPEND ENGINE_EXTRA_INCLUDES ${VMA_INCLUDE_DIR})

else()
    message(FATAL_ERROR "BACKEND inválido: '${BACKEND}'. Use GL, DX11 ou VK.")
endif()

# =============================================================================
# Biblioteca da engine (compartilhada — .so/.dll)
#
# Shared em vez de static porque o FFI do LuaJIT carrega via dlopen().
# =============================================================================
add_library(engine SHARED ${ENGINE_SRCS})

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(DEFINED ENGINE_EXTRA_INCLUDES)
    target_include_directories(engine PRIVATE ${ENGINE_EXTRA_INCLUDES})
endif()

target_link_libraries(engine PUBLIC ${ENGINE_LIBS})

target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:   -Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>: -Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:  /W3 /wd4100>
)

# SSE4.1 para o path SIMD do RendererVK (sem custo em CPUs sem suporte)
if(BACKEND_UPPER STREQUAL "VK")
    target_compile_options(engine PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:   -msse4.1>
        $<$<CXX_COMPILER_ID:Clang>: -msse4.1>
    )
endif()

# =============================================================================
# Executável de exemplo (opcional)
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(game main.cpp)
    target_link_libraries(game PRIVATE engine)
    message(STATUS "Executável 'game' será construído a partir de main.cpp")
endif()

# =============================================================================
# Resumo do build
# =============================================================================
message(STATUS "")
message(STATUS "  Projeto   : Engine")
message(STATUS "  Backend   : ${BACKEND_UPPER}")
message(STATUS "  Padrão C++: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Fontes    : ${ENGINE_SRCS}")
message(STATUS "")