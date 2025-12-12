#include "controls.h"
#include <cctype>
#include <iostream>
#include <set>
#include <vector>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glew.h>
#include <GL/glut.h>
#endif

#include "../models/object/Object.h"
#include "../models/file_io/file_io.h"
#include "tinyfiledialogs.h"
#include "../render/PathTracer.h"
#include <queue>

// -------------------------------------------------------
// VARIÁVEIS GLOBAIS EXTERNAS (Definidas no main.cpp)
// -------------------------------------------------------
extern object::Object* g_object;
extern float g_zoom;
extern bool g_vertex_only_mode;
extern bool g_face_only_mode;
extern float g_rotation_x;
extern float g_rotation_y;
extern float g_offset_x;
extern float g_offset_y;

// Variáveis do Path Tracing
extern bool g_pathTracingMode;
extern std::vector<Vec3> g_ptVertices;
extern std::vector<std::vector<unsigned int>> g_ptFaces;

// Declaração da função de inicialização
void initPathTracingTexture(int w, int h);

// -------------------------------------------------------

namespace {
    std::set<unsigned char> keysDown;
    std::set<int> specialKeysDown;
    static int lastLeftClickTime = 0;

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
        const float rotationStep = 1.0f;
        if (keysDown.count('w')) rotation_x -= rotationStep;
        if (keysDown.count('s')) rotation_x += rotationStep;
        if (keysDown.count('a')) rotation_y -= rotationStep;
        if (keysDown.count('d')) rotation_y += rotationStep;
    }

    void processZoom(float &zoom, unsigned char key, int modifiers) {
        const float zoomStep = 0.05f;
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
        const float moveStep = 0.05f;
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
        else if (lowerKey == 'p') {
            // --- LÓGICA DO PATH TRACING (ATUALIZADA) ---
            g_pathTracingMode = !g_pathTracingMode;

            if (g_pathTracingMode) {
                std::cout << "Path Tracing Ativado! Sincronizando malha..." << std::endl;

                // 1. Acessa os dados ATUAIS do objeto editado
                // (Assumindo que g_object tem os métodos getVertices e getFaces)
                const auto& currentVertices = g_object->getVertices();
                const auto& currentFaces = g_object->getFaces();

                if (currentVertices.empty()) {
                    std::cerr << "Erro: Objeto vazio." << std::endl;
                    g_pathTracingMode = false;
                    return;
                }

                // 2. Recalcula Bounding Box para Centralizar e Escalar
                // (Precisamos refazer isso pois a nova face pode ter mudado o tamanho do objeto)
                float minX = currentVertices[0][0], maxX = currentVertices[0][0];
                float minY = currentVertices[0][1], maxY = currentVertices[0][1];
                float minZ = currentVertices[0][2], maxZ = currentVertices[0][2];

                for (const auto& v : currentVertices) {
                    if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
                    if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
                    if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
                }

                float centerX = (minX + maxX) / 2.0f;
                float centerY = (minY + maxY) / 2.0f;
                float centerZ = (minZ + maxZ) / 2.0f;

                float w = maxX - minX;
                float h = maxY - minY;
                float d = maxZ - minZ;
                float maxDim = std::max(std::max(w, h), d);

                // Fator de escala para o Path Tracer (Alvo: tamanho ~2.0)
                float scale = 2.0f / (maxDim > 0 ? maxDim : 1.0f);

                // 3. Prepara a Cena Estática para o Renderizador
                static SceneData scene;

                // Limpa lixo anterior da BVH para evitar memory leak
                if (scene.bvhRoot) {
                    delete scene.bvhRoot; // O destrutor da SceneData cuida recursivamente
                    scene.bvhRoot = nullptr;
                }
                scene.vertices.clear();
                scene.faces.clear();
                scene.triIndices.clear();

                // 4. Copia e Transforma Vértices (Objeto -> PathTracer)
                for (const auto& v : currentVertices) {
                    scene.vertices.push_back(Vec3(
                        (v[0] - centerX) * scale,
                        (v[1] - centerY) * scale,
                        (v[2] - centerZ) * scale
                    ));
                }

                // 5. Copia Faces (com Triangulação Automática)
                for (const auto& face : currentFaces) {
                    if (face.size() == 3) {
                        // Triângulo normal: copia direto
                        std::vector<unsigned int> f_uint;
                        for(auto idx : face) f_uint.push_back(static_cast<unsigned int>(idx));
                        scene.faces.push_back(f_uint);
                    }
                    else if (face.size() == 4) {
                        // Quadrilátero: Divide em 2 triângulos
                        // Triângulo A: vértices 0, 1, 2
                        std::vector<unsigned int> t1;
                        t1.push_back(static_cast<unsigned int>(face[0]));
                        t1.push_back(static_cast<unsigned int>(face[1]));
                        t1.push_back(static_cast<unsigned int>(face[2]));
                        scene.faces.push_back(t1);

                        // Triângulo B: vértices 0, 2, 3
                        std::vector<unsigned int> t2;
                        t2.push_back(static_cast<unsigned int>(face[0]));
                        t2.push_back(static_cast<unsigned int>(face[2]));
                        t2.push_back(static_cast<unsigned int>(face[3]));
                        scene.faces.push_back(t2);
                    }
                    else if (face.size() > 4) {
                        // Polígono Genérico (Fan Triangulation):
                        // Cria triângulos conectando o primeiro vértice a todos os outros
                        for (size_t i = 1; i < face.size() - 1; ++i) {
                            std::vector<unsigned int> t;
                            t.push_back(static_cast<unsigned int>(face[0]));
                            t.push_back(static_cast<unsigned int>(face[i]));
                            t.push_back(static_cast<unsigned int>(face[i + 1]));
                            scene.faces.push_back(t);
                        }
                    }
                }

                // 6. Constrói a Aceleração (BVH)
                std::cout << "Construindo BVH para " << scene.faces.size() << " faces..." << std::endl;
                buildBVH(scene);

                // Conecta e Inicia
                g_renderMesh = &scene;

                int winW = glutGet(GLUT_WINDOW_WIDTH);
                int winH = glutGet(GLUT_WINDOW_HEIGHT);
                initPathTracingTexture(winW, winH);

            } else {
                std::cout << "Voltando para OpenGL..." << std::endl;
                g_renderMesh = nullptr;
            }
            glutPostRedisplay();
        }
        else if (lowerKey == 'a') {
            // SHIFT + A: Selecionar Objeto Conectado (Híbrido)
            if (modifiers & GLUT_ACTIVE_SHIFT) {
                if (g_object->getSelectedFaces().empty()) {
                    std::cout << "Selecione pelo menos uma face antes de usar Shift+A." << std::endl;
                }
                else {
                    // Pega a face "semente"
                    int seedFace = g_object->getSelectedFaces().front();
                    bool groupSelected = false;

                    // ---------------------------------------------------------
                    // 1. TENTATIVA VIA GRUPO (OBJ com usemtl/o/g) - O(N)
                    // ---------------------------------------------------------
                    const auto& cells = g_object->getFaceCells();

                    // Verifica se existem células e se o ID é válido
                    // Nota: O leitor OBJ define -1 para faces sem grupo.
                    // Como o vetor é unsigned int, -1 vira o maior valor possível (0xFFFFFFFF).
                    if (!cells.empty() && seedFace < cells.size()) {
                        unsigned int groupID = cells[seedFace];

                        // Se o ID for válido (diferente de -1/UINT_MAX), usa o método de grupo
                        if (groupID != 0xFFFFFFFF) {
                            std::cout << "Selecionando por Grupo definido no arquivo (ID: " << groupID << ")..." << std::endl;
                            g_object->selectFacesByGroup(seedFace);
                            groupSelected = true;
                        }
                    }

                    // ---------------------------------------------------------
                    // 2. FALLBACK VIA GEOMETRIA (BFS / Flood Fill) - O(V+E)
                    // ---------------------------------------------------------
                    // Executa se não houve seleção por grupo (arquivos OFF, STL ou OBJ simples)
                    if (!groupSelected) {
                        std::cout << "Grupo nao detectado. Usando topologia geometrica (BFS)..." << std::endl;

                        // [SEU CÓDIGO ORIGINAL DE BFS]
                        std::cout << "Atualizando topologia e expandindo selecao..." << std::endl;

                        g_object->updateConnectivity();

                        const auto& adjList = g_object->getFaceAdjacency();
                        int numFaces = (int)g_object->getFaces().size();

                        std::vector<bool> visited(numFaces, false);
                        std::queue<int> q;

                        for (int fIdx : g_object->getSelectedFaces()) {
                            if (fIdx < numFaces) {
                                visited[fIdx] = true;
                                q.push(fIdx);
                            }
                        }

                        while (!q.empty()) {
                            int current = q.front();
                            q.pop();

                            if (current < 0 || current >= adjList.size()) continue;

                            for (int neighbor : adjList[current]) {
                                if (neighbor >= 0 && neighbor < numFaces && !visited[neighbor]) {
                                    visited[neighbor] = true;
                                    q.push(neighbor);

                                    g_object->getSelectedFaces().push_back(neighbor);
                                    g_object->setFaceColor(neighbor, {1.0f, 0.0f, 0.0f});
                                }
                            }
                        }
                        std::cout << "Concluido (Geometria)." << std::endl;
                    }
                }
            }
            else {
               keyDown(key);
            }
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
            else if (g_object->getSelectedFaces().size() < 3 || g_object->getSelectedFace() == -1) {
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