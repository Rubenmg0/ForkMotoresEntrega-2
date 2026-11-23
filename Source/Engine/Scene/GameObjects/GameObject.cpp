#include "GameObject.h"
#include "./Components/Component.h"
#include "./Components/TransformComponent.h"
#include "./Components/MeshComponent.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#ifdef _DEBUG
#include <tracy/Tracy.hpp>
#endif

// Empieza en 1 para que un octree recien creado (version 0) se construya la primera vez
uint64_t GameObject::spatialVersion = 1;

GameObject::GameObject(const std::string& name, GameUUID id) : uuid(id), name(name), parent(nullptr)
{
}

GameObject::GameObject(const GameObject& other)
    : uuid(GameUUID()), active(other.active), name(other.name), parent(nullptr)
{
    // El array se recorre en orden: el Transform (indice 0) se clona antes que la camara,
    // que lo necesita en su constructor
    for (const auto& comp : other.components)
    {
        if (comp)
        {
            AddComponent(comp->Clone(this));
        }
    }
}

GameObject::~GameObject()
{
}

GameObject& GameObject::operator=(const GameObject& other)
{
    if (this == &other) return *this;

    name = other.name;
    active = other.active;
    for (size_t i = 0; i < COMPONENT_COUNT; ++i)
    {
        if (other.components[i])
        {
            components[i] = other.components[i]->Clone(this);
        }
        else
        {
            components[i].reset();
        }
    }
    MarkSpatialDirty();
    return *this;
}

std::unique_ptr<GameObject> GameObject::Duplicate(const std::string& newName) const
{
    auto clone = std::make_unique<GameObject>(*this);
    if (!newName.empty())
    {
        clone->SetName(newName);
    }
    return clone;
}

std::unique_ptr<GameObject> GameObject::Clone(const std::string& newName) const
{
    return Duplicate(newName);
}

void GameObject::Update(float dt)
{
#ifdef _DEBUG
    ZoneScoped;
#endif

    if (!active) return;

    // Iterar directamente sobre el array fijo (cero indirectación dinámicas)
    for (auto& comp : components)
    {
        if (comp && comp->IsActive())
        {
            comp->Update(dt);
        }
    }

    for (auto child : children)
    {
        if (child->IsActive())
        {
            child->Update(dt);
        }
    }
}

void GameObject::AddComponent(std::unique_ptr<Component> component)
{
    if (!component) return;

    // Asignar directamente en el índice del enum correspondiente
    ComponentType type = component->GetType();
    components[type] = std::move(component);

    if (type == COMPONENT_MESH || type == COMPONENT_TRANSFORM)
    {
        MarkSpatialDirty();
    }
}

void GameObject::RemoveComponent(ComponentType type)
{
    // El Transform es obligatorio (la camara y la jerarquia dependen de el)
    if (type == COMPONENT_TRANSFORM || type >= COMPONENT_COUNT) return;

    components[type].reset();
    if (type == COMPONENT_MESH)
    {
        MarkSpatialDirty();
    }
}

void GameObject::Enable()
{
    if (active) return;
    active = true;
    MarkSpatialDirty();
}

void GameObject::Disable()
{
    if (!active) return;
    active = false;
    MarkSpatialDirty();
}

bool GameObject::IsActiveInHierarchy() const
{
    for (const GameObject* node = this; node != nullptr; node = node->parent)
    {
        if (!node->active) return false;
    }
    return true;
}

int GameObject::GetSiblingIndex() const
{
    if (parent == nullptr) return 0;
    const auto& siblings = parent->children;
    const auto it = std::find(siblings.begin(), siblings.end(), this);
    return (it == siblings.end()) ? 0 : static_cast<int>(it - siblings.begin());
}

void GameObject::SetSiblingIndex(int index)
{
    if (parent == nullptr) return;
    auto& siblings = parent->children;
    const auto it = std::find(siblings.begin(), siblings.end(), this);
    if (it == siblings.end()) return;
    siblings.erase(it);
    index = std::clamp(index, 0, static_cast<int>(siblings.size()));
    siblings.insert(siblings.begin() + index, this);
}

