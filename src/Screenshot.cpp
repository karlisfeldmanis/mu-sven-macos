#include "Screenshot.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gif_lib.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <turbojpeg.h>
#include <vector>

// Static members for GIF capture
std::vector<Screenshot::GifFrame> Screenshot::gifFrames;
int Screenshot::gifWidth = 0;
int Screenshot::gifHeight = 0;
static float gifScale = 1.0f;
static int gifSkipCount = 0;
static int gifFrameCounter = 0;

void Screenshot::Capture(GLFWwindow *window,
                         const std::string &customFilename) {
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);

  std::vector<unsigned char> pixels(width * height * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

  // Flip the image vertically (OpenGL Y=0 is bottom)
  std::vector<unsigned char> flipped_pixels(width * height * 3);
  for (int y = 0; y < height; ++y) {
    memcpy(&flipped_pixels[y * width * 3],
           &pixels[(height - 1 - y) * width * 3], width * 3);
  }

  // Prepare filename
  std::string filename;
  if (!customFilename.empty()) {
    filename = customFilename;
  } else {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "mu_"
       << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S")
       << ".jpg";
    filename = ss.str();
  }

  // Ensure screenshots directory exists
  std::filesystem::path dir = std::filesystem::current_path() / "screenshots";
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directory(dir);
  }
  std::filesystem::path fullPath = dir / filename;
  std::string path = fullPath.string();

  // Compress to JPEG
  tjhandle compressor = tjInitCompress();
  unsigned char *jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;

  if (tjCompress2(compressor, flipped_pixels.data(), width, 0, height, TJPF_RGB,
                  &jpeg_buf, &jpeg_size, TJSAMP_444, 95, TJFLAG_FASTDCT) < 0) {
    std::cerr << "TurboJPEG compression error: " << tjGetErrorStr2(compressor)
              << std::endl;
    tjDestroy(compressor);
    return;
  }

  FILE *file = fopen(path.c_str(), "wb");
  if (file) {
    fwrite(jpeg_buf, 1, jpeg_size, file);
    fclose(file);
    std::cout << "✓ Screenshot saved to: "
              << std::filesystem::absolute(fullPath) << std::endl;
  } else {
    std::cerr << "Failed to open file for screenshot: " << path << std::endl;
  }

  tjFree(jpeg_buf);
  tjDestroy(compressor);
}

// --- Animated GIF capture ---

void Screenshot::BeginGif(int width, int height, float scale, int skipCount) {
  gifFrames.clear();
  gifWidth = width;
  gifHeight = height;
  gifScale = scale;
  gifSkipCount = skipCount;
  gifFrameCounter = 0;
}

void Screenshot::AddGifFrame(GLFWwindow *window) {
  if (gifFrameCounter % (gifSkipCount + 1) != 0) {
    gifFrameCounter++;
    return;
  }
  gifFrameCounter++;

  int fbWidth, fbHeight;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  std::vector<unsigned char> pixels(fbWidth * fbHeight * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, fbWidth, fbHeight, GL_RGB, GL_UNSIGNED_BYTE,
               pixels.data());

  int targetW = (int)(fbWidth * gifScale);
  int targetH = (int)(fbHeight * gifScale);

  GifFrame frame;
  frame.width = targetW;
  frame.height = targetH;
  frame.pixels.resize(targetW * targetH * 3);

  if (gifScale >= 0.99f) {
    // Just flip and store
    for (int y = 0; y < targetH; ++y) {
      memcpy(&frame.pixels[y * targetW * 3],
             &pixels[(fbHeight - 1 - y) * fbWidth * 3], targetW * 3);
    }
  } else {
    // Simple box filter (averaging)
    float xStep = (float)fbWidth / targetW;
    float yStep = (float)fbHeight / targetH;

    for (int ty = 0; ty < targetH; ++ty) {
      for (int tx = 0; tx < targetW; ++tx) {
        int srcXStart = (int)(tx * xStep);
        int srcYStart = (int)((targetH - 1 - ty) * yStep); // Flip here
        int srcXEnd = (int)((tx + 1) * xStep);
        int srcYEnd = (int)((targetH - ty) * yStep);

        long long r = 0, g = 0, b = 0;
        int count = 0;
        for (int sy = srcYStart; sy < srcYEnd; ++sy) {
          for (int sx = srcXStart; sx < srcXEnd; ++sx) {
            r += pixels[(sy * fbWidth + sx) * 3 + 0];
            g += pixels[(sy * fbWidth + sx) * 3 + 1];
            b += pixels[(sy * fbWidth + sx) * 3 + 2];
            count++;
          }
        }
        if (count > 0) {
          frame.pixels[(ty * targetW + tx) * 3 + 0] =
              (unsigned char)(r / count);
          frame.pixels[(ty * targetW + tx) * 3 + 1] =
              (unsigned char)(g / count);
          frame.pixels[(ty * targetW + tx) * 3 + 2] =
              (unsigned char)(b / count);
        }
      }
    }
  }
  gifFrames.push_back(std::move(frame));
}

