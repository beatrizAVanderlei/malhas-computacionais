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
// Estrutura de hash
// ======================================================================

// A estrutura `PairHash` define um hash para pares de inteiros (usado para mapear arestas para faces adjacentes).
struct PairHash {
    std::size_t operator()(const std::pair<unsigned int, unsigned int>& p) const {  // Sobrecarga do operador de hash.
        auto h1 = std::hash<unsigned int>{}(p.first);  // Cria o hash do primeiro elemento do par.
        auto h2 = std::hash<unsigned int>{}(p.second); // Cria o hash do segundo elemento do par.
        return h1 ^ (h2 << 1);  // Combina os dois hashes em um único valor.
    }
};

// ======================================================================
// Funções de medição de vizinhança
// ======================================================================

// Função que retorna as faces que contêm o vértice `v_index`.

std::vector<int> getVertexFaces(const object::Object& obj, int v_index) {
    std::vector<int> foundFaces;  // Vetor para armazenar os índices das faces que contêm o vértice.
    const auto& faces = obj.getFaces();  // Obtém as faces do objeto.
    foundFaces.reserve(faces.size());  // Reserva espaço no vetor para melhorar a eficiência (otimização).

    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {  // Percorre todas as faces do objeto.
        const auto& face = faces[i];  // Obtém a face atual.
        if (std::find(face.begin(), face.end(), static_cast<unsigned int>(v_index)) != face.end()) {  // Verifica se o vértice está na face.
            foundFaces.push_back(i);  // Se o vértice for encontrado, adiciona o índice da face ao vetor.
        }
    }
    return foundFaces;  // Retorna o vetor com os índices das faces que contêm o vértice.
}

// Função que retorna os vértices adjacentes ao vértice `v_index` utilizando as arestas.

std::vector<unsigned int> getVertexAdjacent(const object::Object& obj, int v_index) {
    std::unordered_set<unsigned int> neighbors;  // Conjunto para armazenar os vértices adjacentes (sem duplicatas).
    const auto& edges = obj.getEdges();  // Obtém as arestas do objeto.

    for (const auto& edge : edges) {  // Para cada aresta no conjunto de arestas do objeto:
        if (edge.first == static_cast<unsigned int>(v_index))  // Se o vértice `v_index` for o primeiro vértice da aresta.
            neighbors.insert(edge.second);  // Adiciona o segundo vértice da aresta como vizinho.
        else if (edge.second == static_cast<unsigned int>(v_index))  // Se o vértice `v_index` for o segundo vértice da aresta.
            neighbors.insert(edge.first);  // Adiciona o primeiro vértice da aresta como vizinho.
    }
    return std::vector<unsigned int>(neighbors.begin(), neighbors.end());  // Retorna os vértices adjacentes como um vetor.
}

// ======================================================================
// Mapeamentos pré-computados para otimizar o acesso
// ======================================================================

// Função que cria um mapeamento dos vértices para as faces às quais pertencem.

std::vector<std::vector<int>> computeVertexToFaces(const object::Object& obj) {
    const auto& faces = obj.getFaces();  // Obtém todas as faces do objeto.
    int numVertices = obj.getVertices().size();  // Obtém o número total de vértices.
    std::vector<std::vector<int>> mapping(numVertices);  // Vetor de vetores para armazenar o mapeamento (um vetor para cada vértice).

    for (int f = 0; f < static_cast<int>(faces.size()); ++f) {  // Para cada face:
        for (unsigned int v : faces[f]) {  // Para cada vértice em cada face:
            mapping[v].push_back(f);  // Adiciona o índice da face ao mapeamento do vértice.
        }
    }
    return mapping;  // Retorna o mapeamento de vértices para faces.
}

// Função que calcula as faces adjacentes.
// Utiliza um mapeamento de arestas para identificar rapidamente quais faces compartilham uma aresta.

