#include "TextureLoader.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <turbojpeg.h>

namespace fs = std::filesystem;

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
  // Keep native JPEG top-to-bottom row order. In OpenGL, the first row of
  // glTexImage2D data maps to texCoord v=0.  MU UV coordinates use DirectX
  // convention (v=0 = top of image), so uploading top-to-bottom data means
  // v=0 correctly samples the top of the texture — no flip needed.
  if (tjDecompress2(decompressor, jpeg_ptr, jpeg_size, image_data.data(), width,
                    0, height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
  unsigned char imageType = header[2];
  int width = header[12] | (header[13] << 8);
  int height = header[14] | (header[15] << 8);
  int bpp = header[16];

  if (bpp != 24 && bpp != 32) {
    std::cerr << "[TextureLoader] Unsupported OZT BPP: " << bpp << " in "
              << path << std::endl;
    return 0;
  }

  int bytesPerPixel = bpp / 8;
  // MU OZT files don't use standard TGA idLength — pixel data always
  // follows immediately after the 18-byte header.
  size_t pixelDataOffset = offset + 18;
  size_t expectedSize = (size_t)width * height * bytesPerPixel;

  if (pixelDataOffset >= full_data.size()) {
    std::cerr << "[TextureLoader] OZT pixel data offset past end of file: "
              << path << std::endl;
    return 0;
  }

  unsigned char *pixel_data = &full_data[pixelDataOffset];
  std::vector<unsigned char> decompressed;
  unsigned char *uploadData = pixel_data;

  // RLE decompression for type 10 (run-length encoded truecolor)
  if (imageType == 10) {
    decompressed.resize(expectedSize);
    size_t srcIdx = 0;
    size_t dstIdx = 0;
    size_t srcSize = full_data.size() - pixelDataOffset;

    while (dstIdx < expectedSize && srcIdx < srcSize) {
      unsigned char packetHeader = pixel_data[srcIdx++];
      int count = (packetHeader & 0x7F) + 1;

      if (packetHeader & 0x80) {
        // Run-length packet: one pixel repeated count times
        if (srcIdx + bytesPerPixel > srcSize)
          break;
        for (int i = 0; i < count && dstIdx + bytesPerPixel <= expectedSize;
             ++i) {
          memcpy(&decompressed[dstIdx], &pixel_data[srcIdx], bytesPerPixel);
          dstIdx += bytesPerPixel;
        }
        srcIdx += bytesPerPixel;
      } else {
        // Raw packet: count pixels follow
        size_t rawBytes = (size_t)count * bytesPerPixel;
        if (srcIdx + rawBytes > srcSize)
          break;
        if (dstIdx + rawBytes > expectedSize)
          break;
        memcpy(&decompressed[dstIdx], &pixel_data[srcIdx], rawBytes);
        srcIdx += rawBytes;
        dstIdx += rawBytes;
      }
    }

    if (dstIdx != expectedSize) {
      std::cerr << "[TextureLoader] RLE decompression size mismatch in " << path
                << " (got " << dstIdx << ", expected " << expectedSize << ")"
                << std::endl;
      return 0;
    }
    uploadData = decompressed.data();
  }

  // V-flip: MU OZT data is always stored top-to-bottom (matching the
  // reference engine which unconditionally reverses row order with ny-1-y).
  // Flip to bottom-to-top for OpenGL's texture origin convention.
  {
    if (imageType != 10) {
      decompressed.assign(uploadData, uploadData + expectedSize);
      uploadData = decompressed.data();
    }
    int rowSize = width * bytesPerPixel;
    std::vector<unsigned char> rowTemp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
      unsigned char *topRow = uploadData + y * rowSize;
      unsigned char *botRow = uploadData + (height - 1 - y) * rowSize;
      memcpy(rowTemp.data(), topRow, rowSize);
      memcpy(topRow, botRow, rowSize);
      memcpy(botRow, rowTemp.data(), rowSize);
    }
  }

  GLenum format = (bpp == 24) ? GL_BGR : GL_BGRA;
  GLint internalFormat = (bpp == 24) ? GL_RGB : GL_RGBA;

  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format,
               GL_UNSIGNED_BYTE, uploadData);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  return textureID;
}

GLuint TextureLoader::LoadByExtension(const std::string &path) {
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find(".ozj") != std::string::npos ||
      lower.find(".jpg") != std::string::npos ||
      lower.find(".jpeg") != std::string::npos) {
    return LoadOZJ(path);
  }
  return LoadOZT(path);
}

static TextureLoadResult LoadByExtensionWithInfo(const std::string &path) {
  TextureLoadResult result;
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find(".ozj") != std::string::npos ||
      lower.find(".jpg") != std::string::npos ||
      lower.find(".jpeg") != std::string::npos) {
    result.textureID = TextureLoader::LoadOZJ(path);
    result.hasAlpha = false; // JPEG never has alpha
    return result;
  }
  result.textureID = TextureLoader::LoadOZT(path);
  if (result.textureID != 0) {
    GLint internalFormat;
    glBindTexture(GL_TEXTURE_2D, result.textureID);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                             &internalFormat);
    result.hasAlpha = (internalFormat == GL_RGBA || internalFormat == GL_RGBA8);
  }
  return result;
}