// Median-cut color quantization: reduce RGB image to 256-color palette
namespace {

struct ColorBox {
  std::vector<int> indices; // indices into the pixel array (pixel index, not
                            // byte)
  const unsigned char *pixels;
  int minR, maxR, minG, maxG, minB, maxB;

  void computeRange() {
    minR = minG = minB = 255;
    maxR = maxG = maxB = 0;
    for (int idx : indices) {
      int r = pixels[idx * 3 + 0];
      int g = pixels[idx * 3 + 1];
      int b = pixels[idx * 3 + 2];
      minR = std::min(minR, r);
      maxR = std::max(maxR, r);
      minG = std::min(minG, g);
      maxG = std::max(maxG, g);
      minB = std::min(minB, b);
      maxB = std::max(maxB, b);
    }
  }

  int longestAxis() const {
    int rr = maxR - minR, gg = maxG - minG, bb = maxB - minB;
    if (rr >= gg && rr >= bb)
      return 0;
    if (gg >= bb)
      return 1;
    return 2;
  }

  std::array<unsigned char, 3> average() const {
    long long r = 0, g = 0, b = 0;
    for (int idx : indices) {
      r += pixels[idx * 3 + 0];
      g += pixels[idx * 3 + 1];
      b += pixels[idx * 3 + 2];
    }
    int n = (int)indices.size();
    return {(unsigned char)(r / n), (unsigned char)(g / n),
            (unsigned char)(b / n)};
  }
};

void medianCutQuantize(const unsigned char *pixels, int pixelCount,
                       GifColorType palette[256],
                       unsigned char *indexedOutput) {
  // Build initial box with all pixel indices (sample if too many)
  ColorBox initial;
  initial.pixels = pixels;
  int step = std::max(1, pixelCount / 50000); // sample for speed
  for (int i = 0; i < pixelCount; i += step) {
    initial.indices.push_back(i);
  }
  initial.computeRange();

  std::vector<ColorBox> boxes;
  boxes.push_back(std::move(initial));

  // Split until we have 256 boxes
  while ((int)boxes.size() < 256) {
    // Find box with largest range
    int bestIdx = 0;
    int bestRange = 0;
    for (int i = 0; i < (int)boxes.size(); ++i) {
      if (boxes[i].indices.size() < 2)
        continue;
      int axis = boxes[i].longestAxis();
      int range;
      if (axis == 0)
        range = boxes[i].maxR - boxes[i].minR;
      else if (axis == 1)
        range = boxes[i].maxG - boxes[i].minG;
      else
        range = boxes[i].maxB - boxes[i].minB;
      if (range > bestRange) {
        bestRange = range;
        bestIdx = i;
      }
    }
    if (bestRange == 0)
      break;

    // Split best box along its longest axis
    ColorBox &box = boxes[bestIdx];
    int axis = box.longestAxis();
    std::sort(box.indices.begin(), box.indices.end(), [&](int a, int b) {
      return box.pixels[a * 3 + axis] < box.pixels[b * 3 + axis];
    });

    int mid = (int)box.indices.size() / 2;
    ColorBox newBox;
    newBox.pixels = box.pixels;
    newBox.indices.assign(box.indices.begin() + mid, box.indices.end());
    box.indices.resize(mid);

    box.computeRange();
    newBox.computeRange();
    boxes.push_back(std::move(newBox));
  }

  // Build palette from box averages
  for (int i = 0; i < 256; ++i) {
    if (i < (int)boxes.size()) {
      auto avg = boxes[i].average();
      palette[i].Red = avg[0];
      palette[i].Green = avg[1];
      palette[i].Blue = avg[2];
    } else {
      palette[i].Red = palette[i].Green = palette[i].Blue = 0;
    }
  }

  // Map each pixel to nearest palette entry
  // Build a lookup for speed: use 5-bit truncated RGB as hash
  for (int i = 0; i < pixelCount; ++i) {
    int r = pixels[i * 3 + 0];
    int g = pixels[i * 3 + 1];
    int b = pixels[i * 3 + 2];

    int bestDist = INT_MAX;
    int bestPal = 0;
    int numPal = std::min(256, (int)boxes.size());
    for (int p = 0; p < numPal; ++p) {
      int dr = r - palette[p].Red;
      int dg = g - palette[p].Green;
      int db = b - palette[p].Blue;
      int dist = dr * dr + dg * dg + db * db;
      if (dist < bestDist) {
        bestDist = dist;
        bestPal = p;
      }
    }
    indexedOutput[i] = (unsigned char)bestPal;
  }
}

} // namespace

