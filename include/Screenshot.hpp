#ifndef SCREENSHOT_HPP
#define SCREENSHOT_HPP

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class Screenshot {
public:
  static void Capture(GLFWwindow *window, const std::string &filename = "");

  // Animated GIF capture: accumulate frames then write
  static void BeginGif(int width, int height, float scale = 1.0f,
                       int skipCount = 0);
  static void AddGifFrame(GLFWwindow *window);
  static void SaveGif(const std::string &path,
                      int delayCs = 4); // delay in centiseconds

private:
  struct GifFrame {
    std::vector<unsigned char> pixels; // RGB, flipped
    int width, height;
  };
  static std::vector<GifFrame> gifFrames;
  static int gifWidth, gifHeight;
};

#endif
