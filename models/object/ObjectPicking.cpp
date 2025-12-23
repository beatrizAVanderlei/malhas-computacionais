/*
 * ======================================================================================
 * OBJECT PICKING - MÓDULO DE SELEÇÃO E INTERAÇÃO
 * ======================================================================================
 * * Este arquivo implementa a lógica para traduzir cliques 2D do mouse em seleção de
 * elementos 3D (Faces e Vértices).
 * * TÉCNICAS UTILIZADAS:
 * * 1. COLOR PICKING (Seleção por Cor):
 * - Em vez de usar Ray Casting matemático (intersecção raio-triângulo na CPU), usamos
 * a GPU para "renderizar" a cena em um buffer oculto.
 * - Cada elemento (face ou vértice) é desenhado com uma cor única que codifica seu ID (Índice).
 * - Exemplo: O elemento de índice 10 vira a cor RGB(0, 0, 10).
 * - Ao ler a cor do pixel sob o mouse (`glReadPixels`), decodificamos o ID do objeto clicado.
 * - Vantagem: Precisão de pixel perfeita e performance constante independente da complexidade da geometria.
 * * 2. SINCRONIZAÇÃO DE CÂMERA:
 * - Para que o clique corresponda ao que o usuário vê, a matriz de projeção e transformação
 * usada no picking deve ser IDÊNTICA à usada na renderização visual.
 * * 3. CODIFICAÇÃO DE ID EM RGB:
 * - Um inteiro de 32 bits é quebrado em 3 bytes (R, G, B).
 * - R = (ID >> 16) & 0xFF
 * - G = (ID >> 8) & 0xFF
 * - B = (ID) & 0xFF
 * * ======================================================================================
 */

#include "object.h"
#include <iostream>
#include <vector>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// ============================================================
// VARIÁVEIS GLOBAIS DE CÂMERA
// ============================================================

extern float g_offset_x;
extern float g_offset_y;
extern float g_zoom;
extern float g_rotation_x;
extern float g_rotation_y;

namespace object {
    // ============================================================
    // 1. HELPER DE TRANSFORMAÇÃO (Matriz ModelView)
    // ============================================================

    // Aplica as transformações geométricas para alinhar o "mundo do picking" com o "mundo visual".
    static void applyPickingTransform(const std::array<float, 3> &pos, float scale) {
        // 1. Transformações da Câmera (Global/View Matrix)
        glTranslatef(g_offset_x, g_offset_y, 0.0f); // Pan
        glScalef(g_zoom, g_zoom, g_zoom); // Zoom
        glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f); // Rotação X
        glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f); // Rotação Y

        // 2. Transformações do Objeto (Local/Model Matrix)
        glTranslatef(pos[0], pos[1], pos[2]);
        glScalef(scale, scale, scale);
    }

    // ============================================================
    // 2. SELEÇÃO DE FACES (COLOR PICKING)
    // ============================================================

    int Object::pickFace(int mouseX, int mouseY, const int viewport[4]) const {
        // Salva estado atual do OpenGL (cores, luzes, texturas) para restaurar depois.
        // O picking é uma operação "invisível" e não deve afetar a tela.
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glDisable(GL_DITHER);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Limpa o buffer de cor e profundidade para começar o desenho do ID map
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPushMatrix();
        applyPickingTransform(position_, scale_);

        // Obtém a geometria triangulada
        auto tri_faces = triangulateFaces(faces_);

        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < tri_faces.size(); ++i) {
            unsigned int index = static_cast<unsigned int>(i);

            // CODIFICAÇÃO: ID (Int) -> Cor (RGB)
            // Bitwise shift para separar os canais de cor
            float r = ((index >> 16) & 0xFF) / 255.0f;
            float g = ((index >> 8) & 0xFF) / 255.0f;
            float b = (index & 0xFF) / 255.0f;

            glColor3f(r, g, b); // Esta cor representa o ID 'index'

            // Desenha o triângulo
            for (int j = 0; j < 3; ++j) {
                unsigned int vertexIndex = tri_faces[i][j];
                const std::array<float, 3> &vertex = vertices_[vertexIndex];
                glVertex3f(vertex[0], vertex[1], vertex[2]);
            }
        }
        glEnd();
        glPopMatrix();

        glFlush(); // Força a GPU a terminar o desenho antes de lermos o pixel

        // LEITURA DO PIXEL (GPU -> CPU)
        int realY = viewport[3] - mouseY;

        unsigned char pixel[3];
        glReadPixels(mouseX, realY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

        glPopAttrib(); // Restaura o OpenGL para o estado normal de renderização

        // DECODIFICAÇÃO: Cor (RGB) -> ID (Int)
        // Reconstrói o inteiro a partir dos bytes de cor
        int pickedTriangleIndex = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];

        // Mapeia o triângulo clicado de volta para a Face Original (N-Gono)
        auto it = faceTriangleMap.find(pickedTriangleIndex);
        if (it != faceTriangleMap.end()) {
            std::cout << "Face original selecionada: " << it->second << std::endl;
            return it->second;
        }

        return -1;
    }

    // ============================================================
    // 3. SELEÇÃO DE VÉRTICES (COLOR PICKING)
    // ============================================================

    int Object::pickVertex(int mouseX, int mouseY, const int viewport[4]) const {
        glPushAttrib(GL_ALL_ATTRIB_BITS);

        glDisable(GL_DITHER);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPushMatrix();
        applyPickingTransform(position_, scale_);

        // Aumenta o tamanho do ponto renderizado para facilitar o clique.
        glPointSize(10.0f);

        glBegin(GL_POINTS);
        for (size_t i = 0; i < vertices_.size(); ++i) {
            unsigned int index = static_cast<unsigned int>(i);

            // Codificação ID -> Cor
            float r = ((index >> 16) & 0xFF) / 255.0f;
            float g = ((index >> 8) & 0xFF) / 255.0f;
            float b = (index & 0xFF) / 255.0f;

            glColor3f(r, g, b);

            const std::array<float, 3> &vertex = vertices_[i];
            glVertex3f(vertex[0], vertex[1], vertex[2]);
        }
        glEnd();
        glPopMatrix();

        glFlush();

        int realY = viewport[3] - mouseY;
        unsigned char pixel[3];
        glReadPixels(mouseX, realY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

        glPopAttrib();

        // Decodificação Cor -> ID
        int pickedIndex = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];

        // Validação de segurança
        if (pickedIndex >= static_cast<int>(vertices_.size()))
            return -1;

        return pickedIndex;
    }

    // ============================================================
    // 4. SELEÇÃO LÓGICA (POR GRUPOS)
    // ============================================================
    /*
     * Seleciona faces semanticamente conectadas.
     * Baseia-se em IDs de grupo (`face_cells`) carregados do arquivo (tags 'g' ou 'usemtl').
     */
    void Object::selectFacesByGroup(int faceIndex) {
        // Validação de segurança
        if (faceIndex < 0 || faceIndex >= static_cast<int>(face_cells_.size())) return;

        // Identifica o Grupo da face clicada
        unsigned int targetID = face_cells_[faceIndex];
        std::cout << "Selecionando grupo ID: " << targetID << std::endl;

        // Varredura linear para encontrar todos os membros do grupo
        for (size_t i = 0; i < face_cells_.size(); ++i) {
            if (face_cells_[i] == targetID) {
                selectedFaces.push_back(static_cast<int>(i));
                faceColors[i] = {1.0f, 0.0f, 0.0f};
            }
        }
    }
} // namespace object
