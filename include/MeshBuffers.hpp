#ifndef MESH_BUFFERS_HPP
#define MESH_BUFFERS_HPP

#include <GL/glew.h>
#include <string>

struct MeshBuffers {
  GLuint vao = 0, vbo = 0, ebo = 0;
  int indexCount = 0;
  GLuint texture = 0;

  // Per-mesh rendering flags (parsed from texture name suffixes)
  bool hasAlpha = false;  // Texture has alpha channel (32-bit TGA / RGBA)
  bool noneBlend = false; // _N suffix: disable blending, render opaque
  bool hidden = false;    // _H suffix: skip rendering entirely
  bool bright = false;    // _R suffix: additive blending

  std::string textureName; // Original BMD texture name (for debugging)
};

#endif // MESH_BUFFERS_HPP
