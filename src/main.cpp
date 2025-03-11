#include <iostream>
#include <string>
#include <vector>
#include <array>


#include "../models/file_io/file_io.h"
#include "../models/object/Object.h"
#include "performance.h"
#include "performance-no-prep.h"


#include "../render/render.h"
#include "../render/controls.h"
#include <GL/glew.h>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// -----------------------
// Modo Gráfico (aplicação)
// -----------------------

object::Object* g_object = nullptr;
float g_rotation_x = 0.0f;
float g_rotation_y = 0.0f;
float g_offset_x = 0.0f;
float g_offset_y = 0.0f;
float g_zoom = 1.0f;
bool g_vertex_only_mode = false;
bool g_face_only_mode = false;

void displayCallback() {
    render::ColorsMap colors;
    colors["surface"] = {0.8f, 0.8f, 0.8f};
    colors["edge"]    = {0.0f, 0.0f, 0.0f};
    colors["vertex"]  = {0.0f, 0.0f, 0.0f};


    glClearColor(1.0f,1.0f,1.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
        glTranslatef(g_offset_x, g_offset_y, 0.0f);
        glScalef(g_zoom, g_zoom, g_zoom);
        glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f);
        glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f);
        g_object->draw(colors, g_vertex_only_mode, g_face_only_mode);
    glPopMatrix();

    glutSwapBuffers();
}

void reshapeCallback(int width, int height) {
    render::setup_opengl(width, height);
}

void idleCallback() {
    controls::updateRotation(g_rotation_x, g_rotation_y);
    controls::updateNavigation(g_offset_x, g_offset_y);
    glutPostRedisplay();
}

void runGraphicalApp(int argc, char** argv) {
    // Inicializa o GLUT e cria a janela
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    int winWidth = 800, winHeight = 600;
    glutInitWindowSize(winWidth, winHeight);
    glutCreateWindow("Visualizador de Malha - OpenGL");
    glutSetKeyRepeat(GLUT_KEY_REPEAT_ON);

#ifndef __APPLE__
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "Erro ao inicializar GLEW: " << glewGetErrorString(err) << std::endl;
        exit(EXIT_FAILURE);
    }
#endif

    render::setup_opengl(winWidth, winHeight);

    int detection_size = 100;
    // Carrega a malha e cria o objeto gráfico
    std::string filename = "../assets/cubo.off";
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Calcula o bounding box
    float minX = vertices[0][0], maxX = vertices[0][0];
    float minY = vertices[0][1], maxY = vertices[0][1];
    float minZ = vertices[0][2], maxZ = vertices[0][2];
    for (const auto &v : vertices) {
        if (v[0] < minX) minX = v[0];
        if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1];
        if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2];
        if (v[2] > maxZ) maxZ = v[2];
    }

    // Calcula o centro da malha
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    float centerZ = (minZ + maxZ) / 2.0f;

    // Desloca os vértices para que o centro fique na origem (0,0,0)
    for (auto &v : vertices) {
        v[0] -= centerX;
        v[1] -= centerY;
        v[2] -= centerZ;
    }

    // Calcula o zoom automático baseado no tamanho da bounding box
    float width = maxX - minX;
    float height = maxY - minY;
    float depth = maxZ - minZ;
    float maxDimension = std::max(std::max(width, height), depth);
    float desiredSize = 2.0f;
    float initialScale = desiredSize / maxDimension;
    g_zoom = initialScale;

    // Converte as faces da malha para o formato utilizado
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face)
            f.push_back(static_cast<unsigned int>(idx));
        faces.push_back(f);
    }

    // Como os vértices já foram centralizados, podemos usar posição 0 para o objeto
    std::vector<unsigned int> face_cells;
    std::array<float, 3> position = { 0.0f, 0.0f, 0.0f };
    g_object = new object::Object(position, vertices, faces, face_cells, filename, detection_size, true);
    g_object->clearColors();

    // Registra os callbacks do GLUT
    glutDisplayFunc(displayCallback);
    glutReshapeFunc(reshapeCallback);
    glutKeyboardFunc(controls::keyboardDownCallback);
    glutKeyboardUpFunc(controls::keyboardUpCallback);
    glutSpecialFunc(controls::specialKeyboardDownCallback);
    glutSpecialUpFunc(controls::specialKeyboardUpCallback);
    glutIdleFunc(idleCallback);
    glutMouseFunc(controls::mouseCallback);

    // Inicia o loop do GLUT
    glutMainLoop();

    delete g_object;
}

// -----------------------
// Modo Performance Test
// -----------------------

void runPerformanceTest() {
    std::string filename = "../assets/tetrahedraChineseDragon2225RefinedFileCut.vtk";

    std::cout << "Modo de teste de desempenho iniciado." << std::endl;

    // Carrega a malha
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face) {
            f.push_back(static_cast<unsigned int>(idx));
        }
        faces.push_back(f);
    }

    // Se houver dados de face_cells, utilize-os; caso contrário, vetor vazio
    std::vector<unsigned int> face_cells;
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    int detection_size = 100;

    object::Object obj(position, vertices, faces, face_cells, filename, detection_size, false);

    exportPerformanceData(obj, "C:/Users/bia/CLionProjects/teste/src/prep/performance-dragon.csv");

    std::cout << "Teste de desempenho finalizado." << std::endl;
}

void runPerformanceTestNoPrep() {
    std::string filename = "../assets/tetrahedraChineseDragon2225RefinedFileCut.vtk";

    std::cout << "Modo de teste de desempenho iniciado." << std::endl;

    // Carrega a malha
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face) {
            f.push_back(static_cast<unsigned int>(idx));
        }
        faces.push_back(f);
    }

    // Se houver dados de face_cells, utilize-os; caso contrário, vetor vazio
    std::vector<unsigned int> face_cells;
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    int detection_size = 100;

    object::Object obj(position, vertices, faces, face_cells, filename, detection_size, false);

    exportPerformanceDataNoPrep(obj, "C:/Users/bia/CLionProjects/teste/src/no-prep/performance-dragon-no-prep.csv");

    std::cout << "Teste de desempenho finalizado." << std::endl;
}


// -----------------------
// Main: escolhe o modo com base no argumento de linha de comando
// -----------------------

int main(int argc, char** argv) {
    // Se receber um argumento, verifique: "0" para performance test, "1" para a aplicação gráfica.
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "0") {
            runPerformanceTest();
        } else if (mode == "1") {
            runGraphicalApp(argc, argv);
        } else if (mode == "2") {
            runPerformanceTestNoPrep();
        } else {
            std::cerr << "Modo inválido. Use '0' para teste de desempenho ou '1' para aplicação gráfica." << std::endl;
            return EXIT_FAILURE;
        }
    } else {
        // Se nenhum argumento for passado a aplicação padrão será a aplicação gráfica
        runGraphicalApp(argc, argv);
    }
    return 0;
}
