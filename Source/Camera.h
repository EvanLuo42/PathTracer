#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

struct CameraData
{
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::vec3 position;
    float fovY;
};

class Camera
{
public:
    Camera()
    {
        position = glm::vec3(0.0f, 0.5f, -2.0f);
        yaw = 90.0f;
        pitch = 0.0f;
        fovY = 60.0f;
        moveSpeed = 2.0f;
        mouseSensitivity = 0.15f;
        UpdateVectors();
    }

    void OnUpdate(GLFWwindow* window, float deltaTime)
    {
        // Only process input when right mouse button is held
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        {
            if (!captured)
            {
                captured = true;
                glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }

            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            const float dx = static_cast<float>(mouseX - lastMouseX);
            const float dy = static_cast<float>(mouseY - lastMouseY);
            lastMouseX = mouseX;
            lastMouseY = mouseY;

            yaw += dx * mouseSensitivity;
            pitch -= dy * mouseSensitivity;
            pitch = glm::clamp(pitch, -89.0f, 89.0f);
            UpdateVectors();

            // WASD movement
            float speed = moveSpeed * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
                speed *= 3.0f;

            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += front * speed;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= front * speed;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position -= right * speed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position += right * speed;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) position += up * speed;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) position -= up * speed;
        }
        else
        {
            if (captured)
            {
                captured = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        // Scroll to adjust speed
        // (handled via callback, see SetScrollCallback)
    }

    CameraData GetCameraData(float aspectRatio) const
    {
        const auto view = glm::lookAt(position, position + front, up);
        const auto proj = glm::perspective(glm::radians(fovY), aspectRatio, 0.001f, 10000.0f);

        CameraData data;
        data.invView = glm::transpose(glm::inverse(view));
        data.invProj = glm::transpose(glm::inverse(proj));
        data.position = position;
        data.fovY = glm::radians(fovY);
        return data;
    }

    void OnScroll(double yOffset)
    {
        moveSpeed = glm::clamp(moveSpeed + static_cast<float>(yOffset) * 0.3f, 0.1f, 100.0f);
    }

    glm::vec3 position;
    float fovY;
    float moveSpeed;

private:
    void UpdateVectors()
    {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        front = glm::normalize(glm::vec3(
            cos(pitchRad) * cos(yawRad),
            sin(pitchRad),
            cos(pitchRad) * sin(yawRad)
        ));
        right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
        up = glm::normalize(glm::cross(right, front));
    }

    glm::vec3 front{0, 0, 1};
    glm::vec3 right{1, 0, 0};
    glm::vec3 up{0, 1, 0};
    float yaw, pitch;
    float mouseSensitivity;

    bool captured = false;
    double lastMouseX = 0, lastMouseY = 0;
};
