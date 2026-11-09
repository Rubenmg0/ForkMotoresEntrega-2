#pragma once

#include "../../Render/Primitives/Primitive.h"

#include <memory>
#include <string>
#include <vector>

// =====================================================================================
// MeshImporter: guarda y carga mallas en NUESTRO FORMATO PROPIO ".mesh".
//
// FORMATO .mesh (binario):
//   1) Cabecera de 40 bytes:
//        char[4]  "MMSH"        -> identificador del formato
//        uint32   version
//        uint32   numero de vertices
//        uint32   numero de indices
//        float[3] AABB minima, float[3] AABB maxima (espacio local)
//   2) Vertices: por cada vertice 8 floats -> posicion (3), normal (3), UV (2)
//   3) Indices: uint32 (3 por triangulo)
//
// Cargar un .mesh es casi un memcpy: no hace falta Assimp ni ningun FBX para ejecutar.
// =====================================================================================
class MeshImporter
{
public:
	static bool Save(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::string& libraryPath);

	// Lee el .mesh y crea el Primitive (sube los buffers a la GPU). nullptr si falla.
	static std::unique_ptr<Primitive> Load(const std::string& libraryPath);
};
