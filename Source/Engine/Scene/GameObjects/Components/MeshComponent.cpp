#include "MeshComponent.h"
#include "../GameObject.h"
#include "../../JsonHelpers.h"
#include "../../../Engine.h"
#include "../../../ResourceManagement/ResourceManager.h"
#include "../../../ResourceManagement/ResourceMesh.h"
#include "../../../../Utils/Log.h"

MeshComponent::MeshComponent(GameObject* owner, UID meshUID) : Component(COMPONENT_MESH, owner)
{
	SetMesh(meshUID);
}

MeshComponent::~MeshComponent()
{
	ReleaseMesh();
}

std::unique_ptr<Component> MeshComponent::Clone(GameObject* newOwner) const
{
	// El clon pide su propia referencia a la misma malla (la cuenta sube en 1)
	auto clone = std::make_unique<MeshComponent>(newOwner, missing ? INVALID_UID : meshUID);
	if (!active) clone->Disable();
	return clone;
}

void MeshComponent::SetMesh(UID uid)
{
	if (uid == meshUID && !missing) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();

	// 1) Pedimos la nueva malla ANTES de soltar la vieja (si fuese la misma, no se descarga)
	ResourceMesh* newMesh = nullptr;
	bool newMissing = false;
	if (uid != INVALID_UID && resources != nullptr)
	{
		Resource* resource = resources->RequestResource(uid);
		if (resource != nullptr && resource->GetType() == ResourceType::Mesh)
		{
			newMesh = static_cast<ResourceMesh*>(resource);
		}
		else
		{
			if (resource != nullptr) resources->ReleaseResource(uid); // existe pero no es una malla
			newMissing = true;
			LOG_WARNING("[Mesh] La malla con UID %llu no existe (asset borrado?)", (unsigned long long)uid);
		}
	}

	// 2) Soltamos la anterior
	ReleaseMesh();

	meshUID = uid;
	mesh = newMesh;
	missing = newMissing;

	// La AABB del objeto ha cambiado
	GameObject::MarkSpatialDirty();
}

void MeshComponent::ReleaseMesh()
{
	if (mesh != nullptr)
	{
		if (ResourceManager* resources = Engine::GetInstance().resources.get())
		{
			resources->ReleaseResource(meshUID);
		}
		mesh = nullptr;
	}
}

void MeshComponent::MarkMeshMissing()
{
	ReleaseMesh();
	missing = (meshUID != INVALID_UID);
	GameObject::MarkSpatialDirty();
}

const Primitive* MeshComponent::GetPrimitive() const
{
	return mesh ? mesh->GetPrimitive() : nullptr;
}

AABB MeshComponent::GetLocalAABB() const
{
	return mesh ? mesh->GetLocalAABB() : AABB();
}

void MeshComponent::Enable()
{
	Component::Enable();
	GameObject::MarkSpatialDirty();
}

void MeshComponent::Disable()
{
	Component::Disable();
	GameObject::MarkSpatialDirty();
}

void MeshComponent::Save(nlohmann::json& out) const
{
	out["mesh"] = meshUID;
	out["active"] = active;
}

void MeshComponent::Load(const nlohmann::json& in)
{
	UID uid = INVALID_UID;
	JsonHelpers::Read(in, "mesh", uid);
	SetMesh(uid);

	bool isActive = true;
	JsonHelpers::Read(in, "active", isActive);
	if (isActive) Enable();
	else Disable();
}
