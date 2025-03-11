#ifndef FILE_WRITERS_H
#define FILE_WRITERS_H

#include "file_io.h"
#include <string>
#include <vector>
#include <array>

namespace fileio {

    void save_file_off(const std::string &filename,
                       const std::vector<std::array<float, 3>> &vertices,
                       const std::vector<std::vector<unsigned int>> &faces);

    void save_file_obj(const std::string &filename,
                       const std::vector<std::array<float, 3>> &vertices,
                       const std::vector<std::vector<unsigned int>> &faces);

    void save_file_stl(const std::string &filename,
                       const std::vector<std::array<float, 3>> &vertices,
                       const std::vector<std::vector<unsigned int>> &faces);

    void save_file_vtk(const std::string &filename,
                       const std::vector<std::array<float, 3>> &vertices,
                       const std::vector<std::vector<unsigned int>> &faces);

}

#endif
