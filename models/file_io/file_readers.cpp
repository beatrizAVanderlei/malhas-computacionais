#include "file_readers.h"
#include "../utils/string_utils.h"
#include "../utils/math_utils.h"
#include "file_io.h" // Para MeshData
#include <fstream>
#include <sstream>
#include <iterator>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <iostream>

namespace fileio {

MeshData read_file_off(const std::string &filename) {
    MeshData data;
    std::ifstream infile(filename);
    if (!infile.is_open())
        throw std::runtime_error("Arquivo não encontrado: " + filename);
    std::string line;
    if(!std::getline(infile, line))
        throw std::runtime_error("Erro ao ler o arquivo OFF.");
    if(line != "OFF")
        throw std::runtime_error("O arquivo não está no formato OFF.");
    if(!std::getline(infile, line))
        throw std::runtime_error("Formato OFF inválido.");
    auto headerParts = string_utils::split(line);
    if(headerParts.size() < 3)
        throw std::runtime_error("Formato OFF inválido.");
    int n_vertices = std::stoi(headerParts[0]);
    int n_faces = std::stoi(headerParts[1]);
    for (int i = 0; i < n_vertices; ++i) {
        if(!std::getline(infile, line))
            throw std::runtime_error("Número insuficiente de vértices no arquivo OFF.");
        auto tokens = string_utils::split(line);
        if(tokens.size() < 3)
            throw std::runtime_error("Coordenadas insuficientes para vértice no OFF.");
        std::array<double,3> vertex = { std::stod(tokens[0]), std::stod(tokens[1]), std::stod(tokens[2]) };
        data.vertices.push_back(vertex);
    }
    for (int i = 0; i < n_faces; ++i) {
        if(!std::getline(infile, line))
            throw std::runtime_error("Número insuficiente de faces no arquivo OFF.");
        auto tokens = string_utils::split(line);
        if(tokens.empty())
            continue;
        int num_verts = std::stoi(tokens[0]);
        if(tokens.size() - 1 < static_cast<size_t>(num_verts))
            throw std::runtime_error("Número de índices não corresponde ao esperado em OFF.");
        std::vector<int> face;
        for (int j = 0; j < num_verts; ++j) {
            face.push_back(std::stoi(tokens[j+1]));
        }
        data.faces.push_back(face);
    }
    return data;
}

MeshData read_file_obj(const std::string &filename) {
    MeshData data;
    std::ifstream infile(filename);
    if (!infile.is_open())
        throw std::runtime_error("Arquivo não encontrado: " + filename);
    std::string line;
    int current_cell_id = 0;
    while (std::getline(infile, line)) {
        if(line.empty() || line[0] == '#')
            continue;
        auto tokens = string_utils::split(line);
        if(tokens.empty())
            continue;
        if(tokens[0] == "o" || tokens[0] == "g") {
            current_cell_id++;
        } else if(tokens[0] == "v") {
            if(tokens.size() < 4)
                throw std::runtime_error("Vértice com coordenadas insuficientes no OBJ.");
            std::array<double,3> vertex = { std::stod(tokens[1]), std::stod(tokens[2]), std::stod(tokens[3]) };
            data.vertices.push_back(vertex);
        } else if(tokens[0] == "f") {
            std::vector<int> face;
            for (size_t i = 1; i < tokens.size(); ++i) {
                size_t pos = tokens[i].find('/');
                std::string indexStr = (pos != std::string::npos) ? tokens[i].substr(0, pos) : tokens[i];
                int idx = std::stoi(indexStr) - 1; // OBJ é 1-indexado
                face.push_back(idx);
            }
            data.faces.push_back(face);
            data.faceCells.push_back(current_cell_id);
        }
    }
    return data;
}

MeshData read_file_stl(const std::string &filename) {
    MeshData data;
    std::ifstream infile(filename);
    if (!infile.is_open())
        throw std::runtime_error("Arquivo não encontrado: " + filename);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(infile, line)) {
        lines.push_back(line);
    }
    auto find_vertex = [&data](const std::array<double,3> &v) -> int {
        for (size_t i = 0; i < data.vertices.size(); ++i) {
            if (data.vertices[i] == v)
                return static_cast<int>(i);
        }
        return -1;
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string trimmed = lines[i];
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](int ch) {
            return !std::isspace(ch);
        }));
        if(string_utils::starts_with(trimmed, "solid") || string_utils::starts_with(trimmed, "endsolid"))
            continue;
        if(string_utils::starts_with(trimmed, "facet normal")) {
            if(i + 4 >= lines.size())
                throw std::runtime_error("Formato STL inválido.");
            std::vector<int> faceIndices;
            for (int j = 0; j < 3; ++j) {
                std::string vertexLine = lines[i + 2 + j];
                auto tokens = string_utils::split(vertexLine);
                if(tokens.empty() || tokens[0] != "vertex")
                    throw std::runtime_error("Formato STL inválido na linha de vértice.");
                if(tokens.size() < 4)
                    throw std::runtime_error("Coordenadas insuficientes em vértice STL.");
                std::array<double,3> vertex = { std::stod(tokens[1]), std::stod(tokens[2]), std::stod(tokens[3]) };
                int index = find_vertex(vertex);
                if(index == -1) {
                    data.vertices.push_back(vertex);
                    index = static_cast<int>(data.vertices.size()) - 1;
                }
                faceIndices.push_back(index);
            }
            data.faces.push_back(faceIndices);
        }
    }
    return data;
}

