#pragma once
#include "Component.h"
#include "TransformComponent.h"
#include "../../../Geometry/Frustum.h"
#include "../../../Geometry/Ray.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <memory>

class GameObject;

// =====================================================================================
// CameraComponent: camara con proyeccion en perspectiva.
//
// Se usa para dos cosas:
//   - La camara del EDITOR (objeto interno "EditorCamera", no aparece en la jerarquia):
//     se mueve con el raton/teclado estilo Unity (orientacion por yaw/pitch).
//   - Las camaras de JUEGO (p.ej. "Main Camera"): son GameObjects normales de la escena,
//     su orientacion sale de su TransformComponent y se pueden editar en el Inspector.
//     La "Main Camera" es la que dibuja la ventana Game y la que hace el frustum culling.
// =====================================================================================
class CameraComponent : public Component
{
public:
    CameraComponent(GameObject* owner);
    ~CameraComponent() override = default;

    std::unique_ptr<Component> Clone(GameObject* newOwner) const override;

    void Update(float dt) override;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio) const;

    // Frustum (en espacio mundo) para el aspect ratio de la ventana donde se dibuja
    Frustum GetFrustum(float aspectRatio) const;

    // Posicion de la camara en el mundo
    glm::vec3 GetWorldPosition() const;

    // Rayo que sale de la camara y pasa por el pixel (mouseX, mouseY) de un viewport de width x height
    Ray ScreenPointToRay(float mouseX, float mouseY, float width, float height) const;

    // Seleccion con el raton: lanza el rayo contra la escena (acelerado con el octree)
    void PerformRaycast(float mouseX, float mouseY, float width, float height);
    void FocusOnObject(GameObject* target);
    void ResetCamera();

    // Solo camara del editor: la coloca en 'position' mirando hacia 'target'
    void LookAtFrom(const glm::vec3& position, const glm::vec3& target);

    // El Editor informa si el ratón está sobre el viewport de Scene para filtrar el input de cámara
    void SetViewportHovered(bool hovered) { viewportHovered = hovered; }
    bool IsViewportHovered() const { return viewportHovered; }
    bool IsControllingCamera() const { return isControllingCamera; }

    // El Editor informa del estado del gizmo de transformacion de la vista Scene (prioridad de input):
    //   hovered -> el raton esta encima del gizmo: Alt + clic izquierdo es para el gizmo, no empieza a orbitar
    //   inUse   -> se esta arrastrando el gizmo: la camara no se mueve (ni clic derecho, ni rueda, ni F)
    void SetGizmoState(bool hovered, bool inUse) { gizmoHovered = hovered; gizmoInUse = inUse; }

    // Parametros editables desde el Inspector
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool frustumCulling = true; // descartar las mallas que quedan fuera del frustum

    float GetMovementSpeed() const { return movementSpeed; }
    void SetMovementSpeed(float speed) { movementSpeed = speed; }

    float GetMouseSensitivity() const { return mouseSensitivity; }
    void SetMouseSensitivity(float sens) { mouseSensitivity = sens; }

    float GetOrbitDistance() const { return orbitDistance; }
    void SetOrbitDistance(float dist) { orbitDistance = dist; }

    void Save(nlohmann::json& out) const override;
    void Load(const nlohmann::json& in) override;

private:
    void UpdateCameraVectors();
    bool IsEditorCamera() const;

    TransformComponent* transform = nullptr;
    bool viewportHovered = false;
    bool gizmoHovered = false; // estado del gizmo del editor en el ultimo frame (ver SetGizmoState)
    bool gizmoInUse = false;
    bool isControllingCamera = false;
    bool wasRelativeMouseMode = false;
    float lockedMouseX = 0.0f;
    float lockedMouseY = 0.0f;

    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    float yaw = -90.0f;
    float pitch = 0.0f;

    float movementSpeed = 5.0f;
    float mouseSensitivity = 0.1f;
    float orbitDistance = 5.0f;
};
