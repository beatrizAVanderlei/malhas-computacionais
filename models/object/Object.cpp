/*
 * ======================================================================================
 * OBJECT.CPP - NÚCLEO DA ESTRUTURA DE DADOS (TOPOLOGIA DE MALHA)
 * ======================================================================================
 *
 * Este arquivo implementa a estrutura de dados central "Object", responsável por manter
 * a representação geométrica e topológica da malha 3D.
 *
 * CONCEITOS FUNDAMENTAIS IMPLEMENTADOS AQUI:
 *
 * 1. MALHA POLIGONAL (Polygon Mesh):
 * - Armazenada como uma lista de Vértices (coordenadas x,y,z) e uma lista de Faces
 * (índices que conectam os vértices).
 * - Suporta triângulos, quadriláteros (quads) e N-gonos genéricos.
 *
 * 2. TOPOLOGIA E CONECTIVIDADE (Graph Theory):
 * - Para permitir edições avançadas (como selecionar vizinhos ou caminhar pela superfície),
 * não basta apenas desenhar triângulos; precisamos saber "quem é vizinho de quem".
 * - Este arquivo calcula e mantém mapas de adjacência (Grafos):
 * a) Vértice -> Faces (Quais faces tocam este ponto?)
 * b) Face -> Faces (Quais triângulos são vizinhos deste?)
 * c) Arestas Únicas (Para desenho wireframe eficiente).
 *
 * 3. HASHING ESPACIAL E DE PARES:
 * - Para identificar arestas compartilhadas sem duplicatas (ex: aresta 1-2 é igual a 2-1),
 * utilizamos funções de Hash personalizadas para normalizar a ordem dos índices.
 *
 * 4. GERENCIAMENTO DE MEMÓRIA GPU (Lifecycle):
 * - Responsável pelo nascimento (Construtor) e morte (Destrutor) dos buffers OpenGL
 * (VBOs, IBOs), garantindo que não haja vazamento de memória na placa de vídeo.
 *
 * ======================================================================================
 */