// Função que lê um arquivo VTK e retorna os dados da malha em uma estrutura MeshData.
MeshData read_file_vtk(const std::string &filename) {
    MeshData data;  // Cria uma instância de MeshData para armazenar os vértices e faces lidas.

    std::ifstream infile(filename);  // Abre o arquivo cujo nome foi passado como parâmetro para leitura.
    if (!infile.is_open())  // Verifica se o arquivo foi aberto com sucesso.
        throw std::runtime_error("Arquivo não encontrado: " + filename);  // Se não conseguiu abrir, lança uma exceção com mensagem de erro.

    std::string line;  // Variável que armazenará cada linha lida do arquivo.
    std::string mode; // Variável que indicará o modo atual de leitura: "POINTS" (para vértices) ou "CONNECTIVITY" (para conectividade/faces).

    int n_points = 0;         // Número total de pontos (vértices) que serão lidos, conforme especificado no arquivo.
    int points_count = 0;     // Contador para acompanhar quantos pontos já foram lidos.
    int n_connectivity = 0;   // Número total de células/conectividades (faces) que serão lidas.
    int connectivity_count = 0; // Contador para acompanhar quantas células já foram lidas.
    int total_indices = 0;    // Número total de índices (usado na seção de conectividade, embora nem sempre seja utilizado diretamente).

    // Loop que percorre o arquivo linha a linha.
    while (std::getline(infile, line)) {
        if(line.empty())           // Se a linha estiver vazia,
            continue;              // pula para a próxima iteração.

        if(line[0] == '#')         // Se a linha começar com '#' (comentário),
            continue;              // pula para a próxima iteração.

        // Converte a linha para letras maiúsculas para facilitar comparações (não sensíveis a caixa).
        std::string upper_line = string_utils::to_upper(line);

        // Divide a linha em partes (tokens) usando espaços como delimitadores.
        auto parts = string_utils::split(line);

        // Se a linha indica a seção DATASET, ela é ignorada.
        if(string_utils::starts_with(upper_line, "DATASET")) {
            continue;
        }
        // Se a linha indica o início da seção POINTS:
        else if(string_utils::starts_with(upper_line, "POINTS")) {
            if(parts.size() < 3)  // Verifica se a linha possui pelo menos 3 partes (palavra-chave, número de pontos e tipo de dados).
                throw std::runtime_error("Formato VTK inválido na linha de POINTS.");  // Se não, lança uma exceção.

            mode = "POINTS";                   // Define o modo atual para leitura de pontos.
            n_points = std::stoi(parts[1]);      // Converte a segunda parte (número de pontos) para inteiro.
            points_count = 0;                  // Reseta o contador de pontos lidos.
            continue;                        // Pula para a próxima iteração, pois esta linha é apenas o cabeçalho da seção.
        }
        // Se a linha indica o início da seção POLYGONS ou CELLS (que contêm informações de conectividade):
        else if(string_utils::starts_with(upper_line, "POLYGONS") || string_utils::starts_with(upper_line, "CELLS")) {
            if(parts.size() < 3)  // Verifica se a linha possui pelo menos 3 partes (palavra-chave, número de células e total de índices).
                throw std::runtime_error("Formato VTK inválido na linha de POLYGONS/CELLS.");  // Se não, lança uma exceção.

            mode = "CONNECTIVITY";             // Define o modo atual para leitura de conectividade (faces).
            n_connectivity = std::stoi(parts[1]);  // Converte a segunda parte para inteiro: número de células (faces).
            total_indices = std::stoi(parts[2]);   // Converte a terceira parte para inteiro: total de índices (útil para validação ou informações adicionais).
            connectivity_count = 0;            // Reseta o contador de células lidas.
            continue;                        // Pula para a próxima iteração, pois esta linha é apenas o cabeçalho da seção.
        }
        else {  // Para todas as outras linhas que não são cabeçalhos de seção:
            if(mode == "POINTS") {  // Se o modo atual é de leitura de pontos:
                if(points_count < n_points) {  // Se ainda não foram lidos todos os pontos esperados:
                    // Divide a linha em tokens para obter as coordenadas.
                    auto tokens = string_utils::split(line);
                    if(tokens.size() < 3)  // Verifica se foram encontradas pelo menos 3 coordenadas (x, y, z).
                        throw std::runtime_error("São necessárias ao menos 3 coordenadas por ponto.");  // Se não, lança uma exceção.

                    // Converte os tokens para double e armazena em um array representando um vértice.
                    std::array<double,3> vertex = { std::stod(tokens[0]), std::stod(tokens[1]), std::stod(tokens[2]) };
                    data.vertices.push_back(vertex);  // Adiciona o vértice à lista de vértices dos dados da malha.
                    points_count++;  // Incrementa o contador de pontos lidos.
                }
                continue;  // Pula para a próxima iteração, pois a linha foi processada.
            } else if(mode == "CONNECTIVITY") {  // Se o modo atual é de leitura de conectividade:
                if(connectivity_count < n_connectivity) {  // Se ainda não foram lidas todas as células esperadas:
                    auto tokens = string_utils::split(line);  // Divide a linha em tokens para extrair os índices.
                    if(tokens.empty())  // Se não houver tokens,
                        continue;     // pula para a próxima iteração.

                    // O primeiro token indica o número de vértices que formam a célula.
                    int num_verts = std::stoi(tokens[0]);

                    // Verifica se o número de índices fornecido (tokens sem o primeiro) é igual ao esperado.
                    if(tokens.size() - 1 != static_cast<size_t>(num_verts))
                        throw std::runtime_error("Número de índices não corresponde ao esperado.");  // Se não corresponder, lança uma exceção.

                    std::vector<int> face;  // Cria um vetor para armazenar os índices dos vértices que compõem a célula.
                    // Itera pelos tokens restantes (índices) e os converte para inteiro.
                    for (size_t i = 1; i < tokens.size(); i++) {
                        face.push_back(std::stoi(tokens[i]));
                    }
                    data.faces.push_back(face);  // Adiciona a célula (face) lida à lista de faces dos dados da malha.
                    data.faceCells.push_back(0);  // Adiciona um valor 0 à lista faceCells (pode servir de marcador ou placeholder para informações futuras).
                    connectivity_count++;  // Incrementa o contador de células lidas.
                }
                continue;  // Pula para a próxima iteração, pois a linha foi processada.
            }
        }
    }
    return data;  // Após processar todas as linhas, retorna os dados da malha com vértices e faces preenchidos.
}

} // namespace fileio
