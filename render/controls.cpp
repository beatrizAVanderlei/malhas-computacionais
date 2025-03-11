#include "controls.h"
#include <cctype>
#include <iostream>
#include <set>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glew.h>
#include <GL/glut.h>
#endif

#include "../models/object/Object.h"
#include "../models/file_io/file_io.h"
#include "tinyfiledialogs.h"

// Variáveis globais
extern object::Object* g_object;
extern float g_zoom;
extern bool g_vertex_only_mode;
extern bool g_face_only_mode;

// Variáveis internas
namespace {
    std::set<unsigned char> keysDown;
    std::set<int> specialKeysDown;
    // Tempo do último clique esquerdo (em ms)
    static int lastLeftClickTime = 0;

    // Função auxiliar para obter o viewport
    void getViewport(int viewport[4]) {
        glGetIntegerv(GL_VIEWPORT, viewport);
    }
}

namespace controls {

    //---------------------------
    // Funções auxiliares de teclado
    //---------------------------

    void keyDown(unsigned char key) {
        keysDown.insert(static_cast<unsigned char>(std::tolower(key)));
    }

    void keyUp(unsigned char key) {
        keysDown.erase(static_cast<unsigned char>(std::tolower(key)));
    }

    void updateRotation(float &rotation_x, float &rotation_y) {
        const float rotationStep = 1.0f;  // ajuste conforme necessário
        if (keysDown.count('w')) rotation_x -= rotationStep;
        if (keysDown.count('s')) rotation_x += rotationStep;
        if (keysDown.count('a')) rotation_y -= rotationStep;
        if (keysDown.count('d')) rotation_y += rotationStep;
    }

    void processZoom(float &zoom, unsigned char key, int modifiers) {
        const float zoomStep = 0.05f;  // ajuste conforme necessário
        if (key == '=' || key == '+' || key == 43)
            zoom += zoomStep;
        else if (key == '-' || key == 45)
            zoom -= zoomStep;
    }

    void specialKeyDown(int key) {
        specialKeysDown.insert(key);
    }

    void specialKeyUp(int key) {
        specialKeysDown.erase(key);
    }

    void updateNavigation(float &offset_x, float &offset_y) {
        const float moveStep = 0.05f;  // ajuste conforme necessário
        if (specialKeysDown.count(GLUT_KEY_UP)) offset_y += moveStep;
        if (specialKeysDown.count(GLUT_KEY_DOWN)) offset_y -= moveStep;
        if (specialKeysDown.count(GLUT_KEY_LEFT)) offset_x -= moveStep;
        if (specialKeysDown.count(GLUT_KEY_RIGHT)) offset_x += moveStep;
    }

    //---------------------------
    // Callbacks do teclado
    //---------------------------

