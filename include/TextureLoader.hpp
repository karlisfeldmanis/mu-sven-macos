#ifndef TEXTURE_LOADER_HPP
#define TEXTURE_LOADER_HPP

#include <GL/glew.h>
#include <string>
#include <vector>

class TextureLoader {
public:
  static GLuint LoadOZJ(const std::string &path);
  static std::vector<unsigned char> LoadOZJRaw(const std::string &path,
                                               int &width, int &height);
  static GLuint LoadOZT(const std::string &path);
};

#endif
