#include "MaterialComponent.h"
#include "../../JsonHelpers.h"
#include "../../../Engine.h"
#include "../../../Render/Texture/TextureManager.h"
#include "../../../ResourceManagement/ResourceManager.h"
#include "../../../ResourceManagement/ResourceTexture.h"
#include "../../../../Utils/Log.h"

MaterialComponent::MaterialComponent(GameObject* owner) : Component(COMPONENT_MATERIAL, owner)
{
}

MaterialComponent::~MaterialComponent()
{
	ReleaseTexture();
}

std::unique_ptr<Component> MaterialComponent::Clone(GameObject* newOwner) const
{
	auto clone = std::make_unique<MaterialComponent>(newOwner);
	clone->color = color;
	clone->useTexture = useTexture;
	clone->useCheckerboard = useCheckerboard;
	clone->SetTexture(textureUID); // el clon pide su propia referencia
	if (!active) clone->Disable();
	return clone;
}

void MaterialComponent::SetTexture(UID uid)
{
	if (uid == textureUID && !missing) return;

	ResourceManager* resources = Engine::GetInstance().resources.get();

	// Primero pedimos la nueva y despues soltamos la vieja (si fuese la misma no se descarga)
	ResourceTexture* newTexture = nullptr;
	bool newMissing = false;
	if (uid != INVALID_UID && resources != nullptr)
	{
		Resource* resource = resources->RequestResource(uid);
		if (resource != nullptr && resource->GetType() == ResourceType::Texture)
		{
			newTexture = static_cast<ResourceTexture*>(resource);
		}
		else
		{
			if (resource != nullptr) resources->ReleaseResource(uid);
			newMissing = true;
			LOG_WARNING("[Material] La textura con UID %llu no existe (asset borrado?)", (unsigned long long)uid);
		}
	}

	ReleaseTexture();

	textureUID = uid;
	texture = newTexture;
	missing = newMissing;
}

void MaterialComponent::ReleaseTexture()
{
	if (texture != nullptr)
	{
		if (ResourceManager* resources = Engine::GetInstance().resources.get())
		{
			resources->ReleaseResource(textureUID);
		}
		texture = nullptr;
	}
}

void MaterialComponent::MarkTextureMissing()
{
	ReleaseTexture();
	missing = (textureUID != INVALID_UID);
}

unsigned int MaterialComponent::GetActiveTextureID() const
{
	const TextureManager* textures = Engine::GetInstance().textures.get();

	if (useCheckerboard && textures && textures->GetCheckerboard())
	{
		return textures->GetCheckerboard()->id;
	}

	if (!useTexture || textureUID == INVALID_UID) return 0;

	// Textura borrada o que no se pudo cargar: se ve la textura "missing" (rosa/negro)
	const bool broken = missing || texture == nullptr || texture->GetGLID() == 0;
	if (broken)
	{
		return (textures && textures->GetMissingTexture()) ? textures->GetMissingTexture()->id : 0;
	}
	return texture->GetGLID();
}

void MaterialComponent::Save(nlohmann::json& out) const
{
	out["texture"] = textureUID;
	out["color"] = JsonHelpers::FromVec4(color);
	out["useTexture"] = useTexture;
	out["checkerboard"] = useCheckerboard;
	out["active"] = active;
}

void MaterialComponent::Load(const nlohmann::json& in)
{
	JsonHelpers::ReadVec4(in, "color", color);
	JsonHelpers::Read(in, "useTexture", useTexture);
	JsonHelpers::Read(in, "checkerboard", useCheckerboard);

	UID uid = INVALID_UID;
	JsonHelpers::Read(in, "texture", uid);
	SetTexture(uid);

	bool isActive = true;
	JsonHelpers::Read(in, "active", isActive);
	if (isActive) Enable();
	else Disable();
}
