#include "performance-no-prep.h"
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
// (auxiliar para o cálculo ingênuo da adjacência de faces)
// ======================================================================
struct PairHash {
    std::size_t operator()(const std::pair<unsigned int, unsigned int>& p) const {
        auto h1 = std::hash<unsigned int>{}(p.first);
        auto h2 = std::hash<unsigned int>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// ======================================================================
// Estruturas de dados para agrupar métricas e evitar false sharing
// ======================================================================
/**
 * @brief Agrupa as métricas de desempenho de cada vértice, que antes estavam
 *        em quatro vetores separados (timeVertexFaces, numVertexFaces,
 *        timeVertexAdjacent, numVertexAdjacent).
 */
struct VertexPerfData {
    double timeFaces;       // tempo para acesso às faces do vértice
    int    numFaces;        // número de faces do vértice
    double timeAdjacent;    // tempo para acesso aos vértices adjacentes
    int    numAdjacent;     // número de vértices adjacentes
};

/**
 * @brief Agrupa as métricas de desempenho de cada face, que antes estavam
 *        em quatro vetores separados (timeAccessFaceVertices, numFaceVertices,
 *        timeFaceAdjacent, numFaceAdjacent).
 */
struct FacePerfData {
    double timeAccessVertices; // tempo para acessar os vértices de uma face
    int    numVertices;        // número de vértices nessa face
    double timeFaceAdjacent;   // tempo para acessar as faces adjacentes
    int    numFaceAdjacent;    // número de faces adjacentes
};

// ======================================================================
// Funções de medição de vizinhança (SEM PRÉ-PROCESSAMENTO) - inlined
// ======================================================================
static inline std::vector<int> getVertexFacesNoPrepInline(const object::Object& obj, int v_index) {
    const auto& faces = obj.getFaces();
    std::vector<int> foundFaces;
    foundFaces.reserve(faces.size());

    int nFaces = static_cast<int>(faces.size());
    for (int i = 0; i < nFaces; ++i) {
        const auto& face_i = faces[i];
        int faceSize = static_cast<int>(face_i.size());

        // Percorre manualmente a face em busca do v_index
        for (int pos = 0; pos < faceSize; ++pos) {
            if (face_i[pos] == static_cast<unsigned int>(v_index)) {
                foundFaces.push_back(i);
                break; // já encontramos, não precisa continuar
            }
        }
    }
    return foundFaces;
}

static inline std::vector<unsigned int> getVertexAdjacentNoPrepInline(const object::Object& obj, int v_index) {
    std::unordered_set<unsigned int> neighbors;
    const auto& edges = obj.getEdges();
    for (const auto& edge : edges) {
        if (edge.first == static_cast<unsigned int>(v_index)) {
            neighbors.insert(edge.second);
        } else if (edge.second == static_cast<unsigned int>(v_index)) {
            neighbors.insert(edge.first);
        }
    }
    return std::vector<unsigned int>(neighbors.begin(), neighbors.end());
}

static inline std::vector<int> getFaceAdjacentNoPrepInline(const object::Object& obj, int f_index) {
    std::vector<int> adjacentFaces;
    const auto& faces = obj.getFaces();
    const auto& thisFace = faces[f_index];

    // Monta as arestas da face atual em um set
    std::unordered_set<std::pair<unsigned int, unsigned int>, PairHash> edgesOfThisFace;
    edgesOfThisFace.reserve(thisFace.size());
    for (int i = 0; i < static_cast<int>(thisFace.size()); ++i) {
        unsigned int a = thisFace[i];
        unsigned int b = thisFace[(i + 1) % thisFace.size()];
        if (a > b) std::swap(a, b);
        edgesOfThisFace.insert({a, b});
    }

    // Percorre todas as outras faces e verifica se compartilham alguma aresta
    int totalFaces = static_cast<int>(faces.size());
    for (int j = 0; j < totalFaces; ++j) {
        if (j == f_index) continue;

        const auto& otherFace = faces[j];
        bool isAdjacent = false;

        for (int k = 0; k < static_cast<int>(otherFace.size()); ++k) {
            unsigned int a = otherFace[k];
            unsigned int b = otherFace[(k + 1) % otherFace.size()];
            if (a > b) std::swap(a, b);

            if (edgesOfThisFace.find({a, b}) != edgesOfThisFace.end()) {
                isAdjacent = true;
                break;
            }
        }
        if (isAdjacent) {
            adjacentFaces.push_back(j);
        }
    }

    return adjacentFaces;
}

// ======================================================================
// Funções auxiliares para estatísticas
// ======================================================================
double computeMeanNoPrep(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double computeStdDevNoPrep(const std::vector<double>& values, double mean) {
    if (values.size() <= 1) return 0.0;
    double accum = 0.0;
    for (double v : values) {
        double diff = (v - mean);
        accum += diff * diff;
    }
    return std::sqrt(accum / (values.size() - 1));
}

double computeMeanIntNoPrep(const std::vector<int>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

// ======================================================================
// Função principal: exporta dados de desempenho para um arquivo CSV
// (SEM QUALQUER PRÉ-PROCESSAMENTO DE MAPEAMENTOS)
// ======================================================================
void exportPerformanceDataNoPrep(const object::Object& obj, const std::string &outputFile) {
    auto startTotal = Clock::now();

    // Obter referências locais
    const auto& vertices = obj.getVertices();
    const auto& faces = obj.getFaces();
    int numVertices = static_cast<int>(vertices.size());
    int numFaces = static_cast<int>(faces.size());

    // ------------------------------------------------------------------
    // Em vez de 4 vetores para métricas de vértice, usamos 1 vetor de struct
    // ------------------------------------------------------------------
    std::vector<VertexPerfData> vertexPerf(numVertices);
    // Inicializa caso queira garantir zeros (opcional, mas válido):
    // for (auto &vp : vertexPerf) {
    //     vp.timeFaces = 0.0;
    //     vp.numFaces  = 0;
    //     vp.timeAdjacent = 0.0;
    //     vp.numAdjacent  = 0;
    // }

    // ------------------------------------------------------------------
    // Em vez de 4 vetores para métricas de face, usamos 1 vetor de struct
    // ------------------------------------------------------------------
    std::vector<FacePerfData> facePerf(numFaces);
    // for (auto &fp : facePerf) {
    //     fp.timeAccessVertices = 0.0;
    //     fp.numVertices        = 0;
    //     fp.timeFaceAdjacent   = 0.0;
    //     fp.numFaceAdjacent    = 0;
    // }

    // Processa vértices (paralelizado se disponível)
    #pragma omp parallel for schedule(static)
    for (int v = 0; v < numVertices; ++v) {
        // 1) Tempo para acessar as faces do vértice
        auto t1 = Clock::now();
        auto facesOfVertex = getVertexFacesNoPrepInline(obj, v);
        auto t2 = Clock::now();

        vertexPerf[v].timeFaces = std::chrono::duration<double>(t2 - t1).count();
        vertexPerf[v].numFaces  = static_cast<int>(facesOfVertex.size());

        // 2) Tempo para acessar os vértices vizinhos
        t1 = Clock::now();
        auto adjacentVertices = getVertexAdjacentNoPrepInline(obj, v);
        t2 = Clock::now();

        vertexPerf[v].timeAdjacent = std::chrono::duration<double>(t2 - t1).count();
        vertexPerf[v].numAdjacent  = static_cast<int>(adjacentVertices.size());
    }
    std::cout << "PROCESSAMOS OS VÉRTICES (SEM PRE-PROCESSAMENTO)" << std::endl;

    // Processa faces (paralelizado se disponível)
    #pragma omp parallel for schedule(static)
    for (int f = 0; f < numFaces; ++f) {
        // 1) Tempo para acessar os vértices de uma face
        auto t1 = Clock::now();
        const auto& faceVertices = faces[f];  // acesso direto
        auto t2 = Clock::now();
        facePerf[f].timeAccessVertices = std::chrono::duration<double>(t2 - t1).count();
        facePerf[f].numVertices        = static_cast<int>(faceVertices.size());

        // 2) Tempo para acessar as faces vizinhas
        t1 = Clock::now();
        auto adjacentFaces = getFaceAdjacentNoPrepInline(obj, f);
        t2 = Clock::now();
        facePerf[f].timeFaceAdjacent = std::chrono::duration<double>(t2 - t1).count();
        facePerf[f].numFaceAdjacent  = static_cast<int>(adjacentFaces.size());
    }
    std::cout << "PROCESSAMOS AS FACES (SEM PRE-PROCESSAMENTO)" << std::endl;

    // Tempo total
    auto endTotal = Clock::now();
    double totalTime = std::chrono::duration<double>(endTotal - startTotal).count();

    // Escreve resultados em arquivo CSV
    std::ofstream fout(outputFile);
    if (!fout.is_open()) {
        std::cerr << "Erro ao abrir o arquivo " << outputFile << std::endl;
        return;
    }

    // Cabeçalho: mesmo padrão de antes
    fout << "Tipo,Index,TempoFaces,NumFaces,TempoAdjacentes,NumAdjacentes\n";

    // Dados dos vértices
    for (int v = 0; v < numVertices; ++v) {
        fout << "v," << v << ","
             << vertexPerf[v].timeFaces    << "," << vertexPerf[v].numFaces    << ","
             << vertexPerf[v].timeAdjacent << "," << vertexPerf[v].numAdjacent << "\n";
    }

    // Dados das faces
    for (int f = 0; f < numFaces; ++f) {
        fout << "f," << f << ","
             << facePerf[f].timeAccessVertices << "," << facePerf[f].numVertices   << ","
             << facePerf[f].timeFaceAdjacent   << "," << facePerf[f].numFaceAdjacent << "\n";
    }

    // Tempo total de execução (para referência)
    fout << "total,," << totalTime << ",\n";

    fout.close();
}
