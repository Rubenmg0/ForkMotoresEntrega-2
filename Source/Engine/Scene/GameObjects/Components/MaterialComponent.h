#pragma once

#include "Component.h"
#include "../../../ResourceManagement/Resource.h"

#include <glm/glm.hpp>
#include <memory>

class GameObject;
class ResourceTexture;

// =====================================================================================
// MaterialComponent: color + textura difusa del GameObject.
// Igual que MeshComponent, NO es dueno de la textura: guarda el UID de un ResourceTexture y
// lo pide/suelta al ResourceManager (conteo de referencias).
//
// Texturas especiales (las crea el TextureManager):
//   - Checkerboard: cuadros blanco/negro, para revisar las UV (opcion del Inspector).
//   - Missing: cuadros rosa/negro, se usa si la textura asignada ya no existe.
// =====================================================================================
class MaterialComponent : public Component
{
public:
	explicit MaterialComponent(GameObject* owner);
	~MaterialComponent() override;

	MaterialComponent(const MaterialComponent&) = delete;
	MaterialComponent& operator=(const MaterialComponent&) = delete;

	std::unique_ptr<Component> Clone(GameObject* newOwner) const override;

	const glm::vec4& GetColor() const { return color; }
	void SetColor(const glm::vec4& newColor) { color = newColor; }

	bool GetUseTexture() const { return useTexture; }
	void SetUseTexture(bool enabled) { useTexture = enabled; }

	// Cambia la textura difusa: pide la nueva (+1 referencia) y suelta la anterior (-1).
	// INVALID_UID = sin textura.
	void SetTexture(UID uid);
	UID GetTextureUID() const { return textureUID; }
	const ResourceTexture* GetTexture() const { return texture; } // nullptr si no hay

	// La textura asignada ya no existe (asset borrado) -> se dibuja la textura "missing"
	bool IsTextureMissing() const { return missing; }

	// El ResourceManager ha borrado la textura: se suelta la referencia y queda como "missing"
	void MarkTextureMissing();

	bool HasTransparentAlpha() const { return color.a < 0.99f; }

	// Alterna la checkerboard encima de la textura original, sin descartarla
	bool GetUseCheckerboard() const { return useCheckerboard; }
	void SetUseCheckerboard(bool enabled) { useCheckerboard = enabled; }

	// ID de OpenGL que hay que usar al dibujar (0 = sin textura)
	unsigned int GetActiveTextureID() const;

	void Save(nlohmann::json& out) const override;
	void Load(const nlohmann::json& in) override;

private:
	void ReleaseTexture();

	UID textureUID = INVALID_UID;
	ResourceTexture* texture = nullptr; // propiedad del ResourceManager
	glm::vec4 color = glm::vec4(1.0f);
	bool useTexture = true;
	bool useCheckerboard = false;
	bool missing = false;
};
