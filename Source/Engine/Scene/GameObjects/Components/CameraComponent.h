#pragma once
#include "Component.h"
#include "TransformComponent.h"
#include "../../../Geometry/Frustum.h"
#include "../../../Geometry/Ray.h"
#include "../../../Geometry/AABB.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <memory>

class GameObject;

// =====================================================================================
// CameraComponent: camara con proyeccion en perspectiva u ortografica.
//
// Se usa para dos cosas:
//   - La camara del EDITOR (objeto interno "EditorCamera", no aparece en la jerarquia).
//     Sigue el modelo de la SceneView de Unity: un PIVOTE (punto al que mira), una DISTANCIA
//     a ese pivote y una orientacion (yaw / pitch). La posicion sale de ahi:
//         posicion = pivote - delante * distancia
//     Controles (como Unity):
//         Clic derecho + raton      mirar alrededor (flythrough); WASD/QE para moverse,
//                                   Shift acelera, la rueda cambia la velocidad
//         Alt + clic izquierdo      orbitar alrededor del pivote
//         Clic central / Hand (Q)   desplazar (pan)
//         Alt + clic derecho        zoom (dolly) arrastrando
//         Rueda                     zoom hacia el pivote
//         F                         encuadrar la seleccion (animado)
//   - Las camaras de JUEGO (p.ej. "Main Camera"): son GameObjects normales de la escena,
//     su orientacion sale de su TransformComponent y se pueden editar en el Inspector.
//     La "Main Camera" es la que dibuja la ventana Game y la que hace el frustum culling.
// =====================================================================================
class CameraComponent : public Component
{
public:
    // Estado de la camara del editor (se guarda en el layout del editor)
    struct EditorViewState
    {
        glm::vec3 pivot = glm::vec3(0.0f);
        float distance = 10.0f;
        float yaw = -90.0f;
        float pitch = 0.0f;
        bool orthographic = false;
    };

    // Tipo de navegacion que se esta haciendo con el raton (para el cursor y el overlay de la vista Scene)
    enum class NavMode { None, Fly, Orbit, Pan, Zoom };

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

    // Lanza el rayo contra la escena (acelerado con el octree) y devuelve el objeto mas cercano.
    // No cambia la seleccion: eso lo decide el editor segun los modificadores (Ctrl / Shift).
    GameObject* PickAt(float mouseX, float mouseY, float width, float height);

    // Encuadra (animado) un objeto y sus hijos o una caja cualquiera
    void FocusOnObject(GameObject* target);
    void FrameBounds(const AABB& bounds, bool animate = true);
    void ResetCamera();

    // Solo camara del editor: la coloca en 'position' mirando hacia 'target'
    void LookAtFrom(const glm::vec3& position, const glm::vec3& target);

    // Solo camara del editor: mira a lo largo de un eje (la camara se pone en el lado 'axisSide')
    void AlignViewToAxis(const glm::vec3& axisSide, bool animate = true);
    // Solo camara del editor: anima la vista hasta una orientacion / pivote
    void AnimateTo(const glm::vec3& pivot, float distance, float yaw, float pitch);

    // El Editor informa si el ratón está sobre el viewport de Scene para filtrar el input de cámara
    void SetViewportHovered(bool hovered) { viewportHovered = hovered; }
    bool IsViewportHovered() const { return viewportHovered; }
    bool IsControllingCamera() const { return navMode != NavMode::None; }
    NavMode GetNavMode() const { return navMode; }

    // Tamano en pixeles del viewport (para que el pan siga exactamente al raton)
    void SetViewportPixelSize(float width, float height) { viewportWidthPx = width; viewportHeightPx = height; }
    // Herramienta Hand (Q) del editor: el clic izquierdo desplaza la vista
    void SetHandToolActive(bool active) { handToolActive = active; }

    // El Editor informa del estado del gizmo de transformacion de la vista Scene (prioridad de input):
    //   hovered -> el raton esta encima del gizmo: el clic izquierdo es para el gizmo
    //   inUse   -> se esta arrastrando el gizmo: la camara no se mueve (ni clic derecho, ni rueda)
    void SetGizmoState(bool hovered, bool inUse) { gizmoHovered = hovered; gizmoInUse = inUse; }

    // Parametros editables desde el Inspector
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool frustumCulling = true; // descartar las mallas que quedan fuera del frustum
    bool orthographic = false;  // proyeccion ortografica (camaras de juego)
    float orthoSize = 5.0f;     // mitad del alto visible en modo ortografico (camaras de juego)
    int targetDisplay = 0;      // display de la vista Game en el que se ve (0 = Display 1), como en Unity
    static constexpr int MAX_DISPLAYS = 8;

    // Camara del editor: perspectiva / isometrica (ortografica)
    bool IsOrthographic() const { return orthographic; }
    void SetOrthographic(bool ortho) { orthographic = ortho; }

    float GetMovementSpeed() const { return movementSpeed; }
    void SetMovementSpeed(float speed) { movementSpeed = speed; }
    // Segundos que le quedan al aviso "velocidad de la camara" de la vista Scene
    float GetSpeedOverlayTime() const { return speedOverlayTime; }

    float GetMouseSensitivity() const { return mouseSensitivity; }
    void SetMouseSensitivity(float sens) { mouseSensitivity = sens; }

    // Distancia de la camara del editor a su pivote
    float GetOrbitDistance() const { return distance; }
    void SetOrbitDistance(float dist) { distance = dist > 0.01f ? dist : 0.01f; animating = false; }

    const glm::vec3& GetPivot() const { return pivot; }
    const glm::vec3& GetFront() const { return front; }
    const glm::vec3& GetRight() const { return right; }
    const glm::vec3& GetUp() const { return up; }
    float GetYaw() const { return yaw; }
    float GetPitch() const { return pitch; }

    EditorViewState GetEditorViewState() const;
    void SetEditorViewState(const EditorViewState& state);

    void Save(nlohmann::json& out) const override;
    void Load(const nlohmann::json& in) override;

private:
    void UpdateCameraVectors();
    void SyncTransform();
    void UpdateAnimation(float dtSeconds);
    bool IsEditorCamera() const;
    float GetHalfHeightAtPivot() const; // mitad del alto visible a la distancia del pivote

    TransformComponent* transform = nullptr;
    bool viewportHovered = false;
    bool gizmoHovered = false; // estado del gizmo del editor en el ultimo frame (ver SetGizmoState)
    bool gizmoInUse = false;
    bool handToolActive = false;
    float viewportWidthPx = 800.0f;
    float viewportHeightPx = 600.0f;

    NavMode navMode = NavMode::None;
    int navButton = 0;          // boton del raton que mantiene la navegacion actual
    bool wasRelativeMouseMode = false;
    float lockedMouseX = 0.0f;
    float lockedMouseY = 0.0f;
    float flyTime = 0.0f;       // segundos moviendose con WASD (aceleracion como en Unity)
    float speedOverlayTime = 0.0f;

    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    float yaw = -90.0f;
    float pitch = 0.0f;

    // Camara del editor: pivote y distancia (la posicion se calcula a partir de ellos)
    glm::vec3 pivot = glm::vec3(0.0f);
    float distance = 5.0f;

    // Transiciones animadas (encuadrar, alinear a un eje)
    bool animating = false;
    glm::vec3 targetPivot = glm::vec3(0.0f);
    float targetDistance = 5.0f;
    float targetYaw = -90.0f;
    float targetPitch = 0.0f;

    float movementSpeed = 5.0f;
    float mouseSensitivity = 0.15f;
};
