#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

class Camera {
public:
  Camera(glm::vec3 position = glm::vec3(150.0f, 0.0f, 150.0f));

  void ProcessKeyboard(int direction, float deltaTime);
  void ProcessMouseRotation(float xoffset, float yoffset);
  void ProcessMouseScroll(float yoffset);

  void SaveState(const std::string &filename);
  void LoadState(const std::string &filename);

  void SetPosition(glm::vec3 pos) { Position = pos; }
  void SetAngles(float yaw, float pitch);
  void SetZoom(float zoom) {
    Zoom = zoom;
    TargetZoom = zoom;
  }

  void Update(float deltaTime);

  glm::mat4 GetViewMatrix();
  glm::mat4 GetProjectionMatrix(float width, float height);

  glm::vec3 GetPosition() const { return Position; }
  float GetYaw() const { return Yaw; }
  float GetPitch() const { return Pitch; }
  float GetZoom() const { return Zoom; }

private:
  void updateCameraVectors();

  glm::vec3 Position;
  glm::vec3 Front;
  glm::vec3 Up;
  glm::vec3 Right;
  glm::vec3 WorldUp;

  float Yaw;
  float Pitch;
  float Zoom;
  float TargetZoom;

  float MovementSpeed;
  float MouseSensitivity;
};

#endif
