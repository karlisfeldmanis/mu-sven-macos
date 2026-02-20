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

  std::cout << "BMD: " << argv[1] << " Name: " << head.Name << std::endl;
  std::cout << "Actions: " << head.NumActions << std::endl;

  // Mesh loop
  for (int i = 0; i < head.NumMesh; ++i) {
    short numVerts, numNorms, numTex, numTris, tex;
    fs.read((char *)&numVerts, 2);
    fs.read((char *)&numNorms, 2);
    fs.read((char *)&numTex, 2);
    fs.read((char *)&numTris, 2);
    fs.read((char *)&tex, 2);

    long meshDataStart = fs.tellg();

    fs.seekg(numVerts * 16, std::ios::cur);
    fs.seekg(numNorms * 20, std::ios::cur);
    fs.seekg(numTex * 8, std::ios::cur);
    fs.seekg(numTris * 64, std::ios::cur);
    char texName[32];
    fs.read(texName, 32);
    std::cout << "  Mesh " << i << " Texture: " << texName
              << " Verts: " << numVerts << " Tris: " << numTris << std::endl;

    long currentPos = fs.tellg();

    // Check for quads
    int quads = 0;
    fs.seekg(meshDataStart + numVerts * 16 + numNorms * 20 + numTex * 8,
             std::ios::beg);
    for (int j = 0; j < numTris; ++j) {
      short poly;
      fs.read((char *)&poly, 2);
      if (poly == 4)
        quads++;
      fs.seekg(62, std::ios::cur);
    }
    if (quads > 0)
      std::cout << "    Quads found: " << quads << std::endl;

    // Read and check vertices for duplicates
    std::vector<char> vdata(numVerts * 16);
    fs.seekg(meshDataStart, std::ios::beg);
    fs.read(vdata.data(), numVerts * 16);

    int dupes = 0;
    std::vector<int> boneCounts(head.NumBones, 0);
    for (int j = 0; j < numVerts; ++j) {
      short node;
      memcpy(&node, vdata.data() + j * 16, 2);
      if (node >= 0 && node < head.NumBones)
        boneCounts[node]++;

      float *p1 = (float *)(vdata.data() + j * 16 + 4);
      for (int k = j + 1; k < numVerts; ++k) {
        float *p2 = (float *)(vdata.data() + k * 16 + 4);
        if (p1[0] == p2[0] && p1[1] == p2[1] && p1[2] == p2[2]) {
          dupes++;
          break;
        }
      }
    }
    if (dupes > 0)
      std::cout << "    Duplicate vertices: " << dupes << "/" << numVerts
                << std::endl;

    std::cout << "    Bone segments: ";
    for (int b = 0; b < head.NumBones; ++b)
      std::cout << "B" << b << ":" << boneCounts[b] << " ";
    std::cout << std::endl;

    if (i == 1) {
      std::cout << "    Mesh 1 Vertices:" << std::endl;
      for (int j = 0; j < numVerts; ++j) {
        float *p = (float *)(vdata.data() + j * 16 + 4);
        std::cout << "      V" << j << "(" << p[0] << "," << p[1] << "," << p[2]
                  << ")" << std::endl;
      }
    }

    fs.seekg(currentPos, std::ios::beg);
  }

  // To find bones, we must read the numKeys for each action first
  std::vector<short> actionKeys(head.NumActions);
  for (int i = 0; i < head.NumActions; ++i) {
    fs.read((char *)&actionKeys[i], 2);
    char lockPos;
    fs.read(&lockPos, 1);
    if (lockPos && actionKeys[i] > 0) {
      fs.seekg(actionKeys[i] * 12, std::ios::cur);
    }
  }

  // Bones loop
  std::cout << "Bones: " << head.NumBones << std::endl;
  for (int i = 0; i < head.NumBones; ++i) {
    char dummy;
    fs.read(&dummy, 1);
    char name[33] = {0};
    if (!dummy) {
      fs.read(name, 32);
      short parent;
      fs.read((char *)&parent, 2);
      std::cout << "  Bone " << i << ": " << name << " Parent=" << parent;

      for (int j = 0; j < head.NumActions; ++j) {
        if (actionKeys[j] > 0) {
          float pos[3];
          fs.read((char *)pos, 12);
          if (j == 0) {
            std::cout << " Action0_Frame0(" << pos[0] << "," << pos[1] << ","
                      << pos[2] << ")";
          }
          fs.seekg((actionKeys[j] - 1) * 12,
                   std::ios::cur);                     // skip other frames pos
          fs.seekg(actionKeys[j] * 12, std::ios::cur); // skip rot
        }
      }
      std::cout << std::endl;
    } else {
      std::cout << "  Bone " << i << ": Dummy" << std::endl;
    }
  }

  return 0;
}
