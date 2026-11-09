#pragma once

#include "Resource.h"
#include "../Geometry/AABB.h"
#include "../Render/Primitives/GeometryGenerator.h"

#include <memory>

// =====================================================================================
// ResourceMesh: malla (vertices + indices). En disco vive en Library/Meshes/<UID>.mesh
// (formato propio, ver MeshImporter). Cuando esta cargada guarda un Primitive, que tiene
// los buffers de OpenGL (VAO/VBO/EBO) y una copia en RAM para el picking y las normales.
//
// Las primitivas basicas (Cube, Sphere, Plane...) tambien son ResourceMesh, pero "built-in":
// no tienen fichero, se generan por codigo con GeometryGenerator al cargarse.
// =====================================================================================
class ResourceMesh : public Resource
{
public:
	explicit ResourceMesh(UID uid) : Resource(uid, ResourceType::Mesh) {}
	~ResourceMesh() override = default;

	// nullptr si la malla no esta cargada
	const Primitive* GetPrimitive() const { return primitive.get(); }

	// AABB en espacio local de la malla (invalida si no esta cargada)
	AABB GetLocalAABB() const;

	size_t GetVertexCount() const { return primitive ? primitive->GetVertexCount() : 0; }
	size_t GetTriangleCount() const { return primitive ? primitive->GetTriangleCount() : 0; }

	size_t GetMemoryUsage() const override;

	// Lo usa el ResourceManager al registrar las primitivas internas del motor
	void SetBuiltInPrimitive(PrimitiveType type) { builtInType = type; builtIn = true; }

protected:
	bool LoadInMemory() override;
	void UnloadFromMemory() override;

private:
	std::unique_ptr<Primitive> primitive;
	PrimitiveType builtInType = PrimitiveType::Cube;
};
