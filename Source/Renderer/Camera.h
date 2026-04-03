#pragma once

#include "../Core/ShaderVar.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <GLFW/glfw3.h>

struct CameraData
{
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 invViewMatrix;
    glm::mat4 invProjMatrix;
    glm::mat4 viewProjMatrix;
    glm::mat4 invViewProjMatrix;

    glm::vec3 position;
    float fovY;

    glm::vec3 forward;
    float aspectRatio;

    glm::vec3 up;
    float nearZ;

    glm::vec3 right;
    float farZ;

    glm::vec2 jitter;
    uint32_t frameIndex;
    float _pad;
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

    void Bind(const ShaderVar& var) const
    {
        auto cam = var["gCamera"];
        cam["viewMatrix"] = data.viewMatrix;
        cam["projMatrix"] = data.projMatrix;
        cam["invViewMatrix"] = data.invViewMatrix;
        cam["invProjMatrix"] = data.invProjMatrix;
        cam["viewProjMatrix"] = data.viewProjMatrix;
        cam["invViewProjMatrix"] = data.invViewProjMatrix;
        cam["position"] = data.position;
        cam["fovY"] = data.fovY;
        cam["forward"] = data.forward;
        cam["aspectRatio"] = data.aspectRatio;
        cam["up"] = data.up;
        cam["nearZ"] = data.nearZ;
        cam["right"] = data.right;
        cam["farZ"] = data.farZ;
        cam["jitter"] = data.jitter;
        cam["frameIndex"] = data.frameIndex;
    }

    void OnUpdate(GLFWwindow* window, float deltaTime)
    {
        auto& imguiIO = ImGui::GetIO();

        // Only process input when right mouse button is held and ImGui doesn't want the mouse
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !imguiIO.WantCaptureMouse)
        {
            if (!captured)
            {
                captured = true;
                glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                imguiIO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
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

            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
                position += front * speed;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
                position -= front * speed;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
                position -= right * speed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
                position += right * speed;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
                position += up * speed;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
                position -= up * speed;
        }
        else
        {
            if (captured)
            {
                captured = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                imguiIO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
        }

        // Scroll to adjust speed
        // (handled via callback, see SetScrollCallback)
    }

    void Update(float aspectRatio)
    {
        constexpr float nearZ = 0.001f;
        constexpr float farZ = 10000.0f;

        const auto view = glm::lookAt(position, position + front, up);
        const auto proj = glm::perspectiveRH_ZO(glm::radians(fovY), aspectRatio, nearZ, farZ);
        const auto viewProj = proj * view;

        data.viewMatrix = glm::transpose(view);
        data.projMatrix = glm::transpose(proj);
        data.invViewMatrix = glm::transpose(glm::inverse(view));
        data.invProjMatrix = glm::transpose(glm::inverse(proj));
        data.viewProjMatrix = glm::transpose(viewProj);
        data.invViewProjMatrix = glm::transpose(glm::inverse(viewProj));

        data.position = position;
        data.fovY = glm::radians(fovY);
        data.forward = front;
        data.aspectRatio = aspectRatio;
        data.up = up;
        data.nearZ = nearZ;
        data.right = right;
        data.farZ = farZ;

        data.jitter = glm::vec2(0.0f);
        data.frameIndex = frameIndex++;
        data._pad = 0.0f;
    }

    [[nodiscard]] const CameraData& GetData() const { return data; }

    void OnScroll(double yOffset) { moveSpeed = glm::clamp(moveSpeed + static_cast<float>(yOffset) * 0.3f, 0.1f, 100.0f); }

    void OnRenderUI()
    {
        ImGui::DragFloat3("Position", &position.x, 0.1f);
        bool orientChanged = false;
        orientChanged |= ImGui::DragFloat("Yaw", &yaw, 0.5f);
        orientChanged |= ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f);
        if (orientChanged)
            UpdateVectors();
        ImGui::SliderFloat("FoV", &fovY, 10.0f, 120.0f);
        ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 0.1f, 100.0f);
        ImGui::SliderFloat("Sensitivity", &mouseSensitivity, 0.01f, 1.0f);
    }

    glm::vec3 position{};
    float fovY;
    float moveSpeed;

private:
    void UpdateVectors()
    {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        front = glm::normalize(glm::vec3(cos(pitchRad) * cos(yawRad), sin(pitchRad), cos(pitchRad) * sin(yawRad)));
        right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
        up = glm::normalize(glm::cross(right, front));
    }

    glm::vec3 front{0, 0, 1};
    glm::vec3 right{1, 0, 0};
    glm::vec3 up{0, 1, 0};
    float yaw, pitch;
    float mouseSensitivity;

    CameraData data{};
    uint32_t frameIndex = 0;
    bool captured = false;
    double lastMouseX = 0, lastMouseY = 0;

};