std::vector<std::vector<int>> computeFaceAdjacency(const object::Object& obj) {
    const auto& faces = obj.getFaces();  // Obtém todas as faces.
    int numFaces = faces.size();  // Obtém o número total de faces.
    std::vector<std::vector<int>> faceAdj(numFaces);  // Vetor de vetores para armazenar as faces adjacentes de cada face.
    std::unordered_map<std::pair<unsigned int, unsigned int>, std::vector<int>, PairHash> edgeToFaces;  // Mapa de arestas para faces.

    // Para cada face, insira todas as arestas (ordenadas) no mapa.
    for (int f = 0; f < numFaces; ++f) {
        const auto& face = faces[f];
        int n = face.size();  // Número de vértices na face.
        for (int i = 0; i < n; ++i) {  // Para cada vértice da face:
            unsigned int a = face[i];  // Primeiro vértice da aresta.
            unsigned int b = face[(i+1) % n];  // Segundo vértice da aresta (circular para formar um ciclo).
            if (a > b) std::swap(a, b);  // Garante que a aresta seja armazenada de forma ordenada.
            std::pair<unsigned int, unsigned int> edge = {a, b};  // Cria o par de vértices representando a aresta.
            edgeToFaces[edge].push_back(f);  // Adiciona a face ao mapeamento da aresta.
        }
    }

    // Para cada face, obtenha todas as faces que compartilham alguma aresta.
    for (int f = 0; f < numFaces; ++f) {
        std::unordered_set<int> adjSet;  // Conjunto para armazenar as faces adjacentes.
        const auto& face = faces[f];
        int n = face.size();
        for (int i = 0; i < n; ++i) {  // Para cada aresta da face:
            unsigned int a = face[i];
            unsigned int b = face[(i+1) % n];
            if (a > b) std::swap(a, b);  // Garante que a aresta seja ordenada.
            std::pair<unsigned int, unsigned int> edge = {a, b};  // Cria o par da aresta.
            const auto& faceList = edgeToFaces[edge];  // Obtém as faces que compartilham a aresta.
            for (int other : faceList) {  // Para cada face que compartilha a aresta:
                if (other != f) {  // Ignora a face atual.
                    adjSet.insert(other);  // Adiciona a face adjacente ao conjunto.
                }
            }
        }
        faceAdj[f] = std::vector<int>(adjSet.begin(), adjSet.end());  // Converte o conjunto em um vetor e armazena.
    }
    return faceAdj;  // Retorna o mapeamento de faces adjacentes.
}

// ======================================================================
// Funções auxiliares para o cálculo de estatísticas
// ======================================================================

double computeMean(const std::vector<double>& values) {  // Calcula a média dos valores.
    if (values.empty()) return 0;  // Retorna 0 se o vetor estiver vazio.
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();  // Soma todos os valores e divide pelo número de elementos.
}

double computeStdDev(const std::vector<double>& values, double mean) {  // Calcula o desvio padrão.
    if (values.size() <= 1) return 0;  // Retorna 0 se houver apenas 1 valor ou nenhum.
    double accum = 0;
    for (double v : values)  // Para cada valor:
        accum += (v - mean) * (v - mean);  // Calcula a diferença ao quadrado em relação à média.
    return std::sqrt(accum / (values.size() - 1));  // Retorna a raiz quadrada da variância (desvio padrão).
}

double computeMeanInt(const std::vector<int>& values) {  // Calcula a média para um vetor de inteiros.
    if (values.empty()) return 0;  // Retorna 0 se o vetor estiver vazio.
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();  // Soma e divide pelo tamanho.
}

// ======================================================================
// Função que exporta os dados de desempenho para um arquivo CSV
// ======================================================================

void exportPerformanceData(const object::Object& obj, const std::string &outputFile) {  // Função que exporta os dados de desempenho para um arquivo CSV.
    auto startTotal = Clock::now();  // Inicia a contagem de tempo total.

    // Obtém as referências para os vértices e faces do objeto.
    const auto& vertices = obj.getVertices();
    const auto& faces = obj.getFaces();
    int numVertices = vertices.size();
    int numFaces = faces.size();

    // Pré-calcula os mapeamentos necessários para acelerar as consultas.
    auto vertexToFaces = computeVertexToFaces(obj);
    auto faceAdjacency = computeFaceAdjacency(obj);

    // Vetores para armazenar os dados de desempenho dos vértices e faces
    std::vector<double> timeVertexFaces(numVertices, 0);
    std::vector<int> numVertexFaces(numVertices, 0);
    std::vector<double> timeVertexAdjacent(numVertices, 0);
    std::vector<int> numVertexAdjacent(numVertices, 0);
    std::vector<double> timeAccessFaceVertices(numFaces, 0);
    std::vector<int> numFaceVertices(numFaces, 0);
    std::vector<double> timeFaceAdjacent(numFaces, 0);
    std::vector<int> numFaceAdjacent(numFaces, 0);

    // Processa os vértices em paralelo
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

    // Processa as faces em paralelo
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

    // Calcula o tempo total de execução
    auto endTotal = Clock::now();
    double totalTime = std::chrono::duration<double>(endTotal - startTotal).count();

    std::ofstream fout(outputFile);
    if (!fout.is_open()) {
        std::cerr << "Erro ao abrir o arquivo " << outputFile << std::endl;
        return;
    }

    fout << "Tipo,Index,TempoFaces,NumFaces,TempoAdjacentes,NumAdjacentes\n";

    for (int v = 0; v < numVertices; ++v) {
        fout << "v," << v << "," << timeVertexFaces[v] << "," << numVertexFaces[v] << ","
             << timeVertexAdjacent[v] << "," << numVertexAdjacent[v] << "\n";
    }

    for (int f = 0; f < numFaces; ++f) {
        fout << "f," << f << "," << timeAccessFaceVertices[f] << "," << numFaceVertices[f] << ","
             << timeFaceAdjacent[f] << "," << numFaceAdjacent[f] << "\n";
    }

    fout << "total,," << totalTime << ",\n";

    fout.close();
}