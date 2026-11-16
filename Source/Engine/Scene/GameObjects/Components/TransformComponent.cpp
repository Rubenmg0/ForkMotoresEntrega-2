#include "TransformComponent.h"
#include "MeshComponent.h"
#include "../GameObject.h"
#include "../../JsonHelpers.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <cmath>

std::unique_ptr<Component> TransformComponent::Clone(GameObject* newOwner) const
{
	auto clone = std::make_unique<TransformComponent>(newOwner);
	clone->SetPosition(position);
	clone->SetRotation(rotation);
	clone->SetScale(scale);
	if (!active) clone->Disable();
	return clone;
}

const glm::mat4& TransformComponent::GetWorldMatrix() const
{
	if (worldDirty)
	{
		const GameObject* parent = owner ? owner->GetParent() : nullptr;
		worldMatrix = parent ? parent->GetWorldMatrix() * GetLocalModelMatrix() : GetLocalModelMatrix();
		worldDirty = false;
	}
	return worldMatrix;
}

bool TransformComponent::DecomposeLocalMatrix(const glm::mat4& localMatrix, glm::vec3& outPosition, glm::vec3& outRotation, glm::vec3& outScale) const
{
	// localMatrix = T * (Rx * Ry * Rz) * S. Por tanto la columna 3 es la traslacion y cada una de las
	// tres primeras columnas es un eje de la rotacion multiplicado por la escala de ese eje.
	glm::mat4 rotationOnly(1.0f);
	glm::vec3 newScale(1.0f);
	for (int axis = 0; axis < 3; ++axis)
	{
		const glm::vec3 column = glm::vec3(localMatrix[axis]);
		const float length = glm::length(column);
		if (!(length > 1e-6f)) return false; // escala nula (o NaN): de ese eje no se puede sacar la rotacion

		// La longitud siempre es positiva: si la escala actual es negativa (espejo) se mantiene su signo
		const float sign = (scale[axis] < 0.0f) ? -1.0f : 1.0f;
		newScale[axis] = length * sign;
		rotationOnly[axis] = glm::vec4(column / (length * sign), 0.0f);
	}

	// Mismo orden que GetLocalModelMatrix: Rx * Ry * Rz
	float x = 0.0f, y = 0.0f, z = 0.0f;
	glm::extractEulerAngleXYZ(rotationOnly, x, y, z);
	const glm::vec3 newRotation = glm::degrees(glm::vec3(x, y, z));
	const glm::vec3 newPosition = glm::vec3(localMatrix[3]);

	// Si la matriz traia valores no finitos no se toca nada (un NaN en el transform ya no se iria nunca)
	const auto isFinite = [](const glm::vec3& v) { return !glm::any(glm::isnan(v)) && !glm::any(glm::isinf(v)); };
	if (!isFinite(newPosition) || !isFinite(newRotation) || !isFinite(newScale)) return false;

	outPosition = newPosition;
	outRotation = newRotation;
	outScale = newScale;
	return true;
}

void TransformComponent::InvalidateWorld()
{
	// Si ya estaba sucio, por la invariante sus descendientes también lo están
	if (worldDirty) return;
	worldDirty = true;

	if (owner == nullptr) return;

	// Si el objeto tiene malla su AABB en el mundo ha cambiado: el octree debe reconstruirse
	if (owner->GetComponent<MeshComponent>() != nullptr)
	{
		GameObject::MarkSpatialDirty();
	}

	for (GameObject* child : owner->GetChildren())
	{
		child->InvalidateWorldTransform();
	}
}

void TransformComponent::LookAt(const glm::vec3& target, const glm::vec3& worldUp)
{
	const glm::vec3 forward = target - position;
	if (glm::length(forward) < 1e-6f) return;

	// Si se mira justo hacia arriba o hacia abajo, 'worldUp' es paralelo a la direccion y
	// glm::lookAt no puede construir la base: usamos otro vector "arriba"
	glm::vec3 up = worldUp;
	if (std::abs(glm::dot(glm::normalize(forward), glm::normalize(up))) > 0.999f)
	{
		up = glm::vec3(0.0f, 0.0f, -1.0f);
	}

	// glm::lookAt da la matriz de VISTA (mundo -> camara); su inversa es la orientacion del objeto
	const glm::mat4 view = glm::lookAt(position, target, up);
	const glm::mat4 orientation = glm::inverse(view);

	glm::mat4 rotationOnly(1.0f);
	rotationOnly[0] = glm::vec4(glm::normalize(glm::vec3(orientation[0])), 0.0f);
	rotationOnly[1] = glm::vec4(glm::normalize(glm::vec3(orientation[1])), 0.0f);
	rotationOnly[2] = glm::vec4(glm::normalize(glm::vec3(orientation[2])), 0.0f);

	// Mismo orden que GetLocalModelMatrix: Rx * Ry * Rz
	float x = 0.0f, y = 0.0f, z = 0.0f;
	glm::extractEulerAngleXYZ(rotationOnly, x, y, z);
	SetRotation(glm::degrees(glm::vec3(x, y, z)));
}

void TransformComponent::Save(nlohmann::json& out) const
{
	out["position"] = JsonHelpers::FromVec3(position);
	out["rotation"] = JsonHelpers::FromVec3(rotation);
	out["scale"] = JsonHelpers::FromVec3(scale);
	out["active"] = active;
}

void TransformComponent::Load(const nlohmann::json& in)
{
	glm::vec3 newPosition = position;
	glm::vec3 newRotation = rotation;
	glm::vec3 newScale = scale;
	JsonHelpers::ReadVec3(in, "position", newPosition);
	JsonHelpers::ReadVec3(in, "rotation", newRotation);
	JsonHelpers::ReadVec3(in, "scale", newScale);
	SetPosition(newPosition);
	SetRotation(newRotation);
	SetScale(newScale);

	bool isActive = true;
	JsonHelpers::Read(in, "active", isActive);
	if (isActive) Enable();
	else Disable();
}
