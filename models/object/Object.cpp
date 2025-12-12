#define STB_IMAGE_IMPLEMENTATION
#include "../libs/stb_image.h"
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
        faceColors.resize(faces_.size(), Color{0.8f, 0.8f, 0.8f});

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

    GLuint Object::loadTexture(const std::string& filepath) {
        int width, height, nrChannels;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &nrChannels, 0);

        if (!data) {
            std::cerr << "Falha ao carregar textura." << std::endl;
            return 0;
        }

        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

        // [NOVO] Salva cópia na RAM para o Path Tracer
        RawTextureData raw;
        raw.width = width;
        raw.height = height;

        // Se for RGBA, converte para RGB (Path Tracer simples prefere RGB)
        int numPixels = width * height;
        raw.pixels.reserve(numPixels * 3);

        for(int i = 0; i < numPixels; ++i) {
            int srcIdx = i * nrChannels;
            raw.pixels.push_back(data[srcIdx]);     // R
            raw.pixels.push_back(data[srcIdx + 1]); // G
            raw.pixels.push_back(data[srcIdx + 2]); // B
        }

        texture_cache_cpu_[textureID] = raw; // Salva no mapa

        stbi_image_free(data);
        return textureID;
    }

    // --- GETTERS DE TEXTURA IMPLEMENTADOS AQUI ---
    const std::map<GLuint, RawTextureData>& Object::getTextureCache() const {
        return texture_cache_cpu_;
    }

    const std::map<int, GLuint>& Object::getFaceTextureMap() const {
        return face_texture_map_;
    }

    const std::map<int, std::vector<Vec2>>& Object::getFaceUvMap() const {
        return face_uv_map_;
    }
    // ---------------------------------------------

    // Aplica a textura na face selecionada
    void Object::applyTextureToSelectedFaces(const std::string& filepath) {
        // 1. Verificação de segurança
        if (selectedFaces.empty()) {
            std::cout << "Nenhuma face selecionada." << std::endl;
            return;
        }

        // 2. Carrega a textura APENAS UMA VEZ
        GLuint texID = loadTexture(filepath);
        if (texID == 0) return;

        std::cout << "Aplicando textura continua em " << selectedFaces.size() << " faces..." << std::endl;

        // --- PASSO 1: CALCULAR BOUNDING BOX DA SELEÇÃO ---
        // Precisamos saber o tamanho total da área selecionada para esticar a textura sobre ela
        float minX = 1e20f, maxX = -1e20f;
        float minY = 1e20f, maxY = -1e20f;
        float minZ = 1e20f, maxZ = -1e20f;

        for (int faceIdx : selectedFaces) {
            for (unsigned int vIdx : faces_[faceIdx]) {
                const auto& v = vertices_[vIdx];
                if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
                if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
                if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
            }
        }

        // Calcula as dimensões
        float dx = maxX - minX;
        float dy = maxY - minY;
        float dz = maxZ - minZ;

        // --- PASSO 2: ESCOLHER O MELHOR PLANO DE PROJEÇÃO ---
        // Descobrimos qual eixo é o "mais chato" (menor variação) para usar como normal.
        // Ex: Um chão varia muito em X e Z, mas quase nada em Y. Logo, projetamos no plano XZ.
        int projectionPlane = 0; // 0=YZ, 1=XZ, 2=XY

        if (dx <= dy && dx <= dz) {
            projectionPlane = 0; // Variação em X é a menor -> Projeta em YZ (Lado)
        }
        else if (dy <= dx && dy <= dz) {
            projectionPlane = 1; // Variação em Y é a menor -> Projeta em XZ (Chão/Teto)
        }
        else {
            projectionPlane = 2; // Variação em Z é a menor -> Projeta em XY (Frente)
        }

        // Evita divisão por zero se a seleção for uma linha ou ponto
        if (dx < 1e-4) dx = 1.0f;
        if (dy < 1e-4) dy = 1.0f;
        if (dz < 1e-4) dz = 1.0f;

        // --- PASSO 3: GERAR UVs BASEADO NA POSIÇÃO GLOBAL ---
        for (int faceIdx : selectedFaces) {
            face_texture_map_[faceIdx] = texID;

            std::vector<Vec2> uvs;
            const auto& face = faces_[faceIdx];

            for (unsigned int vIdx : face) {
                const auto& v = vertices_[vIdx];
                float u = 0.0f, coord_v = 0.0f; // Renomeado para evitar conflito com 'v' do vértice

                // Calcula a posição relativa (0.0 a 1.0) dentro da caixa de seleção
                if (projectionPlane == 0) { // Plano YZ
                    u = (v[1] - minY) / dy;       // Eixo Y vira U
                    coord_v = (v[2] - minZ) / dz; // Eixo Z vira V
                }
                else if (projectionPlane == 1) { // Plano XZ (Mais comum para chão)
                    u = (v[0] - minX) / dx;       // Eixo X vira U
                    coord_v = (v[2] - minZ) / dz; // Eixo Z vira V
                    // Opcional: Inverter V se a imagem ficar de cabeça para baixo
                    coord_v = 1.0f - coord_v;
                }
                else { // Plano XY
                    u = (v[0] - minX) / dx;       // Eixo X vira U
                    coord_v = (v[1] - minY) / dy; // Eixo Y vira V
                }

                uvs.push_back({u, coord_v});
            }
            face_uv_map_[faceIdx] = uvs;
        }
    }

    // Desenha as faces texturizadas (Overlay)
    void Object::drawTexturedFaces() {
        if (face_texture_map_.empty()) return;

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Configuração para desenhar sobre a malha sem z-fighting
        glDepthFunc(GL_LEQUAL);

        glColor3f(1.0f, 1.0f, 1.0f); // Cor neutra para a textura

        for (auto const& [faceIdx, texID] : face_texture_map_) {
            // Segurança de índice
            if (faceIdx < 0 || faceIdx >= static_cast<int>(faces_.size())) continue;

            // [CORREÇÃO] Verifica se a face está na lista de seleção
            bool isSelected = false;
            for (int s : selectedFaces) {
                if (s == faceIdx) {
                    isSelected = true;
                    break;
                }
            }

            // Se a face estiver selecionada, NÃO desenha a textura.
            // Isso permite que a cor vermelha da malha base (definida no controls.cpp) apareça.
            if (isSelected) continue;

            const auto& face = faces_[faceIdx];

            if (face_uv_map_.find(faceIdx) == face_uv_map_.end()) continue;
            const auto& uvs = face_uv_map_[faceIdx];

            glBindTexture(GL_TEXTURE_2D, texID);

            glBegin(GL_POLYGON);
            for (size_t i = 0; i < face.size(); ++i) {
                if (i < uvs.size()) glTexCoord2f(uvs[i].u, uvs[i].v);

                int vIdx = face[i];
                glVertex3f(vertices_[vIdx][0], vertices_[vIdx][1], vertices_[vIdx][2]);
            }
            glEnd();
        }

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glDepthFunc(GL_LESS); // Restaura o teste de profundidade padrão
    }

    // Configura os VBOs e IBOs: prepara os arrays e envia os dados para a GPU
    void Object::setupVBOs() {
        vertex_array_.clear();
        for (const auto& v : vertices_) {
            vertex_array_.push_back(v[0]);
            vertex_array_.push_back(v[1]);
            vertex_array_.push_back(v[2]);
        }

        face_index_array_.clear();
        auto tri_faces = triangulateFaces(faces_);
        for (const auto& tri : tri_faces) {
            face_index_array_.push_back(tri[0]);
            face_index_array_.push_back(tri[1]);
            face_index_array_.push_back(tri[2]);
        }

        edge_index_array_.clear();
        for (const auto& edge : edges_) {
            edge_index_array_.push_back(edge.first);
            edge_index_array_.push_back(edge.second);
        }

        if (vbo_vertices_ == 0) glGenBuffers(1, &vbo_vertices_);
        if (ibo_faces_ == 0) glGenBuffers(1, &ibo_faces_);
        if (ibo_edges_ == 0) glGenBuffers(1, &ibo_edges_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices_);
        glBufferData(GL_ARRAY_BUFFER, vertex_array_.size() * sizeof(float), vertex_array_.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_faces_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, face_index_array_.size() * sizeof(unsigned int), face_index_array_.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_edges_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, edge_index_array_.size() * sizeof(unsigned int), edge_index_array_.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void Object::clearColors() {
        std::fill(vertexColors.begin(), vertexColors.end(), Color{0.0f, 0.0f, 0.0f});
        Color faceDefault = {0.8f, 0.8f, 0.8f};
        std::fill(faceColors.begin(), faceColors.end(), faceDefault);
    }

    // Atualiza os VBOs chamando setupVBOs()
    void Object::updateVBOs() {
        setupVBOs();
    }
}