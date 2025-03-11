#include "object.h"
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

// -----------------------
// Metodo escolhido eh o picking de cor: atribuir uma cor a cada vertice/face de acordo com seus pixels
// -----------------------

namespace object {

    int Object::pickFace(int mouseX, int mouseY, const int viewport[4]) const {
        glPushAttrib(GL_ALL_ATTRIB_BITS);

        glDisable(GL_DITHER);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPushMatrix();
        glTranslatef(g_offset_x, g_offset_y, 0.0f);
        glScalef(g_zoom, g_zoom, g_zoom);
        glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f);
        glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f);
        glTranslatef(position_[0], position_[1], position_[2]);
        glScalef(scale_, scale_, scale_);

        auto tri_faces = triangulateFaces(faces_);
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < tri_faces.size(); ++i) {
            unsigned int index = static_cast<unsigned int>(i);
            float r = ((index >> 16) & 0xFF) / 255.0f;
            float g = ((index >> 8) & 0xFF) / 255.0f;
            float b = (index & 0xFF) / 255.0f;
            glColor3f(r, g, b);
            for (int j = 0; j < 3; ++j) {
                unsigned int vertexIndex = tri_faces[i][j];
                const std::array<float, 3>& vertex = vertices_[vertexIndex];
                glVertex3f(vertex[0], vertex[1], vertex[2]);
            }
        }
        glEnd();
        glPopMatrix();

        glFlush();

        int realY = viewport[3] - mouseY;
        unsigned char pixel[3];
        glReadPixels(mouseX, realY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

        glPopAttrib();

        int pickedTriangleIndex = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];

        std::cout << "Triangulo selecionado: " << pickedTriangleIndex << std::endl;

        auto it = faceTriangleMap.find(pickedTriangleIndex);
        if (it != faceTriangleMap.end()) {
            std::cout << "Face original: " << it->second << std::endl;
            return it->second;
        }
        return -1;
    }

    int Object::pickVertex(int mouseX, int mouseY, const int viewport[4]) const {
        glPushAttrib(GL_ALL_ATTRIB_BITS);

        glDisable(GL_DITHER);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPushMatrix();

        glTranslatef(g_offset_x, g_offset_y, 0.0f);
        glScalef(g_zoom, g_zoom, g_zoom);
        glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f);
        glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f);
        glTranslatef(position_[0], position_[1], position_[2]);
        glScalef(scale_, scale_, scale_);

        glPointSize(10.0f);
        glBegin(GL_POINTS);
        for (size_t i = 0; i < vertices_.size(); ++i) {
            unsigned int index = static_cast<unsigned int>(i);
            float r = ((index >> 16) & 0xFF) / 255.0f;
            float g = ((index >> 8) & 0xFF) / 255.0f;
            float b = (index & 0xFF) / 255.0f;
            glColor3f(r, g, b);
            const std::array<float, 3>& vertex = vertices_[i];
            glVertex3f(vertex[0], vertex[1], vertex[2]);
        }
        glEnd();
        glPopMatrix();

        glFlush();

        int realY = viewport[3] - mouseY;
        unsigned char pixel[3];
        glReadPixels(mouseX, realY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

        glPopAttrib();

        int pickedIndex = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
        if (pickedIndex >= static_cast<int>(vertices_.size()))
            return -1;
        return pickedIndex;
    }
}
