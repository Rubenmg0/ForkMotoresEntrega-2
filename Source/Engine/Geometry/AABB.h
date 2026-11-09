#pragma once

#include <glm/glm.hpp>
#include <array>
#include <limits>

// =====================================================================================
// AABB (Axis Aligned Bounding Box): caja alineada con los ejes X, Y, Z definida por su
// esquina minima y su esquina maxima. Es el "bounding volume" que usan todas las mallas
// para el frustum culling, el octree y la seleccion con el raton.
//
// Una AABB recien creada es "invalida" (min = +infinito, max = -infinito) para que al
// hacer Enclose() del primer punto quede exactamente ese punto.
// =====================================================================================
struct AABB
{
	glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
	glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

	AABB() = default;
	AABB(const glm::vec3& minPoint, const glm::vec3& maxPoint) : min(minPoint), max(maxPoint) {}

	bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

	void SetInvalid()
	{
		min = glm::vec3(std::numeric_limits<float>::max());
		max = glm::vec3(std::numeric_limits<float>::lowest());
	}

	// Amplia la caja para que contenga el punto / la otra caja
	void Enclose(const glm::vec3& point)
	{
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	void Enclose(const AABB& other)
	{
		if (!other.IsValid()) return;
		Enclose(other.min);
		Enclose(other.max);
	}

	glm::vec3 GetCenter() const { return (min + max) * 0.5f; }
	glm::vec3 GetSize() const { return max - min; }

	// Las 8 esquinas. Orden: bit 0 -> X, bit 1 -> Y, bit 2 -> Z (0 = min, 1 = max)
	std::array<glm::vec3, 8> GetCorners() const
	{
		std::array<glm::vec3, 8> corners;
		for (int i = 0; i < 8; ++i)
		{
			corners[i] = glm::vec3((i & 1) ? max.x : min.x,
				(i & 2) ? max.y : min.y,
				(i & 4) ? max.z : min.z);
		}
		return corners;
	}

	// Caja (alineada con los ejes del mundo) que encierra a esta caja transformada por una
	// matriz afin (posicion/rotacion/escala). Metodo de Jim Arvo: en vez de transformar las 8
	// esquinas, se acumula por ejes el minimo y el maximo de cada termino de la matriz.
	AABB Transformed(const glm::mat4& matrix) const
	{
		if (!IsValid()) return AABB();

		glm::vec3 newMin(matrix[3]); // empezamos por la traslacion
		glm::vec3 newMax(matrix[3]);
		for (int column = 0; column < 3; ++column)     // eje de origen (x, y, z local)
		{
			for (int row = 0; row < 3; ++row)          // eje de destino (x, y, z mundo)
			{
				const float a = matrix[column][row] * min[column];
				const float b = matrix[column][row] * max[column];
				newMin[row] += (a < b) ? a : b;
				newMax[row] += (a < b) ? b : a;
			}
		}
		return AABB(newMin, newMax);
	}

	// true si 'other' esta completamente dentro de esta caja
	bool Contains(const AABB& other) const
	{
		return other.min.x >= min.x && other.max.x <= max.x &&
			other.min.y >= min.y && other.max.y <= max.y &&
			other.min.z >= min.z && other.max.z <= max.z;
	}

	bool Intersects(const AABB& other) const
	{
		return min.x <= other.max.x && max.x >= other.min.x &&
			min.y <= other.max.y && max.y >= other.min.y &&
			min.z <= other.max.z && max.z >= other.min.z;
	}
};
