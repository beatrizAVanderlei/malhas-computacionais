cmake_minimum_required(VERSION 3.10)
project(teste)

set(CMAKE_CXX_STANDARD 17)

# 🔹 Se estiver usando vcpkg, define o toolchain
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# 🔹 Encontrar bibliotecas necessárias
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# 🔹 Encontrar FreeGLUT manualmente
find_library(FREEGLUT_LIBRARY NAMES freeglut PATHS
        "C:/vcpkg/installed/x64-windows/lib"
        "C:/msys64/mingw64/lib"
)
include_directories(
        "C:/vcpkg/installed/x64-windows/include"
        "C:/msys64/mingw64/include"
)

# 🔹 Diretórios de inclusão do projeto
include_directories(${PROJECT_SOURCE_DIR}/models)
include_directories(${PROJECT_SOURCE_DIR}/render)

# 🔹 Adicionar tinyfiledialogs como biblioteca
add_library(tinyfiledialogs libs/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${PROJECT_SOURCE_DIR}/libs)

# 🔹 Adicionar os arquivos de código-fonte do projeto
add_executable(teste
        src/main.cpp
        models/object/object.cpp
        render/render.cpp
        render/controls.cpp
        models/object/ObjectRendering.cpp
        models/object/ObjectPicking.cpp
        models/object/ObjectEditing.cpp
        utils/string_utils.cpp
        utils/math_utils.cpp
        models/file_io/file_readers.cpp
        models/file_io/file_writers.cpp
        models/file_io/file_io.cpp
        src/performance.cpp
        src/performance-no-prep.cpp
        src/performance-no-prep.h
)

# 🔹 Vincular as bibliotecas
target_link_libraries(teste
        OpenGL::GL
        GLEW::GLEW
        glfw
        ${FREEGLUT_LIBRARY}
        glu32
        opengl32
        tinyfiledialogs
)
