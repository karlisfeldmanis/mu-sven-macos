#ifndef BMD_STRUCTS_HPP
#define BMD_STRUCTS_HPP

#include <glm/glm.hpp>
#include <string>
#include <vector>

#define MAX_BONES 200

typedef glm::vec3 vec3_t;
typedef glm::vec4 vec4_t;

struct BoneMatrix_t {
  std::vector<vec3_t> Position;
  std::vector<vec3_t> Rotation;
  std::vector<vec4_t> Quaternion;
};

struct Bone_t {
  char Name[32];
  short Parent;
  bool Dummy;
  std::vector<BoneMatrix_t> BoneMatrixes; // Index by Action
  bool BoundingBox;
  vec3_t BoundingVertices[8];
};

struct Vertex_t {
  short Node;
  vec3_t Position;
};

struct Normal_t {
  short Node;
  vec3_t Normal;
  short BindVertex;
};

struct TexCoord_t {
  float TexCoordU;
  float TexCoordV;
};

struct Triangle_t {
  char Polygon;
  short VertexIndex[4];
  short NormalIndex[4];
  short TexCoordIndex[4];
  short EdgeTriangleIndex[4];
  bool Front;
};

struct Action_t {
  bool Loop;
  float PlaySpeed;
  short NumAnimationKeys;
  bool LockPositions;
  std::vector<vec3_t> Positions;
};

struct Mesh_t {
  short Texture;
  short NumVertices;
  short NumNormals;
  short NumTexCoords;
  short NumTriangles;
  std::vector<Vertex_t> Vertices;
  std::vector<Normal_t> Normals;
  std::vector<TexCoord_t> TexCoords;
  std::vector<Triangle_t> Triangles;
  std::string TextureName;
};

#endif // BMD_STRUCTS_HPP
