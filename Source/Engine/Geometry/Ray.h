#pragma once

#include "AABB.h"
#include <glm/glm.hpp>

// =====================================================================================
// Ray (rayo): origen + direccion normalizada. Se usa para seleccionar GameObjects con el
// raton (mouse picking). Incluye los dos tests de interseccion que necesitamos:
//   - Rayo vs AABB      (test rapido para descartar objetos / nodos del octree)
//   - Rayo vs Triangulo (test exacto contra la geometria de la malla)
// =====================================================================================
struct Ray
{
	glm::vec3 origin = glm::vec3(0.0f);
	glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // siempre normalizada

	Ray() = default;
	Ray(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
		: origin(rayOrigin), direction(glm::normalize(rayDirection)) {}

	glm::vec3 GetPoint(float distance) const { return origin + direction * distance; }
};

namespace Intersection
{
	// Metodo de los "slabs": el rayo atraviesa la caja si los intervalos de entrada/salida
	// en los tres ejes se solapan. 'outDistance' = distancia de entrada (0 si el origen
	// ya esta dentro de la caja).
	inline bool RayAABB(const Ray& ray, const AABB& box, float& outDistance)
	{
		if (!box.IsValid()) return false;

		float tMin = 0.0f;
		float tMax = 3.402823466e+38F; // FLT_MAX

		for (int axis = 0; axis < 3; ++axis)
		{
			const float origin = ray.origin[axis];
			const float direction = ray.direction[axis];

			if (direction > -1e-8f && direction < 1e-8f)
			{
				// Rayo paralelo a este par de planos: solo puede tocar la caja si el origen esta entre ellos
				if (origin < box.min[axis] || origin > box.max[axis]) return false;
				continue;
			}

			const float inverse = 1.0f / direction;
			float t1 = (box.min[axis] - origin) * inverse;
			float t2 = (box.max[axis] - origin) * inverse;
			if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }

			if (t1 > tMin) tMin = t1;
			if (t2 < tMax) tMax = t2;
			if (tMin > tMax) return false;
		}

		outDistance = tMin;
		return true;
	}

	// Algoritmo de Moller-Trumbore. 'outDistance' = distancia a lo largo del rayo hasta el impacto.
	inline bool RayTriangle(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& outDistance)
	{
		const float EPSILON = 1e-7f;
		const glm::vec3 edge1 = v1 - v0;
		const glm::vec3 edge2 = v2 - v0;
		const glm::vec3 h = glm::cross(ray.direction, edge2);
		const float a = glm::dot(edge1, h);

		// Si 'a' es casi 0 el rayo es paralelo al triangulo
		if (a > -EPSILON && a < EPSILON) return false;

		const float f = 1.0f / a;
		const glm::vec3 s = ray.origin - v0;
		const float u = f * glm::dot(s, h);
		if (u < 0.0f || u > 1.0f) return false;

		const glm::vec3 q = glm::cross(s, edge1);
		const float v = f * glm::dot(ray.direction, q);
		if (v < 0.0f || u + v > 1.0f) return false;

		const float t = f * glm::dot(edge2, q);
		if (t <= EPSILON) return false;

		outDistance = t;
		return true;
	}
}
