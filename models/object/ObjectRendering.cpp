/*
 * ======================================================================================
 * OBJECT RENDERING - MÓDULO DE VISUALIZAÇÃO E DESENHO
 * ======================================================================================
 * * Este arquivo encapsula a lógica de interface com a GPU (Graphics Processing Unit)
 * utilizando a API OpenGL Clássica (Fixed Function Pipeline) com extensões modernas (VBOs).
 * * CONCEITOS E FLUXO DE DADOS:
 * * 1. TRIANGULAÇÃO (Tessellation):
 * - A GPU desenha nativamente apenas triângulos. O objeto pode conter faces complexas
 * (quadriláteros, polígonos côncavos/convexos).
 * - A função `triangulateFaces` converte qualquer N-gono em um conjunto de triângulos
 * usando o metodo "Triangle Fan" (Vértice 0 conecta a todos).
 * * 2. VERTEX BUFFER OBJECTS (VBOs) & INDEX BUFFER OBJECTS (IBOs):
 * - Em vez de enviar vértices um por um a cada frame (modo imediato `glBegin/glEnd` lento),
 * armazenamos os dados na memória da placa de vídeo (VRAM).
 * - VBO (`vbo_vertices_`): Guarda coordenadas (x, y, z).
 * - IBO (`ibo_faces_`, `ibo_edges_`): Guarda apenas os índices (0, 1, 2...), economizando
 * memória e permitindo reutilização de vértices.
 * * 3. TEXTURIZAÇÃO (Texture Mapping):
 * - Utiliza a biblioteca `stb_image` para decodificar formatos PNG/JPG em arrays de bytes.
 * - Gerencia o upload para a GPU (`glTexImage2D`) e configurações de amostragem (filtros).
 * - Implementa lógica de overlay para desenhar texturas sobre a malha base, respeitando
 * a seleção do usuário (o vermelho da seleção tem prioridade sobre a textura).
 * * 4. RENDERIZAÇÃO EM CAMADAS (Multi-pass Rendering):
 * - O metodo `draw` orquestra o desenho em ordem específica para lidar com profundidade:
 * a) Faces Sólidas (Fundo) -> Escreve no Z-Buffer.
 * b) Arestas (Wireframe) -> Linhas pretas para definição de forma.
 * c) Vértices (Pontos) -> Dupla passada para desenhar normais e selecionados (com Z-Test desativado).
 * d) Texturas (Overlay) -> Mistura com a cor base (Blend).
 * * ======================================================================================
 */

#define STB_IMAGE_IMPLEMENTATION
#include "../libs/stb_image.h"
#include "object.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <unordered_set>
#include <algorithm>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

namespace object {

    // ============================================================
    // 1. HELPERS DE GEOMETRIA (Prepara dados para OpenGL)
    // ============================================================