// Case-insensitive file lookup in a directory
static std::string FindFileCI(const std::string &dir,
                              const std::string &filename) {
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  for (auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    std::string name = entry.path().filename().string();
    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                   ::tolower);
    if (nameLower == lower)
      return entry.path().string();
  }
  return "";
}

GLuint TextureLoader::Resolve(const std::string &directory,
                              const std::string &bmdTextureName) {
  // Strip any Windows path prefix (e.g. "Data2\\Object1\\candle.jpg")
  std::string baseName = bmdTextureName;
  auto pos = baseName.find_last_of("\\/");
  if (pos != std::string::npos)
    baseName = baseName.substr(pos + 1);

  // Try exact filename first (case-insensitive)
  std::string found = FindFileCI(directory, baseName);
  if (!found.empty())
    return LoadByExtension(found);

  // Strip extension and try common MU texture extensions.
  // Prefer the same format family as the original extension so that
  // e.g. "tree_01.tga" resolves to tree_01.OZT (has alpha) not tree_01.OZJ.
  std::string stem = baseName;
  std::string origExt;
  auto dotPos = stem.find_last_of('.');
  if (dotPos != std::string::npos) {
    origExt = stem.substr(dotPos);
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::tolower);
    stem = stem.substr(0, dotPos);
  }

  const char *tgaExts[] = {".OZT", ".ozt", ".tga", ".TGA"};
  const char *jpgExts[] = {".OZJ", ".ozj", ".jpg", ".JPG"};
  bool preferTGA =
      (origExt == ".tga" || origExt == ".ozt" || origExt == ".bmp");

  auto tryExts = [&](const char *list[], int count) -> GLuint {
    for (int i = 0; i < count; ++i) {
      found = FindFileCI(directory, stem + list[i]);
      if (!found.empty())
        return LoadByExtension(found);
    }
    return 0;
  };

  GLuint result;
  if (preferTGA) {
    result = tryExts(tgaExts, 4);
    if (!result)
      result = tryExts(jpgExts, 4);
  } else {
    result = tryExts(jpgExts, 4);
    if (!result)
      result = tryExts(tgaExts, 4);
  }
  if (result)
    return result;

  std::cerr << "[TextureLoader::Resolve] Could not find texture: " << baseName
            << " in " << directory << std::endl;
  return 0;
}

TextureScriptFlags
TextureLoader::ParseScriptFlags(const std::string &textureName) {
  TextureScriptFlags flags;

  std::string name = textureName;
  auto slashPos = name.find_last_of("\\/");
  if (slashPos != std::string::npos)
    name = name.substr(slashPos + 1);

  auto dotPos = name.find_last_of('.');
  if (dotPos != std::string::npos)
    name = name.substr(0, dotPos);

  auto underPos = name.find_last_of('_');
  if (underPos == std::string::npos || underPos == name.size() - 1)
    return flags;

  std::string suffix = name.substr(underPos + 1);

  for (char c : suffix) {
    char cu = std::toupper(c);
    if (cu != 'R' && cu != 'H' && cu != 'N' && cu != 'S')
      return flags; // Not a script suffix
  }

  for (char c : suffix) {
    switch (std::toupper(c)) {
    case 'R':
      flags.bright = true;
      break;
    case 'H':
      flags.hidden = true;
      break;
    case 'N':
      flags.noneBlend = true;
      break;
    }
  }

  return flags;
}

TextureLoadResult
TextureLoader::ResolveWithInfo(const std::string &directory,
                               const std::string &bmdTextureName) {
  std::string baseName = bmdTextureName;
  auto pos = baseName.find_last_of("\\/");
  if (pos != std::string::npos)
    baseName = baseName.substr(pos + 1);

  std::string found = FindFileCI(directory, baseName);
  if (!found.empty())
    return LoadByExtensionWithInfo(found);

  std::string stem = baseName;
  std::string origExt;
  auto dotPos = stem.find_last_of('.');
  if (dotPos != std::string::npos) {
    origExt = stem.substr(dotPos);
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::tolower);
    stem = stem.substr(0, dotPos);
  }

  const char *tgaExts[] = {".OZT", ".ozt", ".tga", ".TGA"};
  const char *jpgExts[] = {".OZJ", ".ozj", ".jpg", ".JPG"};
  bool preferTGA =
      (origExt == ".tga" || origExt == ".ozt" || origExt == ".bmp");

  auto tryExts = [&](const char *list[], int count) -> TextureLoadResult {
    for (int i = 0; i < count; ++i) {
      found = FindFileCI(directory, stem + list[i]);
      if (!found.empty())
        return LoadByExtensionWithInfo(found);
    }
    return {0, false};
  };

  TextureLoadResult result;
  if (preferTGA) {
    result = tryExts(tgaExts, 4);
    if (!result.textureID)
      result = tryExts(jpgExts, 4);
  } else {
    result = tryExts(jpgExts, 4);
    if (!result.textureID)
      result = tryExts(tgaExts, 4);
  }
  if (result.textureID)
    return result;

  std::cerr << "[TextureLoader::Resolve] Could not find texture: " << baseName
            << " in " << directory << std::endl;
  return {0, false};
}
