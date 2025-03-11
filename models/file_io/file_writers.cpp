#include "file_writers.h"
#include "../utils/string_utils.h"
#include "../utils/math_utils.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace fileio {

void save_file_off(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces) {
    std::ofstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Erro ao abrir o arquivo para escrita: " + filename);
    file << "OFF\n";
    file << vertices.size() << " " << faces.size() << " 0\n";
    for (const auto &v : vertices) {
        file << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (const auto &face : faces) {
        file << face.size();
        for (unsigned int idx : face) {
            file << " " << idx;
        }
        file << "\n";
    }
}

void save_file_obj(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces) {
    std::ofstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Erro ao abrir o arquivo para escrita: " + filename);
    for (const auto &v : vertices) {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (const auto &face : faces) {
        file << "f";
        for (unsigned int idx : face) {
            file << " " << (idx + 1); // OBJ é 1-indexado
        }
        file << "\n";
    }
}

void save_file_stl(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces) {
    std::ofstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Erro ao abrir o arquivo para escrita: " + filename);
    file << "solid model\n";
    for (const auto &face : faces) {
        std::vector<std::array<unsigned int, 3>> triangles;
        if (face.size() == 3) {
            triangles.push_back({ face[0], face[1], face[2] });
        } else if (face.size() > 3) {
            for (size_t i = 1; i < face.size() - 1; ++i) {
                triangles.push_back({ face[0], face[i], face[i+1] });
            }
        }
        for (const auto &tri : triangles) {
            const auto &v1 = vertices[tri[0]];
            const auto &v2 = vertices[tri[1]];
            const auto &v3 = vertices[tri[2]];
            auto normal = math_utils::calculate_normal(v1, v2, v3);
            file << "  facet normal " << normal[0] << " " << normal[1] << " " << normal[2] << "\n";
            file << "    outer loop\n";
            file << "      vertex " << v1[0] << " " << v1[1] << " " << v1[2] << "\n";
            file << "      vertex " << v2[0] << " " << v2[1] << " " << v2[2] << "\n";
            file << "      vertex " << v3[0] << " " << v3[1] << " " << v3[2] << "\n";
            file << "    endloop\n";
            file << "  endfacet\n";
        }
    }
    file << "endsolid model\n";
}

void save_file_vtk(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces) {
    std::ofstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Erro ao abrir o arquivo para escrita: " + filename);
    file << "# vtk DataFile Version 3.0\n";
    file << "Arquivo gerado pelo programa de computação gráfica\n";
    file << "ASCII\n";
    file << "DATASET POLYDATA\n";
    file << "POINTS " << vertices.size() << " float\n";
    for (const auto &v : vertices) {
        file << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    int total_indices = 0;
    for (const auto &face : faces) {
        total_indices += face.size() + 1;
    }
    file << "CELLS " << faces.size() << " " << total_indices << "\n";
    for (const auto &face : faces) {
        file << face.size();
        for (unsigned int idx : face) {
            file << " " << idx;
        }
        file << "\n";
    }
}

} // namespace fileio
