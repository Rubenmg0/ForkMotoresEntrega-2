#pragma once

#include "Component.h"
#include "../../../Geometry/AABB.h"
#include "../../../ResourceManagement/Resource.h"

#include <memory>

class Primitive;
class ResourceMesh;

// =====================================================================================
// MeshComponent: indica QUE malla dibuja el GameObject. No es dueno de la malla: guarda el
// UID de un ResourceMesh y lo pide al ResourceManager (conteo de referencias). Asi, 100
// GameObjects con la misma malla comparten una unica copia en memoria.
// =====================================================================================
class MeshComponent : public Component
{
public:
	explicit MeshComponent(GameObject* owner, UID meshUID = INVALID_UID);
	~MeshComponent() override;

	// Copiar a mano romperia el conteo de referencias: se usa Clone()
	MeshComponent(const MeshComponent&) = delete;
	MeshComponent& operator=(const MeshComponent&) = delete;

	std::unique_ptr<Component> Clone(GameObject* newOwner) const override;

	// Cambia la malla: pide la nueva (+1 referencia) y suelta la anterior (-1 referencia).
	// INVALID_UID deja el componente sin malla.
	void SetMesh(UID uid);
	UID GetMeshUID() const { return meshUID; }
	const ResourceMesh* GetMeshResource() const { return mesh; }

	// Geometria lista para dibujar (nullptr si no hay malla cargada)
	const Primitive* GetPrimitive() const;

	// AABB de la malla en espacio local (invalida si no hay malla)
	AABB GetLocalAABB() const;

	// El UID guardado ya no existe (por ejemplo se borro el asset)
	bool IsMeshMissing() const { return missing; }

	// El ResourceManager ha borrado la malla: se suelta la referencia y queda como "missing"
	void MarkMeshMissing();

	// Activar/desactivar el componente cambia lo que se dibuja: el octree debe enterarse
	void Enable() override;
	void Disable() override;

	void Save(nlohmann::json& out) const override;
	void Load(const nlohmann::json& in) override;

private:
	void ReleaseMesh();

	UID meshUID = INVALID_UID;
	ResourceMesh* mesh = nullptr; // propiedad del ResourceManager
	bool missing = false;
};
