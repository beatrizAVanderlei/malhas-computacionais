/*
 * ======================================================================================
 * RENDER.CPP - CONFIGURAÇÃO E ABSTRAÇÃO DO OPENGL
 * ======================================================================================
 * * Este arquivo serve como uma "fachada" (Facade Pattern) para as chamadas de baixo nível
 * da API OpenGL.
 * * RESPONSABILIDADES:
 * * 1. CONFIGURAÇÃO DA CÂMERA (PIPELINE DE TRANSFORMAÇÃO):
 * - Configura a Matriz de Projeção (Lente da câmera): Define como o mundo 3D é achatado
 * na tela 2D (Perspectiva vs Ortográfica).
 * - Configura a Matriz de ModelView (Posição da câmera): Define onde o "olho" está no mundo.
 * * 2. GERENCIAMENTO DE ESTADO (STATE MACHINE):
 * - O OpenGL é uma máquina de estados. Aqui ativamos recursos vitais como o Z-Buffer
 * (Depth Test) para garantir que objetos próximos tampem os distantes.
 * * 3. DESENHO GENÉRICO (ABSTRAÇÃO):
 * - A função `draw_scene` aceita qualquer objeto que herde de `Drawable`. Isso permite
 * que o renderizador desenhe qualquer coisa (malha, partículas, UI) sem conhecer
 * os detalhes de implementação do objeto.
 * * ======================================================================================
 */

#include "render.h"

namespace render {

    /*
     * Configura a janela de visualização e a perspectiva da câmera.
     * Chamada sempre que a janela é redimensionada (evento Reshape).
     */
    void setup_opengl(int width, int height) {
        // 1. VIEWPORT: Mapeia coordenadas normalizadas (-1 a 1) para pixels da tela (0 a width/height).
        // Essencial para saber onde desenhar dentro da janela do sistema operacional.
        glViewport(0, 0, width, height);

        // 2. MATRIZ DE PROJEÇÃO: Define a "lente" da câmera.
        glMatrixMode(GL_PROJECTION); // Seleciona a pilha de matrizes de projeção
        glLoadIdentity();            // Reseta para a matriz identidade (limpa configurações anteriores)

        // Cria um frustum de perspectiva (Cone de visão).
        // - FOV (Field of View): 45.0 graus (similar à visão humana ou lente 50mm).
        // - Aspect Ratio: width/height (garante que a imagem não fique esticada).
        // - Near/Far Planes: 0.1 a 50.0 (Objetos mais perto que 0.1 ou mais longe que 50.0 são cortados).
        gluPerspective(45.0, static_cast<double>(width) / static_cast<double>(height), 0.1, 50.0);

        // 3. MATRIZ MODELVIEW: Define a posição da câmera/mundo.
        glMatrixMode(GL_MODELVIEW); // Seleciona a pilha de matrizes de modelo/visualização
        glLoadIdentity();           // Reseta a câmera para a origem (0,0,0) olhando para -Z

        // Afasta o mundo 10 unidades para trás (-Z).
        // Isso equivale a mover a câmera 10 unidades para trás (+Z), permitindo ver objetos na origem (0,0,0).
        glTranslatef(0.0f, 0.0f, -10.0f);

        // 4. Z-BUFFER (TESTE DE PROFUNDIDADE)
        // Sem isso, o OpenGL desenharia os triângulos na ordem em que chegam.
        // Com isso ativado, ele verifica a profundidade de cada pixel e só desenha se estiver "na frente".
        glEnable(GL_DEPTH_TEST);
    }

    /*
     * Função genérica para limpar a tela e comandar o desenho de um objeto.
     * Recebe uma referência polimórfica `Drawable&`, desacoplando o renderizador da malha específica.
     */
    void draw_scene(Drawable &obj,
                    bool vertexOnlyMode,
                    bool faceOnlyMode,
                    const Color &background_color,
                    const ColorsMap *colors) {
        
        // --- 1. Lógica de Cores Padrão (Fallback) ---
        // Se o usuário não fornecer um mapa de cores, criamos um padrão.
        ColorsMap default_colors;
        if (!colors) {
            // Cinza claro para faces
            default_colors["surface"] = {0.8f, 0.8f, 0.8f}; 
            // Cinza muito escuro (quase preto) para arestas e vértices
            default_colors["edge"]    = {19.0f / 255.0f, 19.0f / 255.0f, 19.0f / 255.0f};
            default_colors["vertex"]  = {19.0f / 255.0f, 19.0f / 255.0f, 19.0f / 255.0f};
            colors = &default_colors; // Aponta para o local (stack) seguro
        }

        // --- 2. Limpeza do Frame Anterior ---
        // Define a cor de fundo (Clear Color)
        glClearColor(background_color[0], background_color[1], background_color[2], 1.0f);
        
        // Limpa os buffers físicos da GPU:
        // - COLOR_BUFFER: A imagem antiga.
        // - DEPTH_BUFFER: As informações de profundidade (Z) antigas.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- 3. Chamada de Desenho (Polimorfismo) ---
        // Delega a lógica de "como desenhar" para o objeto.
        // O renderizador não sabe se é uma esfera, um cubo ou uma malha complexa.
        obj.draw(*colors, vertexOnlyMode, faceOnlyMode);

        // --- 4. Finalização ---
        // Força a execução de todos os comandos OpenGL pendentes na fila do driver.
        // Nota: Em animações com Double Buffer (GLUT_DOUBLE), o glutSwapBuffers() no main.cpp 
        // faz o papel de atualizar a tela, mas glFlush garante que os comandos saíram da CPU.
        glFlush();
    }
}