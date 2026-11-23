#include "CameraComponent.h"
#include "../../../Engine.h"
#include "../../../Input/Input.h"
#include "../../../Window/Window.h"
#include "../GameObject.h"
#include "../../Scene.h"
#include "../../JsonHelpers.h"
#include "../../../../Utils/Log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PITCH_LIMIT = 89.9f;
    constexpr float MIN_DISTANCE = 0.01f;
    constexpr float MAX_DISTANCE = 100000.0f;
    constexpr float MIN_FLY_SPEED = 0.05f;
    constexpr float MAX_FLY_SPEED = 500.0f;

    // Diferencia de angulos (grados) por el camino mas corto: -180..180
    float ShortestAngle(float from, float to)
    {
        float delta = std::fmod(to - from, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return delta;
    }
}

CameraComponent::CameraComponent(GameObject* owner) : Component(COMPONENT_CAMERA, owner)
{
    transform = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    UpdateCameraVectors();
    if (transform) pivot = transform->GetPosition() + front * distance;
}

std::unique_ptr<Component> CameraComponent::Clone(GameObject* newOwner) const
{
    auto clone = std::make_unique<CameraComponent>(newOwner);
    clone->fov = this->fov;
    clone->nearPlane = this->nearPlane;
    clone->farPlane = this->farPlane;
    clone->frustumCulling = this->frustumCulling;
    clone->orthographic = this->orthographic;
    clone->orthoSize = this->orthoSize;
    clone->movementSpeed = this->movementSpeed;
    clone->mouseSensitivity = this->mouseSensitivity;
    clone->distance = this->distance;
    clone->pivot = this->pivot;
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

float CameraComponent::GetHalfHeightAtPivot() const
{
    const float halfFov = glm::radians(std::clamp(fov, 1.0f, 179.0f)) * 0.5f;
    return distance * std::tan(halfFov);
}

void CameraComponent::Update(float dt)
{
    if (!transform) return;

    // Solo la camara del editor se controla con el raton y el teclado
    if (!IsEditorCamera()) return;

    const float dtSeconds = dt / 1000.0f;
    Input* input = Engine::GetInstance().input.get();
    SDL_Window* sdlWindow = Engine::GetInstance().window->GetWindow();
    const bool windowHasFocus = !input->GetWindowEvent(WE_HIDE);

    if (speedOverlayTime > 0.0f) speedOverlayTime = std::max(0.0f, speedOverlayTime - dtSeconds);

    if (!windowHasFocus)
    {
        if (wasRelativeMouseMode)
        {
            SDL_SetWindowRelativeMouseMode(sdlWindow, false);
            wasRelativeMouseMode = false;
        }
        navMode = NavMode::None;
        return;
    }

    const bool alt = input->IsAlt();
    const bool shift = input->IsShift();

    // ---------------------------------------------------------------------------------
    // 1) Empezar una navegacion: solo si el clic empieza dentro de la vista Scene y no se esta
    //    arrastrando el gizmo. El clic izquierdo encima del gizmo es siempre del gizmo.
    // ---------------------------------------------------------------------------------
    if (navMode == NavMode::None && viewportHovered && !gizmoInUse)
    {
        if (input->IsMouseDown(SDL_BUTTON_RIGHT))
        {
            navMode = alt ? NavMode::Zoom : NavMode::Fly;
            navButton = SDL_BUTTON_RIGHT;
        }
        else if (input->IsMouseDown(SDL_BUTTON_MIDDLE))
        {
            navMode = NavMode::Pan;
            navButton = SDL_BUTTON_MIDDLE;
        }
        else if (input->IsMouseDown(SDL_BUTTON_LEFT) && !gizmoHovered && (alt || handToolActive))
        {
            navMode = alt ? NavMode::Orbit : NavMode::Pan;
            navButton = SDL_BUTTON_LEFT;
        }

        if (navMode != NavMode::None)
        {
            animating = false;
            flyTime = 0.0f;
        }
    }

    // Se termina al soltar el boton que la empezo
    if (navMode != NavMode::None && !input->IsMouseHeld(navButton))
    {
        navMode = NavMode::None;
    }

    // ---------------------------------------------------------------------------------
    // 2) Mientras se navega, el raton se oculta y queda fijo (modo relativo de SDL).
    //    Al soltar vuelve a aparecer donde estaba.
    // ---------------------------------------------------------------------------------
    const bool activeControl = (navMode != NavMode::None);
    if (activeControl && !wasRelativeMouseMode)
    {
        SDL_GetMouseState(&lockedMouseX, &lockedMouseY);
        SDL_CaptureMouse(false);
        SDL_SetWindowRelativeMouseMode(sdlWindow, true);
        wasRelativeMouseMode = true;
    }
    else if (!activeControl && wasRelativeMouseMode)
    {
        SDL_SetWindowRelativeMouseMode(sdlWindow, false);
        SDL_WarpMouseInWindow(sdlWindow, lockedMouseX, lockedMouseY);
        wasRelativeMouseMode = false;
    }

    const glm::vec2 mouseDelta = activeControl ? input->GetMouseDelta() : glm::vec2(0.0f);
    const float scroll = input->GetMouseScrollDelta();

    switch (navMode)
    {
    case NavMode::Fly:
    {
        // Mirar alrededor girando sobre la POSICION de la camara (el pivote se mueve con ella)
        const glm::vec3 eye = pivot - front * distance;
        yaw += mouseDelta.x * mouseSensitivity;
        pitch = std::clamp(pitch - mouseDelta.y * mouseSensitivity, -PITCH_LIMIT, PITCH_LIMIT);
        UpdateCameraVectors();

        glm::vec3 move(0.0f);
        if (input->IsKeyHeld(SDL_SCANCODE_W)) move += front;
        if (input->IsKeyHeld(SDL_SCANCODE_S)) move -= front;
        if (input->IsKeyHeld(SDL_SCANCODE_D)) move += right;
        if (input->IsKeyHeld(SDL_SCANCODE_A)) move -= right;
        if (input->IsKeyHeld(SDL_SCANCODE_E)) move += worldUp;
        if (input->IsKeyHeld(SDL_SCANCODE_Q)) move -= worldUp;

        glm::vec3 newEye = eye;
        if (glm::length(move) > 0.0f)
        {
            // Aceleracion: cuanto mas tiempo se mantiene la tecla, mas rapido (hasta x2.5)
            flyTime += dtSeconds;
            const float acceleration = 1.0f + std::min(flyTime, 3.0f) * 0.5f;
            const float speed = movementSpeed * acceleration * (shift ? 3.0f : 1.0f);
            newEye += glm::normalize(move) * speed * dtSeconds;
        }
        else
        {
            flyTime = 0.0f;
        }

        // Con el clic derecho pulsado la rueda cambia la velocidad de vuelo (como en Unity)
        if (scroll != 0.0f)
        {
            movementSpeed = std::clamp(movementSpeed * std::pow(1.2f, scroll), MIN_FLY_SPEED, MAX_FLY_SPEED);
            speedOverlayTime = 1.5f;
        }

        pivot = newEye + front * distance;
        break;
    }
    case NavMode::Orbit:
    {
        yaw += mouseDelta.x * mouseSensitivity;
        pitch = std::clamp(pitch - mouseDelta.y * mouseSensitivity, -PITCH_LIMIT, PITCH_LIMIT);
        UpdateCameraVectors();
        break;
    }
    case NavMode::Pan:
    {
        // Unidades del mundo por pixel a la distancia del pivote: el punto bajo el raton lo sigue
        const float worldPerPixel = (2.0f * GetHalfHeightAtPivot()) / std::max(viewportHeightPx, 1.0f);
        pivot -= right * (mouseDelta.x * worldPerPixel);
        pivot += up * (mouseDelta.y * worldPerPixel);
        break;
    }
    case NavMode::Zoom:
    {
        const float amount = mouseDelta.x - mouseDelta.y;
        distance = std::clamp(distance * std::exp(-amount * 0.006f), MIN_DISTANCE, MAX_DISTANCE);
        break;
    }
    case NavMode::None:
    default:
        break;
    }

    // Rueda: zoom hacia el pivote, proporcional a la distancia (mas lento cuanto mas cerca)
    if (navMode != NavMode::Fly && scroll != 0.0f && viewportHovered && !gizmoInUse)
    {
        animating = false;
        distance = std::clamp(distance * std::pow(0.88f, scroll), MIN_DISTANCE, MAX_DISTANCE);
    }

    UpdateAnimation(dtSeconds);
    SyncTransform();
}

void CameraComponent::UpdateAnimation(float dtSeconds)
{
    if (!animating) return;

    // Aproximacion exponencial: ~95 % del recorrido en 0.2 s, independiente de los FPS
    const float t = 1.0f - std::exp(-dtSeconds * 15.0f);
    pivot += (targetPivot - pivot) * t;
    distance += (targetDistance - distance) * t;
    yaw += ShortestAngle(yaw, targetYaw) * t;
    pitch += (targetPitch - pitch) * t;
    UpdateCameraVectors();

    const bool done = glm::length(targetPivot - pivot) < 1e-3f * std::max(1.0f, distance) &&
        std::abs(targetDistance - distance) < 1e-3f * std::max(1.0f, distance) &&
        std::abs(ShortestAngle(yaw, targetYaw)) < 0.01f && std::abs(targetPitch - pitch) < 0.01f;
    if (done)
    {
        pivot = targetPivot;
        distance = targetDistance;
        yaw = targetYaw;
        pitch = targetPitch;
        UpdateCameraVectors();
        animating = false;
    }
}

void CameraComponent::SyncTransform()
{
    if (!transform) return;
    transform->SetPosition(pivot - front * distance);
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

    // Camara del editor: mira al pivote desde 'distance'
    const glm::vec3 eye = pivot - front * distance;
    return glm::lookAt(eye, eye + front, up);
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspectRatio) const
{
    // Valores protegidos: un aspect ratio 0 o un near >= far darian una matriz invalida
    const float safeAspect = (aspectRatio > 0.0f) ? aspectRatio : 1.0f;
    const float safeFov = std::clamp(fov, 1.0f, 179.0f);
    const float safeNear = std::max(nearPlane, 0.001f);
    const float safeFar = std::max(farPlane, safeNear + 0.01f);

    if (orthographic)
    {
        if (IsEditorCamera())
        {
            // Vista isometrica del editor: el alto visible es el que tendria la perspectiva a la
            // distancia del pivote (asi al alternar Persp / Iso el encuadre casi no cambia). Los
            // planos van por delante y por detras de la camara: nada se recorta al acercarse.
            const float halfHeight = std::max(GetHalfHeightAtPivot(), 0.001f);
            const float halfWidth = halfHeight * safeAspect;
            return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -safeFar, safeFar);
        }
        const float halfHeight = std::max(orthoSize, 0.001f);
        const float halfWidth = halfHeight * safeAspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, safeNear, safeFar);
    }
    return glm::perspective(glm::radians(safeFov), safeAspect, safeNear, safeFar);
}

