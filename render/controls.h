#ifndef CONTROLS_H
#define CONTROLS_H

namespace controls {

 // Callbacks do GLUT
 void keyboardDownCallback(unsigned char key, int x, int y);
 void keyboardUpCallback(unsigned char key, int x, int y);
 void specialKeyboardDownCallback(int key, int x, int y);
 void specialKeyboardUpCallback(int key, int x, int y);
 void mouseCallback(int button, int state, int x, int y);

 // Funções auxiliares
 void keyDown(unsigned char key);
 void keyUp(unsigned char key);
 void updateRotation(float &rotation_x, float &rotation_y);
 void processZoom(float &zoom, unsigned char key, int modifiers);
 void specialKeyDown(int key);
 void specialKeyUp(int key);
 void updateNavigation(float &offset_x, float &offset_y);
}

#endif
