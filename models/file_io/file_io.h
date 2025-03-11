#ifndef FILE_IO_H
#define FILE_IO_H

#include <string>
#include <vector>
#include <array>

#include "file_readers.h"
#include "file_writers.h"

// Colocamos a estrutura MeshData dentro do namespace fileio
namespace fileio {

    // Estrutura para armazenar os dados da malha
    struct MeshData {
        // Para leitura, usamos double; você pode converter para float se necessário
        std::vector<std::array<double, 3>> vertices;
        std::vector<std::vector<int>> faces;
        std::vector<int> faceCells;
    };

    // Funções públicas de leitura e gravação
    MeshData read_file(const std::string &filename);

    // Na gravação, usamos vértices como float; se necessário, converta os dados de MeshData.
    void save_file(const std::string &filename,
                   const std::vector<std::array<float, 3>> &vertices,
                   const std::vector<std::vector<unsigned int>> &faces);

} // namespace fileio

// Inclui os headers públicos dos módulos de leitura e gravação

#endif // FILE_IO_H
