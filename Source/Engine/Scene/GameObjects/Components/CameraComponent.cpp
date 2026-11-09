#include "CameraComponent.h"
#include "../../../Engine.h"
#include "../../../Input/Input.h"
#include "../../../Window/Window.h"
#include "../GameObject.h"
#include "../../Scene.h"
#include "../../JsonHelpers.h"
#include "../../../../Utils/Log.h"
#include <algorithm>
#include <cmath>

CameraComponent::CameraComponent(GameObject* owner) : Component(COMPONENT_CAMERA, owner)
{
    transform = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    UpdateCameraVectors();
}

std::unique_ptr<Component> CameraComponent::Clone(GameObject* newOwner) const
{
    auto clone = std::make_unique<CameraComponent>(newOwner);
    clone->fov = this->fov;
    clone->nearPlane = this->nearPlane;
    clone->farPlane = this->farPlane;
    clone->frustumCulling = this->frustumCulling;
    clone->movementSpeed = this->movementSpeed;
    clone->mouseSensitivity = this->mouseSensitivity;
    clone->orbitDistance = this->orbitDistance;
    clone->yaw = this->yaw;
    clone->pitch = this->pitch;
    clone->UpdateCameraVectors();
    if (!active) clone->Disable();
    return clone;
}

bool CameraComponent::IsEditorCamera() const
{
    const Scene* scene = Engine::GetInstance().scene.get();
    return scene != nullptr && owner == scene->GetEditorCameraObject();
}

void CameraComponent::Update(float dt)
{
    if (!transform) return;

    // Solo la camara del editor se controla con el raton y el teclado
    if (!IsEditorCamera()) return;

    Input* input = Engine::GetInstance().input.get();
    Scene* scene = Engine::GetInstance().scene.get();
    GameObject* selectedObject = scene ? scene->GetSelected() : nullptr;

    SDL_Window* sdlWindow = Engine::GetInstance().window->GetWindow();
    bool windowHasFocus = !input->GetWindowEvent(WE_HIDE);

    if (!windowHasFocus)
    {
        if (wasRelativeMouseMode)
        {
            SDL_SetWindowRelativeMouseMode(sdlWindow, false);
            wasRelativeMouseMode = false;
        }
        isControllingCamera = false;
        return; // Salimos en este frame si no hay foco
    }

    bool rmbDown = (input->GetMouseButton(SDL_BUTTON_RIGHT) == KEY_DOWN);
    bool rmbHeld = (rmbDown || input->GetMouseButton(SDL_BUTTON_RIGHT) == KEY_REPEAT);
    bool altHeld = (input->GetKey(SDL_SCANCODE_LALT) == KEY_DOWN || input->GetKey(SDL_SCANCODE_LALT) == KEY_REPEAT);
    bool lmbDown = (input->GetMouseButton(SDL_BUTTON_LEFT) == KEY_DOWN);
    bool lmbHeld = (lmbDown || input->GetMouseButton(SDL_BUTTON_LEFT) == KEY_REPEAT);

    // Solo iniciamos el control de cámara si el clic comienza dentro del viewport de Scene
    if ((rmbDown || (altHeld && lmbDown && selectedObject != nullptr)) && viewportHovered && !isControllingCamera)
    {
        isControllingCamera = true;
    }

    if (!rmbHeld && !(altHeld && lmbHeld))
    {
        isControllingCamera = false;
    }

    bool isFreeLooking = isControllingCamera && rmbHeld;
    bool isOrbiting = isControllingCamera && altHeld && lmbHeld && selectedObject != nullptr;
    bool activeControl = isFreeLooking || isOrbiting;
    isControllingCamera = activeControl;

    // Comprobar si la ventana actual sigue teniendo el foco
    if (activeControl && !wasRelativeMouseMode)
    {
        SDL_GetMouseState(&lockedMouseX, &lockedMouseY);
        SDL_CaptureMouse(false);
        SDL_SetWindowRelativeMouseMode(sdlWindow, true);
        wasRelativeMouseMode = true;
    }
    // Se fuerza la liberación si se pierde el control O la ventana pierde el foco
    else if ((!activeControl) && wasRelativeMouseMode)
    {
        SDL_SetWindowRelativeMouseMode(sdlWindow, false);
        SDL_WarpMouseInWindow(sdlWindow, lockedMouseX, lockedMouseY);
        wasRelativeMouseMode = false;
        isControllingCamera = false;
    }

    float currentSpeed = movementSpeed;
    if (input->GetKey(SDL_SCANCODE_LSHIFT))
    {
        currentSpeed *= 2.0f;
    }

    // Rueda del ratón: acercar / alejar la camara en la direccion en la que mira (como Unity)
    if (viewportHovered || activeControl)
    {
        float scroll = input->GetMouseScrollDelta();
        if (scroll != 0.0f)
        {
            transform->SetPosition(transform->GetPosition() + front * (scroll * currentSpeed * 0.5f));
        }
    }

    // Botón derecho: Free look y WASD
    if (isFreeLooking)
    {
        glm::vec2 mouseDelta = input->GetMouseDelta();
        yaw += mouseDelta.x * mouseSensitivity;
        pitch -= mouseDelta.y * mouseSensitivity;
        pitch = std::max(-89.0f, std::min(pitch, 89.0f));

        UpdateCameraVectors();

        glm::vec3 pos = transform->GetPosition();
        float step = currentSpeed * (dt / 1000.0f);

        if (input->GetKey(SDL_SCANCODE_W)) pos += front * step;
        if (input->GetKey(SDL_SCANCODE_S)) pos -= front * step;
        if (input->GetKey(SDL_SCANCODE_A)) pos -= right * step;
        if (input->GetKey(SDL_SCANCODE_D)) pos += right * step;
        if (input->GetKey(SDL_SCANCODE_E)) pos += worldUp * step;
        if (input->GetKey(SDL_SCANCODE_Q)) pos -= worldUp * step;

        transform->SetPosition(pos);
    }

    // Alt + Click Izquierdo: Orbitar alrededor del objeto seleccionado (centro de su AABB)
    if (isOrbiting && selectedObject)
    {
        glm::vec2 mouseDelta = input->GetMouseDelta();
        yaw += mouseDelta.x * mouseSensitivity;
        pitch -= mouseDelta.y * mouseSensitivity;
        pitch = std::max(-89.0f, std::min(pitch, 89.0f));

        UpdateCameraVectors();

        const AABB bounds = selectedObject->GetWorldAABB(true);
        const glm::vec3 targetPos = bounds.IsValid() ? bounds.GetCenter() : glm::vec3(selectedObject->GetWorldMatrix()[3]);
        transform->SetPosition(targetPos - (front * orbitDistance));
    }

    // Tecla F: Focus en el objeto seleccionado (cuando el viewport está activo)
    if (viewportHovered && input->GetKeyDown(SDL_SCANCODE_F) && selectedObject)
    {
        FocusOnObject(selectedObject);
    }
}

