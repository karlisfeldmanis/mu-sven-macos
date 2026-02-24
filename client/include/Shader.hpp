#ifndef SHADER_HPP
#define SHADER_HPP

#include <GL/glew.h>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

class Shader {
public:
  GLuint ID;

  Shader(const char *vertexPath, const char *fragmentPath) {
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
      vShaderFile.open(vertexPath);
      fShaderFile.open(fragmentPath);
      std::stringstream vShaderStream, fShaderStream;
      vShaderStream << vShaderFile.rdbuf();
      fShaderStream << fShaderFile.rdbuf();
      vShaderFile.close();
      fShaderFile.close();
      vertexCode = vShaderStream.str();
      fragmentCode = fShaderStream.str();
    } catch (std::ifstream::failure &e) {
      std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what()
                << std::endl;
    }
    const char *vShaderCode = vertexCode.c_str();
    const char *fShaderCode = fragmentCode.c_str();

    GLuint vertex, fragment;
    int success;
    char infoLog[512];

    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(vertex, 512, NULL, infoLog);
      std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
                << infoLog << std::endl;
    }

    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(fragment, 512, NULL, infoLog);
      std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
                << infoLog << std::endl;
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success) {
      glGetProgramInfoLog(ID, 512, NULL, infoLog);
      std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
                << infoLog << std::endl;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
  }

  void use() { glUseProgram(ID); }

  GLint loc(const std::string &name) const {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
      return it->second;
    GLint l = glGetUniformLocation(ID, name.c_str());
    m_uniformCache[name] = l;
    return l;
  }

  void setBool(const std::string &name, bool value) const {
    glUniform1i(loc(name), (int)value);
  }
  void setInt(const std::string &name, int value) const {
    glUniform1i(loc(name), value);
  }
  void setFloat(const std::string &name, float value) const {
    glUniform1f(loc(name), value);
  }
  void setVec2(const std::string &name, const glm::vec2 &value) const {
    glUniform2fv(loc(name), 1, &value[0]);
  }
  void setVec3(const std::string &name, const glm::vec3 &value) const {
    glUniform3fv(loc(name), 1, &value[0]);
  }
  void setVec3(const std::string &name, float x, float y, float z) const {
    glUniform3f(loc(name), x, y, z);
  }
  void setMat4(const std::string &name, const glm::mat4 &mat) const {
    glUniformMatrix4fv(loc(name), 1, GL_FALSE, &mat[0][0]);
  }

private:
  mutable std::unordered_map<std::string, GLint> m_uniformCache;
};

#endif
