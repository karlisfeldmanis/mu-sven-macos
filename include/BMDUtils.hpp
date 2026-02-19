#ifndef BMD_UTILS_HPP
#define BMD_UTILS_HPP

#include "BMDStructs.hpp"
#include <array>
#include <cfloat>
#include <glm/glm.hpp>
#include <vector>

struct BMDData; // forward declared in BMDParser.hpp

// 3x4 row-major transform matrix (matches MU Online's float[3][4])
using BoneWorldMatrix = std::array<std::array<float, 4>, 3>;

// Axis-aligned bounding box
struct AABB {
  glm::vec3 min{FLT_MAX};
  glm::vec3 max{-FLT_MAX};
  glm::vec3 center() const { return (min + max) * 0.5f; }
  float radius() const { return glm::length(max - min) * 0.5f; }
};

namespace MuMath {

// MU Online rotation (radians) -> quaternion  (matches ZzzMathLib
// AngleQuaternion)
void AngleQuaternion(const glm::vec3 &angles, float q[4]);

// Quaternion -> 3x4 rotation matrix  (matches ZzzMathLib QuaternionMatrix)
void QuaternionMatrix(const float q[4], float m[3][4]);

// Concatenate two 3x4 transforms: out = in1 * in2  (matches R_ConcatTransforms)
void ConcatTransforms(const float in1[3][4], const float in2[3][4],
                      float out[3][4]);

// Transform a point by a 3x4 matrix (rotation + translation)
glm::vec3 TransformPoint(const float m[3][4], const glm::vec3 &v);

// Rotate a vector by a 3x4 matrix (rotation only, no translation)
glm::vec3 RotateVector(const float m[3][4], const glm::vec3 &v);

// Euler angles (degrees) -> 3x4 rotation matrix (matches ZzzMathLib
// AngleMatrix) angles.x = X (sr/cr), angles.y = Y (sp/cp), angles.z = Z (sy/cy)
void AngleMatrix(const glm::vec3 &anglesDeg, float matrix[3][4]);

// Build a weapon offset matrix: AngleMatrix(rotDeg) + translation in [i][3]
BoneWorldMatrix BuildWeaponOffsetMatrix(const glm::vec3 &rotDeg,
                                        const glm::vec3 &offset);

} // namespace MuMath

// Compute bone world matrices for a given action and frame
std::vector<BoneWorldMatrix> ComputeBoneMatrices(const BMDData *bmd,
                                                 int action = 0, int frame = 0);

// Get interpolated bone position and rotation for a single action/frame
bool GetInterpolatedBoneData(const BMDData *bmd, int action, float frame,
                             int boneIdx, glm::vec3 &outPos,
                             glm::vec4 &outQuat);

// Compute bone world matrices with fractional frame interpolation (slerp)
// Uses pre-computed quaternions for smooth animation between keyframes
std::vector<BoneWorldMatrix>
ComputeBoneMatricesInterpolated(const BMDData *bmd, int action, float frame);

// Compute bone world matrices with cross-action blending
std::vector<BoneWorldMatrix>
ComputeBoneMatricesBlended(const BMDData *bmd, int action1, float frame1,
                           int action2, float frame2, float blendAlpha);

// Compute AABB from bone-transformed vertices
AABB ComputeTransformedAABB(const BMDData *bmd,
                            const std::vector<BoneWorldMatrix> &bones);

#endif // BMD_UTILS_HPP