void CameraComponent::UpdateCameraVectors()
{
    glm::vec3 newFront;
    newFront.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    newFront.y = std::sin(glm::radians(pitch));
    newFront.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));

    front = glm::normalize(newFront);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

glm::mat4 CameraComponent::GetViewMatrix() const
{
    if (!transform) return glm::mat4(1.0f);

    // Camara de juego: la orientacion sale de su TransformComponent (editable en el Inspector)
    if (!IsEditorCamera())
    {
        glm::mat4 worldMat = owner->GetWorldMatrix();
        glm::vec3 camPos = glm::vec3(worldMat[3]);
        glm::vec3 camForward = glm::normalize(-glm::vec3(worldMat[2])); // -Z local es hacia adelante en OpenGL
        glm::vec3 camUp = glm::normalize(glm::vec3(worldMat[1]));      // +Y local es hacia arriba
        return glm::lookAt(camPos, camPos + camForward, camUp);
    }

    // Camara del editor: vectores controlados por el raton (yaw/pitch)
    glm::vec3 pos = transform->GetPosition();
    return glm::lookAt(pos, pos + front, up);
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspectRatio) const
{
    // Valores protegidos: un aspect ratio 0 o un near >= far darian una matriz invalida
    const float safeAspect = (aspectRatio > 0.0f) ? aspectRatio : 1.0f;
    const float safeFov = std::clamp(fov, 1.0f, 179.0f);
    const float safeNear = std::max(nearPlane, 0.001f);
    const float safeFar = std::max(farPlane, safeNear + 0.01f);
    return glm::perspective(glm::radians(safeFov), safeAspect, safeNear, safeFar);
}

Frustum CameraComponent::GetFrustum(float aspectRatio) const
{
    return Frustum(GetProjectionMatrix(aspectRatio) * GetViewMatrix());
}