    /*
     * Converte a lista de faces (que pode conter polígonos de N lados)
     * em uma lista plana de triângulos compatível com o rasterizador da GPU.
     * * Algoritmo (Triangle Fan):
     * Para um polígono com vértices v0, v1, v2, v3...
     * Cria triângulos: (v0, v1, v2), (v0, v2, v3), etc.
     * Complexidade: O(Vértices Totais)
     */
    std::vector<std::array<unsigned int, 3>> Object::triangulateFaces(const std::vector<std::vector<unsigned int>>& faces) const {
        std::vector<std::array<unsigned int, 3>> triangles;
        faceTriangleMap.clear(); // Reseta o mapa [Indice Triângulo -> Índice Face Original]

        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
            const auto& face = faces[faceIndex];
            size_t n = face.size();

            if (n < 3) continue; // Ignora linhas ou pontos degenerados

            // Caso simples (Triângulo): Copia direto
            else if (n == 3) {
                triangles.push_back({face[0], face[1], face[2]});
                // Mapeia o triângulo gerado de volta para a face original (útil para picking)
                faceTriangleMap[static_cast<int>(triangles.size()) - 1] = static_cast<int>(faceIndex);
            }
            // Caso complexo (N-Gono): Divide em N-2 triângulos
            else {
                unsigned int v0 = face[0]; // Pivô do leque
                for (size_t i = 1; i < n - 1; ++i) {
                    triangles.push_back({v0, face[i], face[i + 1]});
                    faceTriangleMap[static_cast<int>(triangles.size()) - 1] = static_cast<int>(faceIndex);
                }
            }
        }
        return triangles;
    }

    // ============================================================
    // 2. GERENCIAMENTO DE TEXTURAS (Recursos da GPU)
    // ============================================================

    /*
     * Carrega uma imagem do disco, decodifica e envia para a memória de vídeo.
     * Retorna o ID (Handle) da textura OpenGL.
     */
    GLuint Object::loadTexture(const std::string& filepath) {
        int width, height, nrChannels;

        // O sistema de coordenadas de imagem padrão (origem top-left) é oposto ao do OpenGL (bottom-left).
        // Invertemos verticalmente na carga para corrigir a orientação.
        stbi_set_flip_vertically_on_load(true);

        // Decodifica a imagem (JPG/PNG/BMP) para um array de bytes cru (unsigned char*)
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &nrChannels, 0);

        if (!data) {
            std::cerr << "Falha ao carregar textura: " << filepath << std::endl;
            return 0; // ID 0 indica falha
        }

        // Gera um identificador único para a textura na GPU
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID); // Ativa essa textura para configuração

        // Configura comportamento de bordas (Wrapping) - Repete a textura se UV > 1.0
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Configura filtros de escala (Filtering)
        // MIN_FILTER: Quando a textura está longe (minificada) -> Linear
        // MAG_FILTER: Quando a textura está muito perto (magnificada) -> Linear (suave)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Detecta se a imagem tem transparência (Alpha Channel)
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;

        // Upload dos dados para a VRAM
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

        // [IMPORTANTE] Salva uma cópia dos pixels na RAM (CPU) para o Path Tracer.
        // O Ray Tracing roda na CPU e não consegue ler a memória da GPU diretamente.
        RawTextureData raw;
        raw.width = width;
        raw.height = height;
        int numPixels = width * height;
        raw.pixels.reserve(numPixels * 3);

        for(int i = 0; i < numPixels; ++i) {
            int srcIdx = i * nrChannels;
            // Descarta o canal Alpha se existir, converte para RGB puro para o Path Tracer
            raw.pixels.push_back(data[srcIdx]);     // R
            raw.pixels.push_back(data[srcIdx + 1]); // G
            raw.pixels.push_back(data[srcIdx + 2]); // B
        }

        texture_cache_cpu_[textureID] = raw; // Guarda no cache da classe Object
        stbi_image_free(data); // Libera a memória temporária do STB

        return textureID;
    }

    // ============================================================
    // 3. FUNÇÕES DE DESENHO (DRAW CALLS)
    // ============================================================

    // Função "Master" de desenho. Orquestra a renderização das camadas.
    void Object::draw(const ColorsMap& colors, bool vertexOnlyMode, bool faceOnlyMode) {
        glPushMatrix(); // Salva a matriz atual da câmera

        // Aplica Transformações de Modelo (Model Matrix)
        // Move o objeto local para sua posição no mundo
        glTranslatef(position_[0], position_[1], position_[2]);
        glScalef(scale_, scale_, scale_);

        // Camada 1: Faces Sólidas (Preenchimento)
        if (!vertexOnlyMode) {
            Color faceColor = colors.count("surface") ? colors.at("surface") : Color{1.0f, 0.0f, 0.0f};
            drawFacesVBO(faceColor, vertexOnlyMode);
        }

        // Camada 2: Arestas (Wireframe)
        // Desenhado por cima das faces para destacar a topologia
        Color edgeColor = colors.count("edge") ? colors.at("edge") : Color{0.0f, 0.0f, 0.0f};
        drawEdgesVBO(edgeColor);

        // Camada 3: Vértices (Nuvem de Pontos)
        if (!faceOnlyMode) {
            Color vertexColor = colors.count("vertex") ? colors.at("vertex") : Color{0.0f, 0.0f, 0.0f};
            drawVerticesVBO(vertexColor);
        }

        glPopMatrix(); // Restaura a matriz da câmera
    }

    // Desenha a geometria sólida usando triângulos
    void Object::drawFacesVBO(const Color& defaultColor, bool vertexOnlyMode) {
        if (vertexOnlyMode) return;

        auto tri_faces = triangulateFaces(faces_);

        glBegin(GL_TRIANGLES); // Modo imediato (para flexibilidade de cor por face)
        for (size_t i = 0; i < tri_faces.size(); ++i) {
            // Descobre qual face original é dona deste triângulo
            int origFace = static_cast<int>(i);
            auto it = faceTriangleMap.find(static_cast<int>(i));
            if (it != faceTriangleMap.end())
                origFace = it->second;

            // Lógica de Cor: Usa cor específica da face (ex: seleção) ou padrão
            Color col = defaultColor;
            if (origFace < static_cast<int>(faceColors.size()))
                col = faceColors[origFace];

            glColor3f(col[0], col[1], col[2]);

            // Envia os 3 vértices do triângulo
            for (int j = 0; j < 3; ++j) {
                unsigned int vertexIndex = tri_faces[i][j];
                const std::array<float, 3>& vertex = vertices_[vertexIndex];
                glVertex3f(vertex[0], vertex[1], vertex[2]);
            }
        }
        glEnd();
    }

    // Desenha texturas projetadas sobre as faces
    void Object::drawTexturedFaces() {
        if (face_texture_map_.empty()) return; // Sai se não houver texturas

        // Habilita pipeline de texturização e mistura (blending)
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // [TRUQUE VISUAL] GL_LEQUAL (Less or Equal)
        // Permite desenhar pixels que estão exatamente na mesma profundidade da face sólida.
        // Isso evita "Z-Fighting" (piscar) entre a textura e a malha base.
        glDepthFunc(GL_LEQUAL);

        glColor3f(1.0f, 1.0f, 1.0f); // Cor branca = textura original sem tintura

        for (auto const& [faceIdx, texID] : face_texture_map_) {
            if (faceIdx < 0 || faceIdx >= static_cast<int>(faces_.size())) continue;

            // [LÓGICA DE PRIORIDADE DE SELEÇÃO]
            // Se a face está selecionada, NÃO desenhamos a textura.
            // Queremos ver o "Vermelho" da face base (drawFacesVBO) para indicar seleção.
            bool isSelected = false;
            for(int s : selectedFaces) {
                if(s == faceIdx) { isSelected = true; break; }
            }
            if(isSelected) continue; // Pula essa face

            const auto& face = faces_[faceIdx];
            // Verifica se existem coordenadas UV geradas para esta face
            if (face_uv_map_.find(faceIdx) == face_uv_map_.end()) continue;
            const auto& uvs = face_uv_map_[faceIdx];

            // Vincula a textura correta
            glBindTexture(GL_TEXTURE_2D, texID);

            glBegin(GL_POLYGON);
            for (size_t i = 0; i < face.size(); ++i) {
                // Envia coordenada de textura (UV) antes do vértice
                if (i < uvs.size()) glTexCoord2f(uvs[i].u, uvs[i].v);
                int vIdx = face[i];
                glVertex3f(vertices_[vIdx][0], vertices_[vIdx][1], vertices_[vIdx][2]);
            }
            glEnd();
        }

        // Limpeza de estado
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glDepthFunc(GL_LESS); // Retorna teste de profundidade ao padrão (Less)
    }

    // Desenha o esqueleto da malha (Wireframe)
    // Usa VBOs (Vertex Buffer Objects) para máxima performance.
    void Object::drawEdgesVBO(const Color& color) {
        glColor3f(color[0], color[1], color[2]); // Cor da linha (Preto)
        glLineWidth(2.0f); // Espessura

        // Ativa array de vértices na GPU
        glEnableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices_);
        glVertexPointer(3, GL_FLOAT, 0, nullptr); // Aponta para os dados (3 floats por vértice)

        // Desenha linhas usando índices (IBO de arestas)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_edges_);
        glDrawElements(GL_LINES, static_cast<GLsizei>(edge_index_array_.size()), GL_UNSIGNED_INT, nullptr);

        // Desvincula buffers
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    // Desenha os vértices como pontos
    // Implementa lógica de "Dupla Passada" para destacar seleção
    void Object::drawVerticesVBO(const Color& defaultColor) {
        float tamanhoNormal = 5.0f;
        float tamanhoGrande = 5.0f; // Tamanho igual por decisão de design (pode ser alterado)

        // Cria Set para busca O(1) rápida de vértices selecionados
        std::unordered_set<int> selectedSet(selectedVertices.begin(), selectedVertices.end());

        // --- PASSADA 1: Vértices NÃO Selecionados (Fundo) ---
        glPointSize(tamanhoNormal); // Define tamanho global para este lote
        glBegin(GL_POINTS);
        for (size_t i = 0; i < vertices_.size(); ++i) {
            // Se for selecionado, pula (será desenhado na Passada 2)
            if (selectedSet.count(static_cast<int>(i))) continue;

            Color col = defaultColor;
            if (i < vertexColors.size()) col = vertexColors[i];
            glColor3f(col[0], col[1], col[2]);

            glVertex3f(vertices_[i][0], vertices_[i][1], vertices_[i][2]);
        }
        glEnd();

        // --- PASSADA 2: Vértices SELECIONADOS (Destaque) ---
        if (!selectedVertices.empty()) {
            // [TRUQUE VISUAL] Desabilita Depth Test
            // Faz com que os vértices selecionados sejam desenhados "na frente" de tudo,
            // mesmo que estejam geometricamente atrás de uma face ou linha.
            glDisable(GL_DEPTH_TEST);

            glPointSize(tamanhoGrande);
            glBegin(GL_POINTS);
            for (int idx : selectedVertices) {
                if (idx >= 0 && idx < static_cast<int>(vertices_.size())) {
                    Color col = defaultColor;
                    if (idx < static_cast<int>(vertexColors.size())) col = vertexColors[idx];
                    glColor3f(col[0], col[1], col[2]);

                    glVertex3f(vertices_[idx][0], vertices_[idx][1], vertices_[idx][2]);
                }
            }
            glEnd();

            glEnable(GL_DEPTH_TEST); // Restaura Depth Test
        }
    }

    // ============================================================
    // 4. CONFIGURAÇÃO DE BUFFERS (VBO/IBO)
    // ============================================================

    /*
     * Prepara e envia os dados da malha da RAM (CPU) para a VRAM (GPU).
     * Deve ser chamado sempre que a geometria muda (adição/remoção de vértices).
     */
    void Object::setupVBOs() {
        // 1. Flattening: Converte estruturas complexas (vector<vec3>) em arrays planos (vector<float>)
        vertex_array_.clear();
        for (const auto& v : vertices_) {
            vertex_array_.push_back(v[0]);
            vertex_array_.push_back(v[1]);
            vertex_array_.push_back(v[2]);
        }

        // 2. Prepara índices de faces (Triângulos)
        face_index_array_.clear();
        auto tri_faces = triangulateFaces(faces_);
        for (const auto& tri : tri_faces) {
            face_index_array_.push_back(tri[0]);
            face_index_array_.push_back(tri[1]);
            face_index_array_.push_back(tri[2]);
        }

        // 3. Prepara índices de arestas (Linhas)
        edge_index_array_.clear();
        for (const auto& edge : edges_) {
            edge_index_array_.push_back(edge.first);
            edge_index_array_.push_back(edge.second);
        }

        // 4. Gera Handles OpenGL se não existirem
        if (vbo_vertices_ == 0) glGenBuffers(1, &vbo_vertices_);
        if (ibo_faces_ == 0) glGenBuffers(1, &ibo_faces_);
        if (ibo_edges_ == 0) glGenBuffers(1, &ibo_edges_);

        // 5. Upload dos dados
        // GL_ARRAY_BUFFER: Dados de vértices
        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices_);
        glBufferData(GL_ARRAY_BUFFER, vertex_array_.size() * sizeof(float), vertex_array_.data(), GL_STATIC_DRAW);

        // GL_ELEMENT_ARRAY_BUFFER: Índices (Faces)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_faces_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, face_index_array_.size() * sizeof(unsigned int), face_index_array_.data(), GL_STATIC_DRAW);

        // GL_ELEMENT_ARRAY_BUFFER: Índices (Arestas)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_edges_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, edge_index_array_.size() * sizeof(unsigned int), edge_index_array_.data(), GL_STATIC_DRAW);

        // Desvincula para evitar modificações acidentais
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // Wrapper público para atualizar a geometria
    void Object::updateVBOs() {
        setupVBOs();
    }

} // namespace object