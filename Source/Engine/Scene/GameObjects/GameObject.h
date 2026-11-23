#pragma once

#include "../../../Utils/UUID.h"
#include "../../Geometry/AABB.h"
#include "./Components/Component.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <array>

class TransformComponent;
class MeshComponent;
class MaterialComponent;
class CameraComponent;

class GameObject
{
public:
	GameObject(const std::string& name = "GameObject", GameUUID uuid = GameUUID());
	// Constructor de copia para clonado profundo de componentes con un nuevo UUID
	GameObject(const GameObject& other);
	~GameObject();

	// Operador de asignación por copia (clonado profundo)
	GameObject& operator=(const GameObject& other);

	// Duplicar el GameObject (clonado profundo de todos sus componentes y nuevo UUID generado)
	std::unique_ptr<GameObject> Duplicate(const std::string& newName = "") const;
	std::unique_ptr<GameObject> Clone(const std::string& newName = "") const;

	GameUUID GetUUID() const { return uuid; }

	void Update(float dt);

	// Añadir componente (transferencia directa)
	void AddComponent(std::unique_ptr<Component> component);

	// Quita un componente (el Transform no se puede quitar)
	void RemoveComponent(ComponentType type);

	// Acceso por tipo en tiempo de ejecucion (para el Inspector y la serializacion)
	Component* GetComponentByType(ComponentType type) { return (type < COMPONENT_COUNT) ? components[type].get() : nullptr; }
	const Component* GetComponentByType(ComponentType type) const { return (type < COMPONENT_COUNT) ? components[type].get() : nullptr; }

	// Acceso rápido en O(1) directo sin dynamic_cast
	template <typename T>
	T* GetComponent()
	{
		// Obtenemos el ID de tipo en tiempo de compilación mediante especialización implícita
		constexpr ComponentType typeId = GetComponentTypeID<T>();
		return static_cast<T*>(components[typeId].get());
	}

	template <typename T>
	const T* GetComponent() const
	{
		constexpr ComponentType typeId = GetComponentTypeID<T>();
		return static_cast<const T*>(components[typeId].get());
	}

	// keepWorldTransform = true: el objeto se queda donde estaba en el mundo (reemparentar en la
	// jerarquia). false: se conserva su transform LOCAL (crear hijos, instanciar modelos, cargar escenas).
	bool SetParent(GameObject* newParent, bool keepWorldTransform = true);
	// Igual, pero colocandolo en la posicion 'siblingIndex' entre los hijos del nuevo padre (-1 = al final)
	bool SetParent(GameObject* newParent, bool keepWorldTransform, int siblingIndex);
	// Posicion entre sus hermanos (orden de la Hierarchy; se guarda en la escena)
	int GetSiblingIndex() const;
	void SetSiblingIndex(int index);
	bool AddChild(GameObject* child);
	void RemoveChild(GameObject* child);

	bool IsDescendantOf(const GameObject* ancestor) const;

	GameObject* GetParent() const { return parent; }
	const std::vector<GameObject*>& GetChildren() const { return children; }

	void SetName(const std::string& n) { name = n; }
	const std::string& GetName() const { return name; }

	bool IsActive() const { return active; }
	void Enable();
	void Disable();

	// Activo y con todos sus padres activos (si un padre esta desactivado no se dibuja)
	bool IsActiveInHierarchy() const;

	glm::mat4 GetWorldMatrix() const;
	void InvalidateWorldTransform();

	// AABB en espacio mundo de su malla (invalida si no tiene). Con includeChildren = true
	// incluye las mallas de todos sus descendientes (util para encuadrar un modelo entero).
	AABB GetWorldAABB(bool includeChildren = false) const;

	// ---------------------------------------------------------------------------------
	// "Version espacial": contador global que sube cada vez que algo que afecta al octree
	// cambia (un objeto con malla se mueve, se crea, se borra, se activa o cambia de malla).
	// La escena compara este numero con el del octree para saber si debe reconstruirlo.
	// ---------------------------------------------------------------------------------
	static void MarkSpatialDirty() { ++spatialVersion; }
	static uint64_t GetSpatialVersion() { return spatialVersion; }

private:
	// Helper en tiempo de compilación para mapear Tipos C++ a índices del Array
	template<typename T>
	static constexpr ComponentType GetComponentTypeID();

	static uint64_t spatialVersion;

	GameUUID uuid;

	bool active = true;
	std::string name;

	// Array fijo indexado por enum ComponentType (COMPONENT_TRANSFORM=0, COMPONENT_MESH=1, etc.)
	// Acceso en O(1) directo sin bucles ni asignaciones dinámicas extra
	std::array<std::unique_ptr<Component>, COMPONENT_COUNT> components;

	GameObject* parent = nullptr;
	std::vector<GameObject*> children;
};

// Especializaciones del Helper para resolver tipos en tiempo de compilación
template<> constexpr ComponentType GameObject::GetComponentTypeID<TransformComponent>() { return COMPONENT_TRANSFORM; }
template<> constexpr ComponentType GameObject::GetComponentTypeID<MeshComponent>() { return COMPONENT_MESH; }
template<> constexpr ComponentType GameObject::GetComponentTypeID<MaterialComponent>() { return COMPONENT_MATERIAL; }
template<> constexpr ComponentType GameObject::GetComponentTypeID<CameraComponent>() { return COMPONENT_CAMERA; }
