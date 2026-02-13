#include "TextureLoader.hpp"
#include <fstream>
#include <iostream>
#include <turbojpeg.h>

std::vector<unsigned char> TextureLoader::LoadOZJRaw(const std::string &path,
                                                     int &width, int &height) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[TextureLoader] Cannot open file: " << path << std::endl;
    return {};
  }

  std::vector<unsigned char> full_data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
  if (full_data.size() < 4)
    return {};

  const unsigned char *jpeg_ptr = full_data.data();
  size_t jpeg_size = full_data.size();

  // MU Online OZJ files (and some renamed JPGs) often have a 24-byte header.
  // We skip it if we find the JPEG magic number (FF D8) at offset 24.
  if (full_data.size() > 24 && full_data[24] == 0xFF && full_data[25] == 0xD8) {
    jpeg_ptr += 24;
    jpeg_size -= 24;
  } else if (full_data[0] == 0xFF && full_data[1] == 0xD8) {
    // Standard JPEG starting at offset 0
  } else {
    std::cerr << "[TextureLoader] Invalid JPEG format: " << path << std::endl;
    return {};
  }

  tjhandle decompressor = tjInitDecompress();
  int subsamp, colorspace;
  if (tjDecompressHeader3(decompressor, jpeg_ptr, jpeg_size, &width, &height,
                          &subsamp, &colorspace) < 0) {
    std::cerr << "[TextureLoader] TurboJPEG header error for " << path << ": "
              << tjGetErrorStr2(decompressor) << std::endl;
    tjDestroy(decompressor);
    return {};
  }

  std::vector<unsigned char> image_data(width * height * 3);
  // TJFLAG_BOTTOMUP matches Sven's OpenJpegBuffer which uses the same flag.
  // This outputs rows bottom-to-top, matching OpenGL's texture origin convention.
  if (tjDecompress2(decompressor, jpeg_ptr, jpeg_size, image_data.data(), width,
                    0, height, TJPF_RGB, TJFLAG_FASTDCT | TJFLAG_BOTTOMUP) < 0) {
    std::cerr << "[TextureLoader] TurboJPEG decompression error for " << path
              << ": " << tjGetErrorStr2(decompressor) << std::endl;
    tjDestroy(decompressor);
    return {};
  }
  tjDestroy(decompressor);
  return image_data;
}

GLuint TextureLoader::LoadOZJ(const std::string &path) {
  int w, h;
  auto data = LoadOZJRaw(path, w, h);
  if (data.empty())
    return 0;

  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
               data.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return textureID;
}

GLuint TextureLoader::LoadOZT(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "[TextureLoader] Cannot open OZT: " << path << std::endl;
    return 0;
  }

  std::vector<unsigned char> full_data((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());

  size_t offset = 0;
  // Skip potential MU header (often 4 or 24 bytes)
  // Check for TGA signature start after potential header
  // TGA image type is at offset 2 of TGA header.
  if (full_data.size() > 24) {
    // Basic heuristic: check if data starting at 4 or 24 looks like a TGA
    // header imageType is at +2, depth is at +16.
    if (full_data[4 + 2] == 2 || full_data[4 + 2] == 10)
      offset = 4;
    else if (full_data[24 + 2] == 2 || full_data[24 + 2] == 10)
      offset = 24;
  }

  if (full_data.size() < offset + 18)
    return 0;

  unsigned char *header = &full_data[offset];
  int width = header[12] | (header[13] << 8);
  int height = header[14] | (header[15] << 8);
  int bpp = header[16];

  if (bpp != 24 && bpp != 32) {
    std::cerr << "[TextureLoader] Unsupported OZT BPP: " << bpp << " in "
              << path << std::endl;
    return 0;
  }

  unsigned char *pixel_data = &full_data[offset + 18];
  GLenum format = (bpp == 24) ? GL_BGR : GL_BGRA;
  GLint internalFormat = (bpp == 24) ? GL_RGB : GL_RGBA;

  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format,
               GL_UNSIGNED_BYTE, pixel_data);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  return textureID;
}