Frustum CameraComponent::GetFrustum(float aspectRatio) const
{
    return Frustum(GetProjectionMatrix(aspectRatio) * GetViewMatrix());
}

glm::vec3 CameraComponent::GetWorldPosition() const
{
    if (!transform) return glm::vec3(0.0f);
    if (IsEditorCamera()) return pivot - front * distance;
    return glm::vec3(owner->GetWorldMatrix()[3]);
}

Ray CameraComponent::ScreenPointToRay(float mouseX, float mouseY, float width, float height) const
{
    // Pixel -> coordenadas normalizadas [-1, 1] (en pantalla la Y crece hacia abajo)
    const float x = (2.0f * mouseX) / width - 1.0f;
    const float y = 1.0f - (2.0f * mouseY) / height;

    // Deshacemos proyeccion y vista para un punto en el plano cercano y otro en el lejano
    // (funciona igual con perspectiva y con proyeccion ortografica)
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

    // Encuadramos la AABB del objeto (y de sus hijos); si no tiene malla, su posicion
    AABB bounds = target->GetWorldAABB(true);
    if (!bounds.IsValid())
    {
        const glm::vec3 position = glm::vec3(target->GetWorldMatrix()[3]);
        bounds = AABB(position - glm::vec3(1.0f), position + glm::vec3(1.0f));
    }
    FrameBounds(bounds, true);
}

