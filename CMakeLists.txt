cmake_minimum_required(VERSION 3.10)
project(teste)

set(CMAKE_CXX_STANDARD 17)

# ==========================================
# 1. ENCONTRAR PACOTES E DEPENDÊNCIAS
# ==========================================

# 🔹 OpenMP (Essencial para performance do Path Tracing)
find_package(OpenMP REQUIRED)

# 🔹 Vcpkg Toolchain (se estiver usando)
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# 🔹 Bibliotecas Gráficas
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# 🔹 Encontrar FreeGLUT manualmente (Mantendo seus caminhos)
find_library(FREEGLUT_LIBRARY NAMES freeglut freeglut32 PATHS
        "C:/vcpkg/installed/x64-windows/lib"
        "C:/msys64/mingw64/lib"
)

# ==========================================
# 2. CONFIGURAÇÃO DE DIRETÓRIOS
# ==========================================

include_directories(
        "C:/vcpkg/installed/x64-windows/include"
        "C:/msys64/mingw64/include"
        ${PROJECT_SOURCE_DIR}/models
        ${PROJECT_SOURCE_DIR}/render
        ${PROJECT_SOURCE_DIR}/libs
)

# ==========================================
# 3. BIBLIOTECAS INTERNAS
# ==========================================

# 🔹 TinyFileDialogs
add_library(tinyfiledialogs libs/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${PROJECT_SOURCE_DIR}/libs)

# ==========================================
# 4. EXECUTÁVEL PRINCIPAL
# ==========================================

add_executable(teste
        src/main.cpp
        src/performance.cpp
        src/performance-no-prep.cpp
        src/performance-no-prep.h

        models/object/object.cpp
        models/object/ObjectRendering.cpp
        models/object/ObjectPicking.cpp
        models/object/ObjectEditing.cpp

        models/file_io/file_readers.cpp
        models/file_io/file_writers.cpp
        models/file_io/file_io.cpp

        render/render.cpp
        render/controls.cpp
        render/PathTracer.h  # Listado apenas uma vez agora

        utils/string_utils.cpp
        utils/math_utils.cpp
)

# ==========================================
# 5. LINKAGEM (Tudo em um lugar só)
# ==========================================

target_link_libraries(teste PRIVATE
        # Biblioteca de Paralelismo
        OpenMP::OpenMP_CXX

        # Bibliotecas Gráficas Modernas
        OpenGL::GL
        GLEW::GLEW
        glfw

        # Bibliotecas Gráficas Legado / Sistema
        ${FREEGLUT_LIBRARY}
        glu32
        opengl32

        # Bibliotecas Internas
        tinyfiledialogs
)