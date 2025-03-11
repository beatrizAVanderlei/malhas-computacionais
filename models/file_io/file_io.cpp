#include "file_io.h"
#include "file_readers.h"
#include "file_writers.h"
#include "../utils/string_utils.h"

#include <stdexcept>
#include <iostream>
#include <ostream>

namespace fileio {

    MeshData read_file(const std::string &filename) {
        std::string ext = string_utils::get_extension(filename);
        if(ext == ".off") {
            return read_file_off(filename);
        } else if(ext == ".obj") {
            return read_file_obj(filename);
        } else if(ext == ".stl") {
            return read_file_stl(filename);
        } else if(ext == ".vtk") {
            return read_file_vtk(filename);
        } else {
            throw std::invalid_argument("Formato de arquivo não suportado: " + ext);
        }
    }

    void save_file(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces) {
        std::string fixedFilename = string_utils::fix_filename(filename);
        std::string ext = string_utils::get_extension(fixedFilename);
        // Para depuração:
        std::cout << "Extensão extraída: \"" << ext << "\"" << std::endl;
        if (ext == ".off") {
            save_file_off(fixedFilename, vertices, faces);
        } else if (ext == ".obj") {
            save_file_obj(fixedFilename, vertices, faces);
        } else if (ext == ".stl") {
            save_file_stl(fixedFilename, vertices, faces);
        } else if (ext == ".vtk") {
            save_file_vtk(fixedFilename, vertices, faces);
        } else {
            throw std::invalid_argument("Formato de arquivo não suportado: " + ext);
        }
    }

} // namespace fileio
