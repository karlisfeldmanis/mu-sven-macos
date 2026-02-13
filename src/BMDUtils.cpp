#include "BMDUtils.hpp"
#include "BMDParser.hpp"
#include <cmath>
#include <cstring>

namespace MuMath {

void AngleQuaternion(const glm::vec3 &angles, float q[4]) {
  float sr = sinf(angles.x * 0.5f), cr = cosf(angles.x * 0.5f);
  float sp = sinf(angles.y * 0.5f), cp = cosf(angles.y * 0.5f);
  float sy = sinf(angles.z * 0.5f), cy = cosf(angles.z * 0.5f);
  q[0] = sr * cp * cy - cr * sp * sy;
  q[1] = cr * sp * cy + sr * cp * sy;
  q[2] = cr * cp * sy - sr * sp * cy;
  q[3] = cr * cp * cy + sr * sp * sy;
}

void QuaternionMatrix(const float q[4], float m[3][4]) {
  m[0][0] = 1.0f - 2.0f * q[1] * q[1] - 2.0f * q[2] * q[2];
  m[1][0] = 2.0f * q[0] * q[1] + 2.0f * q[3] * q[2];
  m[2][0] = 2.0f * q[0] * q[2] - 2.0f * q[3] * q[1];
  m[0][1] = 2.0f * q[0] * q[1] - 2.0f * q[3] * q[2];
  m[1][1] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[2] * q[2];
  m[2][1] = 2.0f * q[1] * q[2] + 2.0f * q[3] * q[0];
  m[0][2] = 2.0f * q[0] * q[2] + 2.0f * q[3] * q[1];
  m[1][2] = 2.0f * q[1] * q[2] - 2.0f * q[3] * q[0];
  m[2][2] = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[1] * q[1];
  m[0][3] = m[1][3] = m[2][3] = 0.0f;
}

void ConcatTransforms(const float in1[3][4], const float in2[3][4],
                      float out[3][4]) {
  out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
              in1[0][2] * in2[2][0];
  out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
              in1[0][2] * in2[2][1];
  out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
              in1[0][2] * in2[2][2];
  out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
              in1[0][2] * in2[2][3] + in1[0][3];
  out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
              in1[1][2] * in2[2][0];
  out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
              in1[1][2] * in2[2][1];
  out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
              in1[1][2] * in2[2][2];
  out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
              in1[1][2] * in2[2][3] + in1[1][3];
  out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
              in1[2][2] * in2[2][0];
  out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
              in1[2][2] * in2[2][1];
  out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
              in1[2][2] * in2[2][2];
  out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
              in1[2][2] * in2[2][3] + in1[2][3];
}

glm::vec3 TransformPoint(const float m[3][4], const glm::vec3 &v) {
  return glm::vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
                   m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                   m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]);
}

glm::vec3 RotateVector(const float m[3][4], const glm::vec3 &v) {
  return glm::vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                   m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                   m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}

} // namespace MuMath

std::vector<BoneWorldMatrix> ComputeBoneMatrices(const BMDData *bmd,
                                                  int action, int frame) {
  int numBones = (int)bmd->Bones.size();
  std::vector<BoneWorldMatrix> world(numBones);

  // Identity init
  for (int i = 0; i < numBones; ++i) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        world[i][r][c] = (r == c) ? 1.0f : 0.0f;
  }

  if (bmd->Actions.empty() || action >= (int)bmd->Actions.size())
    return world;

  for (int i = 0; i < numBones; ++i) {
    auto &bone = bmd->Bones[i];
    if (bone.Dummy)
      continue;
    if (action >= (int)bone.BoneMatrixes.size())
      continue;
    auto &bm = bone.BoneMatrixes[action];
    if (bm.Position.empty() || bm.Rotation.empty())
      continue;

    int f = (frame < (int)bm.Position.size()) ? frame : 0;

    float quat[4];
    MuMath::AngleQuaternion(bm.Rotation[f], quat);
    float local[3][4];
    MuMath::QuaternionMatrix(quat, local);
    local[0][3] = bm.Position[f].x;
    local[1][3] = bm.Position[f].y;
    local[2][3] = bm.Position[f].z;

    if (bone.Parent == -1) {
      memcpy(world[i].data(), local, sizeof(float) * 12);
    } else if (bone.Parent >= 0 && bone.Parent < numBones) {
      float result[3][4];
      MuMath::ConcatTransforms((const float(*)[4])world[bone.Parent].data(),
                                local, result);
      memcpy(world[i].data(), result, sizeof(float) * 12);
    }
  }

  return world;
}

AABB ComputeTransformedAABB(const BMDData *bmd,
                             const std::vector<BoneWorldMatrix> &bones) {
  AABB box;
  for (auto &mesh : bmd->Meshes) {
    for (auto &vert : mesh.Vertices) {
      glm::vec3 pos = vert.Position;
      int boneIdx = vert.Node;
      if (boneIdx >= 0 && boneIdx < (int)bones.size()) {
        pos = MuMath::TransformPoint(
            (const float(*)[4])bones[boneIdx].data(), vert.Position);
      }
      box.min = glm::min(box.min, pos);
      box.max = glm::max(box.max, pos);
    }
  }
  return box;
}
