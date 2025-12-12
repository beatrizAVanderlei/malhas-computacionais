#include "object.h"
#include <algorithm>
#include <set>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// Variáveis globais
extern float g_offset_x;
extern float g_offset_y;
extern float g_zoom;
extern float g_rotation_x;
extern float g_rotation_y;

namespace object {

    struct PairHash {
        std::size_t operator()(const std::pair<unsigned int, unsigned int>& p) const {
            auto h1 = std::hash<unsigned int>{}(p.first);
            auto h2 = std::hash<unsigned int>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };


    // Construtor:
    Object::Object(const std::array<float, 3>& position,
               const std::vector<std::array<float, 3>>& vertices,
               const std::vector<std::vector<unsigned int>>& faces,
               const std::vector<unsigned int>& face_cells,
               const std::string& filename,
               int detection_size,
               bool initGl)
    : filename_(filename),
      position_(position),
      vertices_(vertices),
      faces_(faces),
      face_cells_(face_cells),
      detection_size_(detection_size),
      scale_(1.0f),
      vbo_vertices_(0),
      ibo_faces_(0),
      ibo_edges_(0),
      selectedFace(-1),
      selectedVertex(-1)
    {
        // Inicializa as cores padrão para vértices e faces
        vertexColors.resize(vertices_.size(), Color{0.0f, 0.0f, 0.0f});
        faceColors.resize(faces_.size(), Color{60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f});

        facesOriginais = faces_;

        for (size_t i = 0; i < faces_.size(); ++i) {
            originalToCurrentIndex[static_cast<int>(i)] = static_cast<int>(i);
        }

        edges_ = calculateEdges(faces_);

        vertexToFacesMapping = computeVertexToFaces();
        faceAdjacencyMapping  = computeFaceAdjacency();

        if (initGl) {
            setupVBOs();
        }
    }

    // Destrutor: apaga os buffers da GPU
    Object::~Object() {
        if (vbo_vertices_ != 0) glDeleteBuffers(1, &vbo_vertices_);
        if (ibo_faces_ != 0) glDeleteBuffers(1, &ibo_faces_);
        if (ibo_edges_ != 0) glDeleteBuffers(1, &ibo_edges_);
    }

    void Object::updateConnectivity() {
        // Recalcula os mapas baseados na geometria atual (faces_ e vertices_)
        vertexToFacesMapping = computeVertexToFaces();
        faceAdjacencyMapping = computeFaceAdjacency();
    }

    // Adicione esta função no final do arquivo ou junto com os métodos de seleção
    void Object::selectFacesByGroup(int faceIndex) {
        // Validação
        if (faceIndex < 0 || faceIndex >= static_cast<int>(face_cells_.size())) return;

        // Pega o ID do grupo da face clicada
        unsigned int targetID = face_cells_[faceIndex];

        // Se o ID for inválido (ex: -1 castado para unsigned int max), usamos um fallback ou retornamos
        // Aqui assumimos que qualquer ID gravado é válido para agrupamento.

        std::cout << "Selecionando grupo ID: " << targetID << std::endl;

        // Varredura Linear O(N) - Muito rápida para memória contígua
        for (size_t i = 0; i < face_cells_.size(); ++i) {
            if (face_cells_[i] == targetID) {
                // Evita duplicatas verificando se já está selecionado (opcional, mas bom para performance visual)
                // Para simplicidade, apenas adicionamos e pintamos.
                selectedFaces.push_back(static_cast<int>(i));
                faceColors[i] = {1.0f, 0.0f, 0.0f}; // Vermelho
            }
        }
    }

    std::vector<std::vector<int>> Object::computeVertexToFaces() const {
        std::vector<std::vector<int>> mapping(vertices_.size());
        for (int f = 0; f < static_cast<int>(faces_.size()); ++f) {
            for (unsigned int v : faces_[f]) {
                mapping[v].push_back(f);
            }
        }
        return mapping;
    }