void CameraComponent::FrameBounds(const AABB& bounds, bool animate)
{
    if (!bounds.IsValid()) return;

    const float radius = std::max(glm::length(bounds.GetSize()) * 0.5f, 0.05f);
    const float halfFov = glm::radians(std::max(fov, 1.0f)) * 0.5f;
    const float newDistance = std::clamp(radius / std::sin(halfFov), MIN_DISTANCE, MAX_DISTANCE);

    if (animate)
    {
        AnimateTo(bounds.GetCenter(), newDistance, yaw, pitch);
    }
    else
    {
        pivot = bounds.GetCenter();
        distance = newDistance;
        animating = false;
        SyncTransform();
    }
}

void CameraComponent::AnimateTo(const glm::vec3& newPivot, float newDistance, float newYaw, float newPitch)
{
    targetPivot = newPivot;
    targetDistance = std::clamp(newDistance, MIN_DISTANCE, MAX_DISTANCE);
    targetYaw = newYaw;
    targetPitch = std::clamp(newPitch, -PITCH_LIMIT, PITCH_LIMIT);
    animating = true;
}

void CameraComponent::AlignViewToAxis(const glm::vec3& axisSide, bool animate)
{
    if (glm::length(axisSide) < 1e-6f) return;

    // La camara se pone en el lado 'axisSide' del pivote: mira en la direccion contraria
    const glm::vec3 dir = -glm::normalize(axisSide);
    float newPitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    newPitch = std::clamp(newPitch, -PITCH_LIMIT, PITCH_LIMIT);

    // Vistas desde arriba / abajo: el yaw no se puede sacar de la direccion. Se usa -90
    // (mirando hacia -Z), asi la vista Top tiene el eje -Z hacia arriba de la pantalla.
    float newYaw = (std::abs(dir.y) > 0.99f) ? -90.0f : glm::degrees(std::atan2(dir.z, dir.x));
    // Se gira por el camino corto desde el yaw actual (sin dar vueltas completas)
    newYaw = yaw + ShortestAngle(yaw, newYaw);

    if (animate)
    {
        AnimateTo(pivot, distance, newYaw, newPitch);
    }
    else
    {
        yaw = newYaw;
        pitch = newPitch;
        animating = false;
        UpdateCameraVectors();
        SyncTransform();
    }
}

