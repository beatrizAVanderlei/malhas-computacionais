#include "object.h"
#include <iostream>
#include <algorithm>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

namespace object {

    void Object::draw(const ColorsMap& colors, bool vertexOnlyMode, bool faceOnlyMode) {
        glPushMatrix();
        // Aplica transformações do objeto: translação e escala
        glTranslatef(position_[0], position_[1], position_[2]);
        glScalef(scale_, scale_, scale_);

        // Desenha faces se não estiver em modo "apenas vértices"
        if (!vertexOnlyMode) {
            Color faceColor = colors.count("surface") ? colors.at("surface") : Color{1.0f, 0.0f, 0.0f};
            drawFacesVBO(faceColor, vertexOnlyMode);
        }

        // Desenha arestas sempre para manter a conectividade visual
        Color edgeColor = colors.count("edge") ? colors.at("edge") : Color{0.0f, 0.0f, 0.0f};
        drawEdgesVBO(edgeColor);

        // Desenha vértices se não estiver em modo "apenas faces"
        if (!faceOnlyMode) {
            glPointSize(vertexOnlyMode ? 5.0f : 5.0f);
            Color vertexColor = colors.count("vertex") ? colors.at("vertex") : Color{0.0f, 0.0f, 0.0f};
            drawVerticesVBO(vertexColor);  // Chama a função modificada
        }
        glPopMatrix();
    }

    void Object::drawFacesVBO(const Color& defaultColor, bool vertexOnlyMode) {
        if (vertexOnlyMode) return;

        auto tri_faces = triangulateFaces(faces_);
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < tri_faces.size(); ++i) {
            // Recupera o índice da face original usando o mapeamento
            int origFace = static_cast<int>(i);
            auto it = faceTriangleMap.find(static_cast<int>(i));
            if (it != faceTriangleMap.end())
                origFace = it->second;

            Color col = defaultColor;
            if (origFace < static_cast<int>(faceColors.size()))
                col = faceColors[origFace];

            glColor3f(col[0], col[1], col[2]);
            for (int j = 0; j < 3; ++j) {
                unsigned int vertexIndex = tri_faces[i][j];
                const std::array<float, 3>& vertex = vertices_[vertexIndex];
                glVertex3f(vertex[0], vertex[1], vertex[2]);
            }
        }
        glEnd();
    }

    void Object::drawEdgesVBO(const Color& color) {
        glColor3f(color[0], color[1], color[2]);
        glLineWidth(2.0f);

        glEnableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_vertices_);
        glVertexPointer(3, GL_FLOAT, 0, nullptr);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_edges_);
        glDrawElements(GL_LINES, static_cast<GLsizei>(edge_index_array_.size()), GL_UNSIGNED_INT, nullptr);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    void Object::drawVerticesVBO(const Color& defaultColor) {
        float tamanhoPadrao = 5.0f;
        float tamanhoSelecionado = tamanhoPadrao * 2.0f;

        glBegin(GL_POINTS);
        for (size_t i = 0; i < vertices_.size(); ++i) {
            int index = static_cast<int>(i);  // Conversão explícita para int

            // Garantir que selectedVertices é um std::vector<int>
            bool isSelected = (std::find(selectedVertices.begin(), selectedVertices.end(), index) != selectedVertices.end());

            glPointSize(isSelected ? tamanhoSelecionado : tamanhoPadrao);

            Color col = defaultColor;
            if (i < vertexColors.size())
                col = vertexColors[i];
            glColor3f(col[0], col[1], col[2]);

            const std::array<float, 3>& vertex = vertices_[i];
            glVertex3f(vertex[0], vertex[1], vertex[2]);
        }
        glEnd();
    }
}