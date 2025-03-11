#ifndef FILE_READERS_H
#define FILE_READERS_H

#include <string>

namespace fileio {
    // Forward declaration de MeshData (já definida em file_io.h)
    struct MeshData;

    MeshData read_file_off(const std::string &filename);
    MeshData read_file_obj(const std::string &filename);
    MeshData read_file_stl(const std::string &filename);
    MeshData read_file_vtk(const std::string &filename);
}

#endif // FILE_READERS_H