void CameraComponent::ResetCamera()
{
    fov = 45.0f;
    orthographic = false;
    pivot = glm::vec3(0.0f);
    distance = 12.0f;
    yaw = -135.0f;
    pitch = -25.0f;
    animating = false;
    UpdateCameraVectors();
    SyncTransform();
}

void CameraComponent::LookAtFrom(const glm::vec3& position, const glm::vec3& target)
{
    if (!transform) return;

    const glm::vec3 direction = target - position;
    if (glm::length(direction) < 1e-6f) return;

    // front = (cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch)) -> despejamos yaw y pitch
    const glm::vec3 dir = glm::normalize(direction);
    pitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    pitch = std::clamp(pitch, -PITCH_LIMIT, PITCH_LIMIT);
    yaw = glm::degrees(std::atan2(dir.z, dir.x));
    pivot = target;
    distance = std::max(glm::length(direction), MIN_DISTANCE);
    animating = false;
    UpdateCameraVectors();
    SyncTransform();
}

CameraComponent::EditorViewState CameraComponent::GetEditorViewState() const
{
    EditorViewState state;
    state.pivot = pivot;
    state.distance = distance;
    state.yaw = yaw;
    state.pitch = pitch;
    state.orthographic = orthographic;
    return state;
}

void CameraComponent::SetEditorViewState(const EditorViewState& state)
{
    pivot = state.pivot;
    distance = std::clamp(state.distance, MIN_DISTANCE, MAX_DISTANCE);
    yaw = state.yaw;
    pitch = std::clamp(state.pitch, -PITCH_LIMIT, PITCH_LIMIT);
    orthographic = state.orthographic;
    animating = false;
    UpdateCameraVectors();
    SyncTransform();
}

GameObject* CameraComponent::PickAt(float mX, float mY, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f || !transform) return nullptr;

    Scene* scene = Engine::GetInstance().scene.get();
    if (!scene) return nullptr;

    const Ray ray = ScreenPointToRay(mX, mY, width, height);

    // La escena usa el octree: solo prueba los triangulos de los objetos cuya AABB toca el rayo
    float hitDistance = 0.0f;
    GameObject* closestObj = scene->PickObject(ray, &hitDistance);

    // Guardamos el rayo para dibujarlo en la vista Scene (depuracion)
    scene->SetLastPickRay(ray, closestObj ? hitDistance : farPlane);
    return closestObj;
}

void CameraComponent::Save(nlohmann::json& out) const
{
    out["fov"] = fov;
    out["near"] = nearPlane;
    out["far"] = farPlane;
    out["frustumCulling"] = frustumCulling;
    out["orthographic"] = orthographic;
    out["orthoSize"] = orthoSize;
    out["active"] = active;
}

void CameraComponent::Load(const nlohmann::json& in)
{
    JsonHelpers::Read(in, "fov", fov);
    JsonHelpers::Read(in, "near", nearPlane);
    JsonHelpers::Read(in, "far", farPlane);
    JsonHelpers::Read(in, "frustumCulling", frustumCulling);
    JsonHelpers::Read(in, "orthographic", orthographic);
    JsonHelpers::Read(in, "orthoSize", orthoSize);

    fov = std::clamp(fov, 1.0f, 179.0f);
    nearPlane = std::max(nearPlane, 0.001f);
    farPlane = std::max(farPlane, nearPlane + 0.01f);
    orthoSize = std::max(orthoSize, 0.001f);

    bool isActive = true;
    JsonHelpers::Read(in, "active", isActive);
    if (isActive) Enable();
    else Disable();
}
