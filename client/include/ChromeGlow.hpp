#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>

// Consolidated chrome/metal glow rendering for enhanced items (+7/+9/+11/+13).
// Main 5.2 ZzzObject.cpp RenderPartObjectEffect + PartObjectColor/PartObjectColor2.
// Replaces duplicated glow code across HeroCharacter, CharacterSelect,
// ItemModelManager, and NpcManager.
namespace ChromeGlow {

struct Textures {
  GLuint chrome1 = 0; // Effect/Chrome01.OZJ (BITMAP_CHROME)
  GLuint chrome2 = 0; // Effect/Chrome02.OZJ (BITMAP_CHROME2)
  GLuint shiny = 0;   // Effect/Shiny01.OZJ  (BITMAP_SHINY)
};

struct GlowPass {
  int chromeMode; // 1=CHROME, 2=CHROME2, 3=METAL, 4=CHROME4
  GLuint texture;
  glm::vec3 color;
};

// Load/delete the 3 shared environment-map textures (call once at init/shutdown)
void LoadTextures(const std::string &dataPath);
void DeleteTextures();
const Textures &GetTextures();

// Main 5.2 PartObjectColor: 44-color palette for CHROME/METAL passes
glm::vec3 GetPartObjectColor(int category, int itemIndex);
// Main 5.2 PartObjectColor2: 4-color modulator for CHROME2/CHROME4 passes
glm::vec3 GetPartObjectColor2(int category, int itemIndex);

// Fill outPasses with the glow pass definitions for a given enhancement level.
// Returns number of passes (0 if level < 7, 1-3 otherwise).
// Each pass has chromeMode, texture, and color already resolved.
int GetGlowPasses(int enhanceLevel, int category, int itemIndex,
                  GlowPass *outPasses);

// GL state: additive blend, depth write off, cull off
void BeginGlow();
// Restore: alpha blend, depth write on, cull on, reset shader uniforms
void EndGlow(GLuint shaderProgram);

} // namespace ChromeGlow