void Screenshot::SaveGif(const std::string &path, int delayCs) {
  if (gifFrames.empty()) {
    std::cerr << "[GIF] No frames to save" << std::endl;
    return;
  }

  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());

  int err;
  GifFileType *gif = EGifOpenFileName(path.c_str(), false, &err);
  if (!gif) {
    std::cerr << "[GIF] Failed to open file: " << GifErrorString(err)
              << std::endl;
    return;
  }

  EGifSetGifVersion(gif, true); // GIF89a

  int canvasW = gifFrames[0].width;
  int canvasH = gifFrames[0].height;

  if (EGifPutScreenDesc(gif, canvasW, canvasH, 8, 0, nullptr) == GIF_ERROR) {
    std::cerr << "[GIF] Failed to write screen descriptor" << std::endl;
    EGifCloseFile(gif, &err);
    return;
  }

  // NETSCAPE2.0 infinite loop
  EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE);
  EGifPutExtensionBlock(gif, 11, "NETSCAPE2.0");
  unsigned char loopData[3] = {1, 0, 0};
  EGifPutExtensionBlock(gif, 3, loopData);
  EGifPutExtensionTrailer(gif);

  std::cout << "[GIF] Encoding " << gifFrames.size() << " frames (" << canvasW
            << "x" << canvasH << ")..." << std::endl;

  std::vector<unsigned char> prevPixels;
  const int transIndex = 255;

  for (int f = 0; f < (int)gifFrames.size(); ++f) {
    auto &frame = gifFrames[f];
    int w = frame.width;
    int h = frame.height;

    int minX = 0, minY = 0, maxX = w - 1, maxY = h - 1;
    bool isDiff = false;

    // Frame diffing: find bounding box of changes
    if (f > 0 && prevPixels.size() == frame.pixels.size()) {
      minX = w;
      minY = h;
      maxX = -1;
      maxY = -1;
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          int idx = (y * w + x) * 3;
          if (frame.pixels[idx] != prevPixels[idx] ||
              frame.pixels[idx + 1] != prevPixels[idx + 1] ||
              frame.pixels[idx + 2] != prevPixels[idx + 2]) {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            isDiff = true;
          }
        }
      }
      if (!isDiff) {
        // No changes, still need to add a frame for delay (or skip? let's just
        // add minimal)
        minX = minY = 0;
        maxX = maxY = 0;
      }
    }

    int diffW = maxX - minX + 1;
    int diffH = maxY - minY + 1;

    // Extract dirty rect pixels
    std::vector<unsigned char> dirtyPixels(diffW * diffH * 3);
    for (int y = 0; y < diffH; ++y) {
      memcpy(&dirtyPixels[y * diffW * 3],
             &frame.pixels[((minY + y) * w + minX) * 3], diffW * 3);
    }

    GifColorType palette[256];
    std::vector<unsigned char> indexed(diffW * diffH);

    // Reserve index 255 for transparency if this is a diff frame
    medianCutQuantize(dirtyPixels.data(), diffW * diffH, palette,
                      indexed.data());

    // If diffing, we set pixels that match previous frame to transIndex
    if (f > 0 && isDiff) {
      for (int y = 0; y < diffH; ++y) {
        for (int x = 0; x < diffW; ++x) {
          int fx = minX + x;
          int fy = minY + y;
          int idx = (fy * w + fx) * 3;
          if (frame.pixels[idx] == prevPixels[idx] &&
              frame.pixels[idx + 1] == prevPixels[idx + 1] &&
              frame.pixels[idx + 2] == prevPixels[idx + 2]) {
            indexed[y * diffW + x] = (unsigned char)transIndex;
          }
        }
      }
    }

    ColorMapObject *colorMap = GifMakeMapObject(256, palette);

    GraphicsControlBlock gcb;
    gcb.DisposalMode = DISPOSE_DO_NOT;
    gcb.UserInputFlag = false;
    gcb.DelayTime = delayCs;
    gcb.TransparentColor = (f > 0) ? transIndex : NO_TRANSPARENT_COLOR;

    GifByteType gcbBytes[4];
    EGifGCBToExtension(&gcb, gcbBytes);
    EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, gcbBytes);

    if (EGifPutImageDesc(gif, minX, minY, diffW, diffH, false, colorMap) ==
        GIF_ERROR) {
      std::cerr << "[GIF] Failed to write image descriptor for frame " << f
                << std::endl;
      GifFreeMapObject(colorMap);
      break;
    }

    for (int y = 0; y < diffH; ++y) {
      EGifPutLine(gif, indexed.data() + y * diffW, diffW);
    }

    GifFreeMapObject(colorMap);
    prevPixels = frame.pixels;

    if ((f + 1) % 10 == 0 || f == (int)gifFrames.size() - 1) {
      std::cout << "[GIF] Frame " << (f + 1) << "/" << gifFrames.size()
                << std::endl;
    }
  }

  if (EGifCloseFile(gif, &err) == GIF_ERROR) {
    std::cerr << "[GIF] Error closing file: " << GifErrorString(err)
              << std::endl;
  } else {
    auto fileSize = std::filesystem::file_size(std::filesystem::path(path));
    std::cout << "[GIF] Saved: " << path << " (" << (fileSize / 1024) << " KB)"
              << std::endl;
  }

  gifFrames.clear();
}