    std::vector<std::vector<int>> Object::computeFaceAdjacency() const {
        int numFaces = faces_.size();
        std::vector<std::vector<int>> faceAdj(numFaces);
        std::unordered_map<std::pair<unsigned int, unsigned int>, std::vector<int>, PairHash> edgeToFaces;

        // Para cada face, insere todas as arestas (com vértices ordenados) no mapa.
        for (int f = 0; f < numFaces; ++f) {
            const auto &face = faces_[f];
            int n = face.size();
            for (int i = 0; i < n; ++i) {
                unsigned int a = face[i];
                unsigned int b = face[(i + 1) % n];
                if (a > b) std::swap(a, b);
                std::pair<unsigned int, unsigned int> edge = {a, b};
                edgeToFaces[edge].push_back(f);
            }
        }

        // Para cada face, obtém todas as faces que compartilham alguma aresta.
        for (int f = 0; f < numFaces; ++f) {
            std::unordered_set<int> adjSet;
            const auto &face = faces_[f];
            int n = face.size();
            for (int i = 0; i < n; ++i) {
                unsigned int a = face[i];
                unsigned int b = face[(i + 1) % n];
                if (a > b) std::swap(a, b);
                std::pair<unsigned int, unsigned int> edge = {a, b};
                const auto &faceList = edgeToFaces[edge];
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

    // Calcula as arestas únicas a partir das faces
    std::vector<std::pair<unsigned int, unsigned int>> Object::calculateEdges(const std::vector<std::vector<unsigned int>>& faces) {
        std::set<std::pair<unsigned int, unsigned int>> edgeSet;
        for (const auto& face : faces) {
            size_t n = face.size();
            if (n == 4) { // Se for quadrilátero, ignora a diagonal
                edgeSet.insert({std::min(face[0], face[1]), std::max(face[0], face[1])});
                edgeSet.insert({std::min(face[1], face[2]), std::max(face[1], face[2])});
                edgeSet.insert({std::min(face[2], face[3]), std::max(face[2], face[3])});
                edgeSet.insert({std::min(face[3], face[0]), std::max(face[3], face[0])});
            } else {
                for (size_t i = 0; i < n; ++i) {
                    unsigned int v1 = face[i];
                    unsigned int v2 = face[(i + 1) % n];
                    if (v1 > v2) std::swap(v1, v2);
                    edgeSet.insert({v1, v2});
                }
            }
        }
        return std::vector<std::pair<unsigned int, unsigned int>>(edgeSet.begin(), edgeSet.end());
    }

    // Triangula as faces e mapeia cada triângulo à face original
    std::vector<std::array<unsigned int, 3>> Object::triangulateFaces(const std::vector<std::vector<unsigned int>>& faces) const {
        std::vector<std::array<unsigned int, 3>> triangles;
        faceTriangleMap.clear();
        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
            const auto& face = faces[faceIndex];
            size_t n = face.size();
            if (n < 3) continue;
            else if (n == 3) {
                triangles.push_back({face[0], face[1], face[2]});
                faceTriangleMap[static_cast<int>(triangles.size()) - 1] = static_cast<int>(faceIndex);
            } else {
                unsigned int v0 = face[0];
                for (size_t i = 1; i < n - 1; ++i) {
                    triangles.push_back({v0, face[i], face[i + 1]});
                    faceTriangleMap[static_cast<int>(triangles.size()) - 1] = static_cast<int>(faceIndex);
                }
            }
        }
        return triangles;
    }

    // Configura os VBOs e IBOs: prepara os arrays e envia os dados para a GPU
    void Object::setupVBOs() {
        // Converte os vértices para um array "achatado"
        vertex_array_.clear();
        for (const auto& v : vertices_) {
            vertex_array_.push_back(v[0]);
            vertex_array_.push_back(v[1]);
            vertex_array_.push_back(v[2]);
        }

        // Triangula as faces e constrói o array de índices para faces
        face_index_array_.clear();
        auto tri_faces = triangulateFaces(faces_);
        for (const auto& tri : tri_faces) {
            face_index_array_.push_back(tri[0]);
            face_index_array_.push_back(tri[1]);
            face_index_array_.push_back(tri[2]);
        }

        // Constrói o array de índices para as arestas
        edge_index_array_.clear();
        for (const auto& edge : edges_) {
            edge_index_array_.push_back(edge.first);
            edge_index_array_.push_back(edge.second);
        }

        // Gera os buffers na GPU, se necessário
        if (vbo_vertices_ == 0)
            glGenBuffers(1, &vbo_vertices_);
        if (ibo_faces_ == 0)
            glGenBuffers(1, &ibo_faces_);
        if (ibo_edges_ == 0)
            glGenBuffers(1, &ibo_edges_);

        // Envia os dados dos vértices
        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices_);
        glBufferData(GL_ARRAY_BUFFER, vertex_array_.size() * sizeof(float), vertex_array_.data(), GL_STATIC_DRAW);

        // Envia os dados dos índices dos triângulos (faces)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_faces_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, face_index_array_.size() * sizeof(unsigned int), face_index_array_.data(), GL_STATIC_DRAW);

        // Envia os dados dos índices das arestas
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_edges_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, edge_index_array_.size() * sizeof(unsigned int), edge_index_array_.data(), GL_STATIC_DRAW);

        // Desvincula os buffers
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // Atualiza os VBOs chamando setupVBOs()
    void Object::updateVBOs() {
        setupVBOs();
    }
}