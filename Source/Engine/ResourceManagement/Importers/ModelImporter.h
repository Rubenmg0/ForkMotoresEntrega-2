#pragma once

#include "../ImportSettings.h"
#include "../Resource.h"
#include "../ResourceModel.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// =====================================================================================
// ModelImporter: importa modelos 3D (FBX, OBJ, DAE...) con Assimp y los convierte a
// nuestros formatos propios:
//   - Un fichero .mesh por cada malla del modelo   (Library/Meshes/<UID>.mesh)
//   - Un fichero .model con la jerarquia de nodos  (Library/Models/<UID>.model, JSON)
//
// FORMATO .model (JSON):
//   { "version": 1, "source": "Assets/...", "nodes": [
//       { "name": "...", "parent": -1, "position": [x,y,z], "rotation": [x,y,z],
//         "scale": [x,y,z], "mesh": UID, "texture": UID, "color": [r,g,b,a], "camera": false }, ... ] }
//   Los nodos estan en orden "padre antes que hijos" (el primero es la raiz).
//
// Despues de importar, el motor ya no necesita el FBX para nada: instanciar el modelo solo
// lee el .model y los .mesh / .tex que referencia.
// =====================================================================================
class ModelImporter
{
public:
	// Malla del modelo. Se guarda en el .meta para que al reimportar cada malla conserve su UID
	struct MeshEntry
	{
		int index = -1;          // indice de la malla dentro del fichero (orden de Assimp)
		UID uid = INVALID_UID;
		std::string name;
	};

	// Recibe la ruta de textura escrita en un material (p.ej. "..\textures\a.tga") y devuelve
	// el UID de esa textura ya importada (0 si no se encuentra)
	using TextureResolver = std::function<UID(const std::string& texturePathInFile)>;

	// Recibe una textura que venia DENTRO del modelo (bytes de un png/jpg/tga) y devuelve su UID
	using EmbeddedTextureHandler = std::function<UID(const std::string& fileName, const void* data, size_t size)>;

	// .fbx .obj .dae .3ds .gltf .glb
	static bool IsSupportedExtension(const std::string& extension);

	// 'inOutMeshes' entra con las mallas que ya conocia el .meta y sale con la lista actualizada
	static bool Import(const std::string& assetPath, const ModelImportSettings& settings, const std::string& modelLibraryPath,
		std::vector<MeshEntry>& inOutMeshes, const TextureResolver& resolveTexture, const EmbeddedTextureHandler& handleEmbeddedTexture);

	// Lee un fichero .model
	static bool LoadModelFile(const std::string& libraryPath, std::vector<ModelNode>& outNodes);

	// Para modelos arrastrados desde FUERA del proyecto: rutas (UTF-8) de las texturas que el modelo
	// usa y que estan en la misma carpeta, para copiarlas tambien a Assets junto al modelo.
	static std::vector<std::string> FindExternalTextureFiles(const std::string& modelDiskPathUtf8);
};