glm::vec3 CameraComponent::GetWorldPosition() const
{
    if (!transform) return glm::vec3(0.0f);
    if (IsEditorCamera()) return transform->GetPosition();
    return glm::vec3(owner->GetWorldMatrix()[3]);
}

Ray CameraComponent::ScreenPointToRay(float mouseX, float mouseY, float width, float height) const
{
    // Pixel -> coordenadas normalizadas [-1, 1] (en pantalla la Y crece hacia abajo)
    const float x = (2.0f * mouseX) / width - 1.0f;
    const float y = 1.0f - (2.0f * mouseY) / height;

    // Deshacemos proyeccion y vista para un punto en el plano cercano y otro en el lejano
    const glm::mat4 inverseViewProjection = glm::inverse(GetProjectionMatrix(width / height) * GetViewMatrix());
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(x, y, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    return Ray(glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint));
}

void CameraComponent::FocusOnObject(GameObject* target)
{
    if (!target || !transform) return;
    if (Engine::GetInstance().scene)
    {
        Engine::GetInstance().scene->SetSelected(target);
    }

    // Encuadramos la AABB del objeto (y de sus hijos); si no tiene malla, su posicion
    const AABB bounds = target->GetWorldAABB(true);
    glm::vec3 targetPos = glm::vec3(target->GetWorldMatrix()[3]);
    if (bounds.IsValid())
    {
        targetPos = bounds.GetCenter();
        const float radius = glm::length(bounds.GetSize()) * 0.5f;
        const float halfFov = glm::radians(std::max(fov, 1.0f)) * 0.5f;
        orbitDistance = std::max(1.0f, radius / std::sin(halfFov));
    }
    transform->SetPosition(targetPos - (front * orbitDistance));
}

void CameraComponent::ResetCamera()
{
    yaw = -90.0f;
    pitch = 0.0f;
    fov = 45.0f;
    UpdateCameraVectors();
    if (transform)
    {
        transform->SetPosition(glm::vec3(0.0f, 1.0f, 5.0f));
    }
}

void CameraComponent::LookAtFrom(const glm::vec3& position, const glm::vec3& target)
{
    if (!transform) return;
    transform->SetPosition(position);

    const glm::vec3 direction = target - position;
    if (glm::length(direction) < 1e-6f) return;

    // front = (cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch)) -> despejamos yaw y pitch
    const glm::vec3 dir = glm::normalize(direction);
    pitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    pitch = std::max(-89.0f, std::min(pitch, 89.0f));
    yaw = glm::degrees(std::atan2(dir.z, dir.x));
    UpdateCameraVectors();
}

void CameraComponent::PerformRaycast(float mX, float mY, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f || !transform) return;

    Input* input = Engine::GetInstance().input.get();
    if (input && (input->GetKey(SDL_SCANCODE_LALT) == KEY_DOWN || input->GetKey(SDL_SCANCODE_LALT) == KEY_REPEAT)) return;

    Scene* scene = Engine::GetInstance().scene.get();
    if (!scene) return;

    const Ray ray = ScreenPointToRay(mX, mY, width, height);

    // La escena usa el octree: solo prueba los triangulos de los objetos cuya AABB toca el rayo
    float hitDistance = 0.0f;
    GameObject* closestObj = scene->PickObject(ray, &hitDistance);

    // Guardamos el rayo para dibujarlo en la vista Scene (depuracion)
    scene->SetLastPickRay(ray, closestObj ? hitDistance : farPlane);

    scene->SetSelected(closestObj);
    if (closestObj)
    {
        LOG("[Scene] GameObject seleccionado por Raycast: '%s' (distancia %.2f)", closestObj->GetName().c_str(), hitDistance);
    }
}

void CameraComponent::Save(nlohmann::json& out) const
{
    out["fov"] = fov;
    out["near"] = nearPlane;
    out["far"] = farPlane;
    out["frustumCulling"] = frustumCulling;
    out["active"] = active;
}

void CameraComponent::Load(const nlohmann::json& in)
{
    JsonHelpers::Read(in, "fov", fov);
    JsonHelpers::Read(in, "near", nearPlane);
    JsonHelpers::Read(in, "far", farPlane);
    JsonHelpers::Read(in, "frustumCulling", frustumCulling);

    fov = std::clamp(fov, 1.0f, 179.0f);
    nearPlane = std::max(nearPlane, 0.001f);
    farPlane = std::max(farPlane, nearPlane + 0.01f);

    bool isActive = true;
    JsonHelpers::Read(in, "active", isActive);
    if (isActive) Enable();
    else Disable();
}
