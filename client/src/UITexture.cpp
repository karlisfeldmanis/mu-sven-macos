#include "UITexture.hpp"
#include "TextureLoader.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static GLuint LoadPNG(const std::string &path) {
  int w, h, channels;
  unsigned char *data =
      stbi_load(path.c_str(), &w, &h, &channels, 4); // force RGBA
  if (!data) {
    printf("[UITexture] Failed to load PNG: %s (%s)\n", path.c_str(),
           stbi_failure_reason());
    return 0;
  }

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  stbi_image_free(data);
  printf("[UITexture] PNG %s: %dx%d (id=%d)\n", path.c_str(), w, h, tex);
  return tex;
}

UITexture UITexture::Load(const std::string &path) {
  UITexture tex;

  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  // Determine format by extension
  bool isOZJ = lower.ends_with(".ozj") || lower.ends_with(".jpg") ||
               lower.ends_with(".jpeg");
  bool isOZT = lower.ends_with(".ozt") || lower.ends_with(".tga");
  bool isPNG = lower.ends_with(".png");

  if (isPNG) {
    tex.id = LoadPNG(path);
    tex.isOZT = false; // PNG has correct orientation for ImGui
    tex.hasAlpha = true;
  } else if (isOZJ) {
    tex.id = TextureLoader::LoadOZJ(path);
    tex.isOZT = false;
    tex.hasAlpha = false;
  } else if (isOZT) {
    tex.id = TextureLoader::LoadOZT(path);
    tex.isOZT = true; // V-flipped by loader, needs flip back for ImGui
    // Check if RGBA
    if (tex.id) {
      GLint internalFormat;
      glBindTexture(GL_TEXTURE_2D, tex.id);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                               &internalFormat);
      tex.hasAlpha = (internalFormat == GL_RGBA || internalFormat == GL_RGBA8);
    }
  } else {
    printf("[UITexture] Unknown format: %s\n", path.c_str());
    return tex;
  }

  // Query dimensions
  if (tex.id) {
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tex.width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tex.height);
    // UI textures should clamp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!isPNG) // already logged for PNG
      printf("[UITexture] %s %s: %dx%d (id=%d)\n", isOZT ? "OZT" : "OZJ",
             path.c_str(), tex.width, tex.height, tex.id);
  } else {
    printf("[UITexture] FAILED to load: %s\n", path.c_str());
  }

  return tex;
}

void UITexture::Destroy() {
  if (id) {
    glDeleteTextures(1, &id);
    id = 0;
  }
}
