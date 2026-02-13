#ifndef SCREENSHOT_HPP
#define SCREENSHOT_HPP

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>

class Screenshot {
public:
  static void Capture(GLFWwindow *window);
};

#endif