bool GameObject::SetParent(GameObject* newParent, bool keepWorldTransform, int siblingIndex)
{
    if (newParent == parent)
    {
        if (siblingIndex >= 0) SetSiblingIndex(siblingIndex);
        return true;
    }
    if (!SetParent(newParent, keepWorldTransform)) return false;
    if (siblingIndex >= 0) SetSiblingIndex(siblingIndex);
    return true;
}

bool GameObject::SetParent(GameObject* newParent, bool keepWorldTransform)
{
    if (newParent == parent) return true;

    if (newParent == this || (newParent && newParent->IsDescendantOf(this)))
    {
        return false;
    }

    TransformComponent* trans = GetComponent<TransformComponent>();

    // Guardamos la matriz de mundo actual antes de cambiar de padre
    glm::mat4 oldWorldMatrix = GetWorldMatrix();

    if (parent)
    {
        auto& siblings = parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    parent = newParent;
    if (parent)
    {
        parent->children.push_back(this);
    }

    // Recalculamos el Transform local para que mantenga su posición, rotación y tamaño en el mundo
    if (trans && keepWorldTransform)
    {
        glm::mat4 newParentWorld = parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
        glm::mat4 newLocalMatrix = glm::inverse(newParentWorld) * oldWorldMatrix;

        glm::vec3 newPos = glm::vec3(newLocalMatrix[3]);

        glm::vec3 col0 = glm::vec3(newLocalMatrix[0]);
        glm::vec3 col1 = glm::vec3(newLocalMatrix[1]);
        glm::vec3 col2 = glm::vec3(newLocalMatrix[2]);

        glm::vec3 newScale;
        newScale.x = glm::length(col0);
        newScale.y = glm::length(col1);
        newScale.z = glm::length(col2);

        glm::mat3 rotMat(1.0f);
        if (newScale.x > 1e-6f) rotMat[0] = col0 / newScale.x;
        if (newScale.y > 1e-6f) rotMat[1] = col1 / newScale.y;
        if (newScale.z > 1e-6f) rotMat[2] = col2 / newScale.z;

        float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
        glm::extractEulerAngleXYZ(glm::mat4(rotMat), rotX, rotY, rotZ);
        glm::vec3 newRot = glm::degrees(glm::vec3(rotX, rotY, rotZ));

        trans->SetPosition(newPos);
        trans->SetRotation(newRot);
        trans->SetScale(newScale);
    }

    InvalidateWorldTransform();

    // Puede haber cambiado de mundo (y de "activo en jerarquia"): el octree debe actualizarse
    MarkSpatialDirty();
    return true;
}

bool GameObject::AddChild(GameObject* child)
{
    return child != nullptr && child->SetParent(this);
}

void GameObject::RemoveChild(GameObject* child)
{
    if (child != nullptr && child->parent == this)
    {
        child->SetParent(nullptr);
    }
}

bool GameObject::IsDescendantOf(const GameObject* ancestor) const
{
    if (ancestor == nullptr) return false;
    for (const GameObject* node = parent; node != nullptr; node = node->parent)
    {
        if (node == ancestor) return true;
    }
    return false;
}

glm::mat4 GameObject::GetWorldMatrix() const
{
    if (const TransformComponent* t = GetComponent<TransformComponent>())
    {
        return t->GetWorldMatrix();
    }
    return parent ? parent->GetWorldMatrix() : glm::mat4(1.0f);
}

void GameObject::InvalidateWorldTransform()
{
    if (TransformComponent* t = GetComponent<TransformComponent>())
    {
        t->InvalidateWorld();
        return;
    }

    for (GameObject* child : children)
    {
        child->InvalidateWorldTransform();
    }
}

AABB GameObject::GetWorldAABB(bool includeChildren) const
{
    AABB result;

    const MeshComponent* mesh = GetComponent<MeshComponent>();
    if (mesh != nullptr && mesh->IsActive())
    {
        const AABB local = mesh->GetLocalAABB();
        if (local.IsValid())
        {
            // AABB local transformada por la matriz de mundo -> nueva AABB alineada con los ejes del mundo
            result.Enclose(local.Transformed(GetWorldMatrix()));
        }
    }

    if (includeChildren)
    {
        for (const GameObject* child : children)
        {
            if (child->IsActive())
            {
                result.Enclose(child->GetWorldAABB(true));
            }
        }
    }
    return result;
}
