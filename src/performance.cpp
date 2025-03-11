#include "performance.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "../models/object/Object.h"

using Clock = std::chrono::high_resolution_clock;

// ======================================================================
// Estrutura de hash para std::pair<unsigned int, unsigned int>
// ======================================================================

struct PairHash {
    std::size_t operator()(const std::pair<unsigned int, unsigned int>& p) const {
        auto h1 = std::hash<unsigned int>{}(p.first);
        auto h2 = std::hash<unsigned int>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// ======================================================================
// Funções de medição de vizinhança
// ======================================================================

// Retorna os índices das faces que contêm o vértice v_index.

std::vector<int> getVertexFaces(const object::Object& obj, int v_index) {
    std::vector<int> foundFaces;
    const auto& faces = obj.getFaces();
    foundFaces.reserve(faces.size());
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
        const auto& face = faces[i];
        if (std::find(face.begin(), face.end(), static_cast<unsigned int>(v_index)) != face.end()) {
            foundFaces.push_back(i);
        }
    }
    return foundFaces;
}

// Retorna os vértices adjacentes a v_index (utilizando as arestas).

std::vector<unsigned int> getVertexAdjacent(const object::Object& obj, int v_index) {
    std::unordered_set<unsigned int> neighbors;
    const auto& edges = obj.getEdges();
    for (const auto& edge : edges) {
        if (edge.first == static_cast<unsigned int>(v_index))
            neighbors.insert(edge.second);
        else if (edge.second == static_cast<unsigned int>(v_index))
            neighbors.insert(edge.first);
    }
    return std::vector<unsigned int>(neighbors.begin(), neighbors.end());
}

// ======================================================================
// Mapeamentos pré-computados para otimizar o acesso
// ======================================================================

// Calcula o mapeamento de cada vértice para as faces às quais ele pertence.

std::vector<std::vector<int>> computeVertexToFaces(const object::Object& obj) {
    const auto& faces = obj.getFaces();
    int numVertices = obj.getVertices().size();
    std::vector<std::vector<int>> mapping(numVertices);
    for (int f = 0; f < static_cast<int>(faces.size()); ++f) {
        for (unsigned int v : faces[f]) {
            mapping[v].push_back(f);
        }
    }
    return mapping;
}

// Calcula o mapeamento de cada face para as faces adjacentes.
// Utiliza um mapeamento de arestas para identificar rapidamente quais faces compartilham uma aresta.

std::vector<std::vector<int>> computeFaceAdjacency(const object::Object& obj) {
    const auto& faces = obj.getFaces();
    int numFaces = faces.size();
    std::vector<std::vector<int>> faceAdj(numFaces);
    std::unordered_map<std::pair<unsigned int, unsigned int>, std::vector<int>, PairHash> edgeToFaces;

    // Para cada face, insira todas as arestas (ordenadas) no mapa.
    for (int f = 0; f < numFaces; ++f) {
        const auto& face = faces[f];
        int n = face.size();
        for (int i = 0; i < n; ++i) {
            unsigned int a = face[i];
            unsigned int b = face[(i+1) % n];
            if (a > b) std::swap(a, b);
            std::pair<unsigned int, unsigned int> edge = {a, b};
            edgeToFaces[edge].push_back(f);
        }
    }

    // Para cada face, obtenha todas as faces que compartilham alguma aresta.
    for (int f = 0; f < numFaces; ++f) {
        std::unordered_set<int> adjSet;
        const auto& face = faces[f];
        int n = face.size();
        for (int i = 0; i < n; ++i) {
            unsigned int a = face[i];
            unsigned int b = face[(i+1) % n];
            if (a > b) std::swap(a, b);
            std::pair<unsigned int, unsigned int> edge = {a, b};
            const auto& faceList = edgeToFaces[edge];
            for (int other : faceList) {
                if (other != f) {
                    adjSet.insert(other);
                }
            }
        }
        faceAdj[f] = std::vector<int>(adjSet.begin(), adjSet.end());
    }
    return faceAdj;
}

// ======================================================================
// Funções auxiliares para o cálculo de estatísticas
// ======================================================================

double computeMean(const std::vector<double>& values) {
    if (values.empty()) return 0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double computeStdDev(const std::vector<double>& values, double mean) {
    if (values.size() <= 1) return 0;
    double accum = 0;
    for (double v : values)
        accum += (v - mean) * (v - mean);
    return std::sqrt(accum / (values.size() - 1));
}

double computeMeanInt(const std::vector<int>& values) {
    if (values.empty()) return 0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

// ======================================================================
// Função que exporta os dados de desempenho para um arquivo CSV
// ======================================================================

void exportPerformanceData(const object::Object& obj, const std::string &outputFile) {
    auto startTotal = Clock::now();

    // Obter referências locais
    const auto& vertices = obj.getVertices();
    const auto& faces = obj.getFaces();
    int numVertices = vertices.size();
    int numFaces = faces.size();

    // Pré-calcular mapeamento para acelerar as consultas
    auto vertexToFaces = computeVertexToFaces(obj);
    auto faceAdjacency = computeFaceAdjacency(obj);

    // Vetores para armazenar os dados de cada vértice:
    std::vector<double> timeVertexFaces(numVertices, 0);    // Tempo para acessar as faces do vértice (usando o mapeamento)
    std::vector<int> numVertexFaces(numVertices, 0);          // Número de faces que o vértice pertence
    std::vector<double> timeVertexAdjacent(numVertices, 0);   // Tempo para acessar os vértices vizinhos
    std::vector<int> numVertexAdjacent(numVertices, 0);       // Número de vértices vizinhos

    // Vetores para armazenar os dados de cada face:
    std::vector<double> timeAccessFaceVertices(numFaces, 0);  // Tempo para acessar os vértices da face (simples acesso)
    std::vector<int> numFaceVertices(numFaces, 0);            // Número de vértices na face
    std::vector<double> timeFaceAdjacent(numFaces, 0);        // Tempo para acessar as faces vizinhas (usando o mapeamento)
    std::vector<int> numFaceAdjacent(numFaces, 0);            // Número de faces vizinhas

    // Processa vértices em paralelo
    #pragma omp parallel for schedule(static)
    for (int v = 0; v < numVertices; ++v) {
        auto t1 = Clock::now();
        auto facesOfVertex = vertexToFaces[v];
        auto t2 = Clock::now();
        timeVertexFaces[v] = std::chrono::duration<double>(t2 - t1).count();
        numVertexFaces[v] = facesOfVertex.size();

        t1 = Clock::now();
        auto adjacentVertices = getVertexAdjacent(obj, v);
        t2 = Clock::now();
        timeVertexAdjacent[v] = std::chrono::duration<double>(t2 - t1).count();
        numVertexAdjacent[v] = adjacentVertices.size();
    }
    std::cout << "PROCESSAMOS OS VERTICES" << std::endl;

    // Processa faces em paralelo
    #pragma omp parallel for schedule(static)
    for (int f = 0; f < numFaces; ++f) {
        auto t1 = Clock::now();
        const auto& faceVertices = faces[f];
        auto t2 = Clock::now();
        timeAccessFaceVertices[f] = std::chrono::duration<double>(t2 - t1).count();
        numFaceVertices[f] = faceVertices.size();

        t1 = Clock::now();
        auto adjacentFaces = faceAdjacency[f];
        t2 = Clock::now();
        timeFaceAdjacent[f] = std::chrono::duration<double>(t2 - t1).count();
        numFaceAdjacent[f] = adjacentFaces.size();
    }
    std::cout << "PROCESSAMOS AS FACES" << std::endl;

    auto endTotal = Clock::now();
    double totalTime = std::chrono::duration<double>(endTotal - startTotal).count();

    std::ofstream fout(outputFile);
    if (!fout.is_open()) {
        std::cerr << "Erro ao abrir o arquivo " << outputFile << std::endl;
        return;
    }

    fout << "Tipo,Index,TempoFaces,NumFaces,TempoAdjacentes,NumAdjacentes\n";

    // Dados dos vértices
    for (int v = 0; v < numVertices; ++v) {
        fout << "v," << v << "," << timeVertexFaces[v] << "," << numVertexFaces[v] << ","
             << timeVertexAdjacent[v] << "," << numVertexAdjacent[v] << "\n";
    }

    // Dados das faces
    for (int f = 0; f < numFaces; ++f) {
        fout << "f," << f << "," << timeAccessFaceVertices[f] << "," << numFaceVertices[f] << ","
             << timeFaceAdjacent[f] << "," << numFaceAdjacent[f] << "\n";
    }

    // Tempo total de execução
    fout << "total,," << totalTime << ",\n";

    fout.close();
}