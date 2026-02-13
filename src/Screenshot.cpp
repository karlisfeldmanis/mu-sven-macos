#include "Screenshot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <turbojpeg.h>
#include <vector>

void Screenshot::Capture(GLFWwindow *window) {
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
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << "mu_" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S")
     << ".jpg";
  std::string filename = ss.str();

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
