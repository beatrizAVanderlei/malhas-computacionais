#include "render.h"

namespace render {

    void setup_opengl(int width, int height) {
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(45.0, static_cast<double>(width) / static_cast<double>(height), 0.1, 50.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -10.0f);
        glEnable(GL_DEPTH_TEST);
    }

    void draw_scene(Drawable &obj,
                    bool vertexOnlyMode,
                    bool faceOnlyMode,
                    const Color &background_color,
                    const ColorsMap *colors) {
        ColorsMap default_colors;
        if (!colors) {
            default_colors["surface"] = {1.0f, 0.0f, 0.0f};
            default_colors["edge"]    = {19.0f / 255.0f, 19.0f / 255.0f, 19.0f / 255.0f};
            default_colors["vertex"]  = {19.0f / 255.0f, 19.0f / 255.0f, 19.0f / 255.0f};
            colors = &default_colors;
        }

        glClearColor(background_color[0], background_color[1], background_color[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        obj.draw(*colors, vertexOnlyMode, faceOnlyMode);

        glFlush();
    }
}
