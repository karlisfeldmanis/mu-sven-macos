#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct BMDHeader {
  char ID[3];
  char Version;
  char Name[32];
  short NumMesh;
  short NumBones;
  short NumActions;
};
#pragma pack(pop)

int main(int argc, char *argv[]) {
  if (argc < 2)
    return 1;
  std::ifstream fs(argv[1], std::ios::binary);
  if (!fs)
    return 1;

  BMDHeader head;
  fs.read((char *)&head, sizeof(head));

  std::cout << "BMD: " << argv[1] << std::endl;
  std::cout << "Actions: " << head.NumActions << std::endl;

  // Mesh loop
  for (int i = 0; i < head.NumMesh; ++i) {
    short numVerts, numNorms, numTex, numTris, tex;
    fs.read((char *)&numVerts, 2);
    fs.read((char *)&numNorms, 2);
    fs.read((char *)&numTex, 2);
    fs.read((char *)&numTris, 2);
    fs.read((char *)&tex, 2);

    fs.seekg(numVerts * 16, std::ios::cur);
    fs.seekg(numNorms * 20, std::ios::cur);
    fs.seekg(numTex * 8, std::ios::cur);
    fs.seekg(numTris * 64, std::ios::cur);
    char texName[32];
    fs.read(texName, 32);
    std::cout << "  Mesh " << i << " Texture: " << texName << std::endl;
  }

  // Action loop
  for (int i = 0; i < head.NumActions; ++i) {
    short numKeys;
    char lockPos;
    fs.read((char *)&numKeys, 2);
    fs.read((char *)&lockPos, 1);
    std::cout << "  Action " << i << ": Keys=" << (int)numKeys << std::endl;
    if (lockPos && numKeys > 0) {
      fs.seekg(numKeys * 12, std::ios::cur);
    }
  }

  return 0;
}
