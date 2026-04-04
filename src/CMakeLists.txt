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
#
# Se BACKEND não for definido, GL é usado em Linux e DX11 em Windows.
# =============================================================================
if(NOT DEFINED BACKEND)
    if(WIN32)
        set(BACKEND "DX11" CACHE STRING "Backend gráfico (GL|DX11)")
    else()
        set(BACKEND "GL"  CACHE STRING "Backend gráfico (GL|DX11)")
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

else()
    message(FATAL_ERROR "BACKEND inválido: '${BACKEND}'. Use GL, DX11 .")
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