#include "object.h"
#include <algorithm>
#include <set>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace object {
    // ============================================================
    // ESTRUTURAS AUXILIARES
    // ============================================================

    // Functor de Hash para std::pair<uint, uint>.
    // O C++ padrão não possui hash nativo para pares, então precisamos definir um.
    // Usado para identificar arestas únicas em mapas não ordenados (Hash Maps).
    struct PairHash {
        std::size_t operator()(const std::pair<unsigned int, unsigned int> &p) const {
            // Combina o hash do primeiro índice com o hash do segundo.
            auto h1 = std::hash<unsigned int>{}(p.first);
            auto h2 = std::hash<unsigned int>{}(p.second);
            // O operador XOR (^) e o shift (<< 1) misturam os bits para evitar colisões
            // (ex: evitar que {1,2} tenha o mesmo hash que {2,1} se não fossem ordenados).
            return h1 ^ (h2 << 1);
        }
    };

    // ============================================================
    // CONSTRUTOR & DESTRUTOR (CICLO DE VIDA)
    // ============================================================

    // Inicializa o objeto, carrega dados na RAM e prepara a GPU.
    Object::Object(const std::array<float, 3> &position,
                   const std::vector<std::array<float, 3> > &vertices,
                   const std::vector<std::vector<unsigned int> > &faces,
                   const std::vector<unsigned int> &face_cells,
                   const std::string &filename,
                   int detection_size,
                   bool initGl)
        : filename_(filename),
          position_(position), // Posição no mundo (Translação)
          vertices_(vertices), // Lista de coordenadas (x,y,z)
          faces_(faces), // Topologia (índices)
          face_cells_(face_cells), // Grupos lógicos (IDs de material/objeto)
          detection_size_(detection_size),
          scale_(1.0f),
          // Inicializa handles OpenGL com 0 (nulo)
          vbo_vertices_(0),
          ibo_faces_(0),
          ibo_edges_(0),
          selectedFace(-1),
          selectedVertex(-1) {
        // 1. Inicialização de Propriedades Visuais
        // Cria vetores de cor paralelos à geometria.
        // Inicializa vértices com Preto (0,0,0)
        vertexColors.resize(vertices_.size(), Color{0.0f, 0.0f, 0.0f});
        // Inicializa faces com Cinza Claro (0.8, 0.8, 0.8) - Padrão "Clay"
        faceColors.resize(faces_.size(), Color{0.8f, 0.8f, 0.8f});

        // 2. Mapeamento de Identidade (Picking)
        // Cria um mapa "De -> Para" que rastreia os índices originais das faces.
        for (size_t i = 0; i < faces_.size(); ++i) {
            originalToCurrentIndex[static_cast<int>(i)] = static_cast<int>(i);
        }

        if (detection_size_ != 0) {
            this->scale_ = 1.0f;
        } else {
            // Só entra aqui se detection_size for 0. Calcula Bounding Box.
            if (!vertices_.empty()) {
                float minX = vertices_[0][0], maxX = vertices_[0][0];
                float minY = vertices_[0][1], maxY = vertices_[0][1];
                float minZ = vertices_[0][2], maxZ = vertices_[0][2];
                for (const auto &v: vertices_) {
                    if (v[0] < minX) minX = v[0];
                    if (v[0] > maxX) maxX = v[0];
                    if (v[1] < minY) minY = v[1];
                    if (v[1] > maxY) maxY = v[1];
                    if (v[2] < minZ) minZ = v[2];
                    if (v[2] > maxZ) maxZ = v[2];
                }
                float maxDim = std::max({maxX - minX, maxY - minY, maxZ - minZ});
                if (maxDim > 0) this->scale_ = 2.0f / maxDim;
            }
        }

        // 3. Pré-cálculo de Topologia (Otimização)
        // Calcula estruturas de aceleração para navegação na malha.
        edges_ = calculateEdges(faces_); // Extrai linhas para Wireframe
        vertexToFacesMapping = computeVertexToFaces(); // Mapeia Vértice -> Faces Vizinhas
        faceAdjacencyMapping = computeFaceAdjacency(); // Mapeia Face -> Faces Vizinhas

        // 4. Upload para GPU
        if (initGl) {
            setupVBOs();
        }
    }

    // Limpa a memória da placa de vídeo quando o objeto é destruído.
    Object::~Object() {
        // Verifica se os buffers existem antes de deletar
        if (vbo_vertices_ != 0)
            glDeleteBuffers(1, &vbo_vertices_);
        if (ibo_faces_ != 0)
            glDeleteBuffers(1, &ibo_faces_);
        if (ibo_edges_ != 0)
            glDeleteBuffers(1, &ibo_edges_);
    }

    // Recalcula as relações de vizinhança.
    void Object::updateConnectivity() {
        vertexToFacesMapping = computeVertexToFaces();
        faceAdjacencyMapping = computeFaceAdjacency();
        edges_ = calculateEdges(faces_);
    }

    // ============================================================
    // CÁLCULOS TOPOLÓGICOS (TEORIA DOS GRAFOS APLICADA)
    // ============================================================

    // 1. Mapeamento Vértice -> Faces (Reverse Lookup)
    std::vector<std::vector<int> > Object::computeVertexToFaces() const {
        std::vector<std::vector<int> > mapping(vertices_.size());

        // Itera sobre todas as faces
        for (int f = 0; f < static_cast<int>(faces_.size()); ++f) {
            // Para cada vértice 'v' da face 'f'
            for (unsigned int v: faces_[f]) {
                // Adiciona 'f' à lista de faces incidentes em 'v'
                mapping[v].push_back(f);
            }
        }
        return mapping;
    }

    // 2. Grafo de Adjacência de Faces (Dual Graph)
    std::vector<std::vector<int> > Object::computeFaceAdjacency() const {
        int numFaces = faces_.size();
        std::vector<std::vector<int> > faceAdj(numFaces);

        // Passo A: Mapear Arestas -> Lista de Faces que a compartilham.
        std::unordered_map<std::pair<unsigned int, unsigned int>, std::vector<int>, PairHash> edgeToFaces;

        for (int f = 0; f < numFaces; ++f) {
            const auto &face = faces_[f];
            int n = face.size();
            // Itera sobre as arestas do polígono (v[i] -> v[i+1])
            for (int i = 0; i < n; ++i) {
                unsigned int a = face[i];
                unsigned int b = face[(i + 1) % n];
                if (a > b) std::swap(a, b);
                edgeToFaces[{a, b}].push_back(f);
            }
        }

        // Passo B: Construir a lista de adjacência final.
        // Se a aresta E é compartilhada pelas faces F1 e F2, então F1 é vizinha de F2.
        for (int f = 0; f < numFaces; ++f) {
            std::unordered_set<int> adjSet; // Set para evitar duplicatas (ex: vizinho por 2 arestas)
            const auto &face = faces_[f];
            int n = face.size();

            for (int i = 0; i < n; ++i) {
                unsigned int a = face[i];
                unsigned int b = face[(i + 1) % n];
                if (a > b) std::swap(a, b);
                const auto &faceList = edgeToFaces[{a, b}];
                for (int other: faceList) {
                    if (other != f) adjSet.insert(other);
                }
            }
            // Converte Set para Vector (mais rápido para iterar depois)
            faceAdj[f] = std::vector<int>(adjSet.begin(), adjSet.end());
        }
        return faceAdj;
    }


    // 3. Extração de Arestas Únicas (Wireframe)
    std::vector<std::pair<unsigned int, unsigned int> > Object::calculateEdges(
        const std::vector<std::vector<unsigned int> > &faces) {
        std::set<std::pair<unsigned int, unsigned int> > edgeSet; // Set ordenado remove duplicatas automaticamente

        for (const auto &face: faces) {
            size_t n = face.size();

            // Tratamento especial para Quadriláteros (Quads)
            if (n == 4) {
                edgeSet.insert({std::min(face[0], face[1]), std::max(face[0], face[1])});
                edgeSet.insert({std::min(face[1], face[2]), std::max(face[1], face[2])});
                edgeSet.insert({std::min(face[2], face[3]), std::max(face[2], face[3])});
                edgeSet.insert({std::min(face[3], face[0]), std::max(face[3], face[0])});
            } else {
                // Polígonos Genéricos (Triângulos)
                for (size_t i = 0; i < n; ++i) {
                    unsigned int v1 = face[i];
                    unsigned int v2 = face[(i + 1) % n];
                    if (v1 > v2) std::swap(v1, v2); // Normaliza
                    edgeSet.insert({v1, v2});
                }
            }
        }
        // Retorna como vetor linear para envio rápido ao OpenGL (IBO)
        return std::vector<std::pair<unsigned int, unsigned int> >(edgeSet.begin(), edgeSet.end());
    }

    // ============================================================
    // GETTERS DE ACESSO A DADOS (Interface Pública)
    // ============================================================

    // Retorna o cache de texturas (CPU) para uso no Path Tracer.
    const std::map<GLuint, RawTextureData> &Object::getTextureCache() const {
        return texture_cache_cpu_;
    }

    // Retorna o mapa de qual face usa qual ID de textura OpenGL.
    const std::map<int, GLuint> &Object::getFaceTextureMap() const {
        return face_texture_map_;
    }

    // Retorna as coordenadas UV de cada face.
    const std::map<int, std::vector<Vec2> > &Object::getFaceUvMap() const {
        return face_uv_map_;
    }
} // namespace object