    void keyboardDownCallback(unsigned char key, int x, int y) {
        int modifiers = glutGetModifiers();
        char lowerKey = std::tolower(key);

        if (key == 127) { // DELETE
            g_object->deleteSelectedElements();
            glutPostRedisplay();
        }
        else if (lowerKey == 'k') {
            if (!g_object->getSelectedVertices().empty()) {
                int baseVertex = g_object->getSelectedVertices().front();
                g_object->selectAdjacentVertices(baseVertex);
            }
            else if (!g_object->getSelectedFaces().empty()) {
                int faceIndex = g_object->getSelectedFaces().front();
                g_object->selectVerticesFromFace(faceIndex);
            }
            else {
                std::cout << "Nenhum elemento selecionado para extrair vertices." << std::endl;
            }
        }
        else if (lowerKey == 'l') {
            if (!g_object->getSelectedVertices().empty()) {
                int baseVertex = g_object->getSelectedVertices().front();
                g_object->selectFacesFromVertex(baseVertex);
            }
            else if (!g_object->getSelectedFaces().empty()) {
                int baseFace = g_object->getSelectedFaces().front();
                g_object->selectNeighborFacesFromFace(baseFace);
            }
        }
        else if (lowerKey == 'v') {
            g_vertex_only_mode = !g_vertex_only_mode;
            if (g_vertex_only_mode)
                g_face_only_mode = false;
            std::cout << "Modo apenas vertices: " << (g_vertex_only_mode ? "ativado" : "desativado") << std::endl;
        }
        else if (lowerKey == 'f') {
            // Se houver exatamente 3 ou 4 vértices selecionados, cria uma face
            if (g_object->getSelectedVertices().size() == 3 || g_object->getSelectedVertices().size() == 4)
                g_object->createFaceFromSelectedVertices();
            // Caso contrário, se não houver face selecionada, alterna o modo face‑only
            else if (g_object->getSelectedFaces().empty() && g_object->getSelectedFace() == -1) {
                g_face_only_mode = !g_face_only_mode;
                if (g_face_only_mode)
                    g_vertex_only_mode = false;
                std::cout << "Modo apenas faces: " << (g_face_only_mode ? "ativado" : "desativado") << std::endl;
            }
        }
        else if (lowerKey == 'n') {
            if (!g_object->getSelectedFaces().empty())
                g_object->createVertexAndLinkToSelectedFaces();
            else if (!g_object->getSelectedVertices().empty() && g_object->getSelectedVertices().size() <= 3)
                g_object->createVertexAndLinkToSelected();
            else if (g_object->getSelectedVertices().empty() && g_object->getSelectedFaces().empty() && g_object->getSelectedFace() == -1)
                g_object->createVertexFromDialog();
        }
        else if (lowerKey == 'c') {
            int baseFace = g_object->getSelectedFaces().front();
            g_object->selectCellFromSelectedFace(baseFace);
        }
        else if (lowerKey == 'b') {
            const char* saveFilename = tinyfd_saveFileDialog(
                "Salvar Arquivo",
                "modelo",
                4,
                (const char*[]){"OFF files *.off", "OBJ files *.obj", "STL files *.stl", "VTK files *.vtk"},
                "Formatos Suportados"
            );
            if (saveFilename) {
                try {
                    fileio::save_file(saveFilename, g_object->getVertices(), g_object->getFaces());
                    std::cout << "Arquivo salvo com sucesso: " << saveFilename << std::endl;
                } catch (const std::exception &e) {
                    std::cerr << "Erro ao salvar o arquivo: " << e.what() << std::endl;
                }
            }
        }
        else {
            processZoom(g_zoom, key, modifiers);
            keyDown(key);
        }
        glutPostRedisplay();
    }

    void keyboardUpCallback(unsigned char key, int x, int y) {
        keyUp(key);
    }

    void specialKeyboardDownCallback(int key, int x, int y) {
        specialKeyDown(key);
    }

    void specialKeyboardUpCallback(int key, int x, int y) {
        specialKeyUp(key);
    }

    //---------------------------
    // Callback do mouse
    //---------------------------

    void mouseCallback(int button, int state, int x, int y) {
        if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
            int currentTime = glutGet(GLUT_ELAPSED_TIME);
            int threshold = 300; // tempo para considerar duplo clique
            int viewport[4];
            getViewport(viewport);

            if (!g_face_only_mode && (currentTime - lastLeftClickTime < threshold)) {
                int vertexIndex = g_object->pickVertex(x, y, viewport);
                if (vertexIndex >= 0) {
                    std::cout << "Duplo clique no vértice " << vertexIndex << std::endl;
                    g_object->editVertexCoordinates(vertexIndex);
                    g_object->setVertexColor(vertexIndex, {0.0f, 1.0f, 0.0f});
                    glutPostRedisplay();
                    lastLeftClickTime = currentTime;
                    return;
                }
            }

            lastLeftClickTime = currentTime;

            int modifiers = glutGetModifiers();
            bool multiSelect = (modifiers & GLUT_ACTIVE_SHIFT) != 0;
            if (!multiSelect)
                g_object->clearSelection();

            if (!g_face_only_mode) {
                int newVertex = g_object->pickVertex(x, y, viewport);
                if (newVertex >= 0) {
                    g_object->getSelectedVertices().push_back(newVertex);
                    g_object->setVertexColor(newVertex, {1.0f, 0.0f, 0.0f});
                    std::cout << "Vértice " << newVertex << " selecionado e colorido de vermelho." << std::endl;
                    glutPostRedisplay();
                    return;
                }
            }

            if (!g_vertex_only_mode) {
                int newFace = g_object->pickFace(x, y, viewport);
                if (newFace >= 0) {
                    g_object->getSelectedFaces().push_back(newFace);
                    g_object->setFaceColor(newFace, {1.0f, 0.0f, 0.0f});
                    std::cout << "Face " << newFace << " selecionada e colorida de vermelho." << std::endl;
                    glutPostRedisplay();
                    return;
                }
            }

            std::cout << "Nenhum elemento selecionado." << std::endl;
            g_object->clearSelection();
            glutPostRedisplay();
        }
    }

} // namespace controls
