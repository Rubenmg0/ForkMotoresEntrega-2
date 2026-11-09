#pragma once

#include "AABB.h"
#include <glm/glm.hpp>
#include <array>

// Plano en forma normal: dot(normal, p) + distance = 0. La normal apunta HACIA DENTRO del frustum.
struct Plane
{
	glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
	float distance = 0.0f;

	// > 0 delante del plano (dentro), < 0 detras (fuera)
	float SignedDistance(const glm::vec3& point) const { return glm::dot(normal, point) + distance; }
};

enum class FrustumTestResult
{
	Outside,   // la caja esta completamente fuera -> se descarta (culling)
	Intersect, // la caja corta algun plano -> visible en parte
	Inside     // la caja esta completamente dentro
};

// =====================================================================================
// Frustum: piramide truncada que representa lo que ve una camara. Se construye a partir
// de la matriz Proyeccion * Vista (metodo de Gribb & Hartmann: cada plano sale de sumar o
// restar filas de la matriz). Sirve para:
//   - Frustum culling: no dibujar las mallas cuya AABB esta fuera de los 6 planos.
//   - Dibujar el frustum de la camara de juego en la vista Scene (sus 8 esquinas).
// =====================================================================================
class Frustum
{
public:
	Frustum() = default;
	explicit Frustum(const glm::mat4& viewProjection) { Set(viewProjection); }

	void Set(const glm::mat4& viewProjection);

	FrustumTestResult Test(const AABB& box) const;
	bool IsVisible(const AABB& box) const { return Test(box) != FrustumTestResult::Outside; }

	// Esquinas en espacio mundo. Orden: bit 0 -> X (izq/der), bit 1 -> Y (abajo/arriba),
	// bit 2 -> Z (0 = plano cercano, 1 = plano lejano). Mismo orden que AABB::GetCorners().
	const std::array<glm::vec3, 8>& GetCorners() const { return corners; }

	bool IsValid() const { return valid; }

private:
	std::array<Plane, 6> planes{}; // izquierdo, derecho, inferior, superior, cercano, lejano
	std::array<glm::vec3, 8> corners{};
	bool valid = false;
};
