#ifndef RENDER_H
#define RENDER_H

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <map>
#include <string>
#include <array>

namespace render {

    // Definição para cor: vetor com 3 componentes (RGB)
    using Color = std::array<float, 3>;

    // Mapa de cores, com chaves (ex.: "surface", "edge", "vertex")
    using ColorsMap = std::map<std::string, Color>;

    // Interface base para objetos que podem ser renderizados.
    class Drawable {
    public:
        virtual void draw(const ColorsMap &colors, bool vertexOnlyMode, bool faceOnlyMode) = 0;
        virtual ~Drawable() {}
    };

    // Configura o OpenGL (viewport, projeção, câmera e ativação do depth test)
    void setup_opengl(int width, int height);

    // Desenha a cena: limpa a tela com a cor de fundo e chama o metodo draw do objeto.
    void draw_scene(Drawable &obj,
                    bool vertexOnlyMode,
                    bool faceOnlyMode,
                    const Color &background_color = {1.0f, 1.0f, 1.0f},
                    const ColorsMap *colors = nullptr);

}

#endif
