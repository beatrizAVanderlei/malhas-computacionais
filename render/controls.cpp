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
            g_pathTracingMode = !g_pathTracingMode;

            if (g_pathTracingMode) {
                std::cout << "Path Tracing Ativado! Sincronizando malha e processando texturas..." << std::endl;

                const auto& currentVertices = g_object->getVertices();
                const auto& currentFaces = g_object->getFaces();

                // Getters do Object
                const auto& textureCache = g_object->getTextureCache();
                const auto& faceTexMap = g_object->getFaceTextureMap();
                const auto& faceUvMap = g_object->getFaceUvMap();

                if (currentVertices.empty()) { g_pathTracingMode = false; return; }

                // 1. Normalização e Escala
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
                float w = maxX - minX, h = maxY - minY, d = maxZ - minZ;
                float maxDim = std::max(std::max(w, h), d);
                float scale = 2.0f / (maxDim > 0 ? maxDim : 1.0f);

                // 2. Prepara SceneData
                static SceneData scene;
                if (scene.bvhRoot) { delete scene.bvhRoot; scene.bvhRoot = nullptr; }
                scene.vertices.clear(); scene.faces.clear(); scene.triIndices.clear();
                scene.textures.clear(); scene.faceTextureID.clear(); scene.faceUVs.clear();

                for (const auto& v : currentVertices) {
                    scene.vertices.push_back(Vec3(
                        (v[0] - centerX) * scale,
                        (v[1] - centerY) * scale,
                        (v[2] - centerZ) * scale
                    ));
                }

                // 3. PROCESSAMENTO OTIMIZADO DE TEXTURAS (Pré-cálculo)
                std::map<GLuint, int> glToPtMap;

                for (auto const& [glID, rawData] : textureCache) {
                    TextureData tex;
                    tex.width = rawData.width;
                    tex.height = rawData.height;

                    // Aloca memória para floats (R, G, B)
                    int numPixels = tex.width * tex.height;
                    tex.pixels.resize(numPixels * 3);

                    // [OTIMIZAÇÃO] Converte Byte->Float e aplica Gama/Boost AQUI
                    // Isso evita fazer pow() milhões de vezes no render loop.
                    for (int i = 0; i < numPixels * 3; ++i) {
                        float val = rawData.pixels[i] / 255.0f; // Normaliza 0..1

                        // Aplica Correção Gama (sRGB -> Linear) e Boost de Vivacidade (1.3x)
                        tex.pixels[i] = std::pow(val, 2.2f) * 1.3f;
                    }

                    scene.textures.push_back(tex);
                    glToPtMap[glID] = (int)scene.textures.size() - 1;
                }

                // 4. Copia Faces e UVs
                for (size_t fIdx = 0; fIdx < currentFaces.size(); ++fIdx) {
                    const auto& face = currentFaces[fIdx];

                    int currentTexID = -1;
                    if (faceTexMap.count(fIdx) && glToPtMap.count(faceTexMap.at(fIdx))) {
                        currentTexID = glToPtMap.at(faceTexMap.at(fIdx));
                    }

                    // Conversão Vec2 -> PtVec2
                    std::vector<PtVec2> originalUVs;
                    if (currentTexID != -1 && faceUvMap.count(fIdx)) {
                        for (const auto& uv : faceUvMap.at(fIdx)) {
                            originalUVs.push_back({uv.u, uv.v});
                        }
                    }

                    if (face.size() == 3) {
                        std::vector<unsigned int> tri = {face[0], face[1], face[2]};
                        scene.faces.push_back(tri);
                        scene.faceTextureID.push_back(currentTexID);

                        if(currentTexID != -1 && originalUVs.size() >= 3)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[1], originalUVs[2]});
                        else
                            scene.faceUVs.push_back({});
                    }
                    else if (face.size() == 4) {
                        // Tri 1
                        scene.faces.push_back({face[0], face[1], face[2]});
                        scene.faceTextureID.push_back(currentTexID);
                        if(currentTexID != -1 && originalUVs.size() >= 4)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[1], originalUVs[2]});
                        else scene.faceUVs.push_back({});

                        // Tri 2
                        scene.faces.push_back({face[0], face[2], face[3]});
                        scene.faceTextureID.push_back(currentTexID);
                        if(currentTexID != -1 && originalUVs.size() >= 4)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[2], originalUVs[3]});
                        else scene.faceUVs.push_back({});
                    }
                    else if (face.size() > 4) {
                        for (size_t i = 1; i < face.size() - 1; ++i) {
                            scene.faces.push_back({face[0], face[i], face[i + 1]});
                            scene.faceTextureID.push_back(currentTexID);
                            if(currentTexID != -1 && originalUVs.size() >= face.size())
                                scene.faceUVs.push_back({originalUVs[0], originalUVs[i], originalUVs[i+1]});
                            else scene.faceUVs.push_back({});
                        }
                    }
                }

                std::cout << "Construindo BVH..." << std::endl;
                buildBVH(scene);
                g_renderMesh = &scene;

                int winW = glutGet(GLUT_WINDOW_WIDTH);
                int winH = glutGet(GLUT_WINDOW_HEIGHT);
                initPathTracingTexture(winW, winH);

            } else {
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
        // Em keyboardDownCallback:
        // Em controls.cpp -> keyboardDownCallback

        else if (lowerKey == 't') {
            // Verifica se existe ALGUMA seleção (1 ou mais faces)
            if (!g_object->getSelectedFaces().empty()) {

                // Abre o seletor de arquivos
                const char* filepath = tinyfd_openFileDialog(
                    "Selecionar Textura",
                    "",
                    2,
                    (const char*[]){"*.png", "*.jpg"},
                    "Imagens",
                    0
                );

                if (filepath) {
                    // A função interna do objeto cuida de distribuir a textura para todas as faces selecionadas
                    g_object->applyTextureToSelectedFaces(std::string(filepath));
                    glutPostRedisplay();
                }
            } else {
                std::cout << "Selecione uma ou mais faces para aplicar textura." << std::endl;
            }
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