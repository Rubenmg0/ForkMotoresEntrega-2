#pragma once

#include <cstdint>
#include <string>

// Identificador unico de un recurso (64 bits aleatorios). 0 = "ninguno".
using UID = uint64_t;
constexpr UID INVALID_UID = 0;

enum class ResourceType
{
	Unknown = 0,
	Texture,
	Mesh,
	Model
};

const char* ResourceTypeToString(ResourceType type);
ResourceType ResourceTypeFromString(const std::string& text);

// Genera un UID aleatorio nuevo (nunca devuelve 0 ni los UID reservados de las primitivas)
UID GenerateUID();

// Ruta en Library del fichero propio de un recurso. Ej: (Mesh, 123) -> "Library/Meshes/123.mesh"
std::string GetLibraryPathFor(ResourceType type, UID uid);

// =====================================================================================
// Resource: clase base de todo lo que el motor carga desde la carpeta Library
// (texturas, mallas y modelos).
//
// CONTEO DE REFERENCIAS:
//   - Cada componente que usa un recurso (MeshComponent, MaterialComponent) lo pide al
//     ResourceManager con RequestResource(uid) -> la cuenta sube en 1.
//   - Cuando deja de usarlo llama a ReleaseResource(uid) -> la cuenta baja en 1.
//   - El recurso se carga en memoria (RAM/GPU) cuando la cuenta pasa de 0 a 1 y se
//     descarga cuando vuelve a 0. Por eso una textura usada por 50 GameObjects solo
//     esta UNA vez en memoria.
//
// Solo el ResourceManager puede cambiar la cuenta y cargar/descargar (es 'friend').
// =====================================================================================
class Resource
{
	friend class ResourceManager;

public:
	Resource(UID uid, ResourceType type) : uid(uid), type(type) {}
	virtual ~Resource() = default;

	// Los recursos se gestionan siempre por puntero desde el ResourceManager: no se copian
	Resource(const Resource&) = delete;
	Resource& operator=(const Resource&) = delete;

	UID GetUID() const { return uid; }
	ResourceType GetType() const { return type; }
	const char* GetTypeName() const { return ResourceTypeToString(type); }

	const std::string& GetName() const { return name; }
	const std::string& GetAssetPath() const { return assetPath; }     // fichero original en Assets/
	const std::string& GetLibraryPath() const { return libraryPath; } // fichero propio en Library/

	unsigned int GetReferenceCount() const { return referenceCount; }
	bool IsLoaded() const { return loaded; }
	bool IsBuiltIn() const { return builtIn; } // recursos internos del motor (Cube, Sphere...)

	// Memoria aproximada que ocupa el recurso cargado (para mostrarla en el editor)
	virtual size_t GetMemoryUsage() const { return 0; }

protected:
	// Implementadas por cada tipo de recurso
	virtual bool LoadInMemory() = 0;
	virtual void UnloadFromMemory() = 0;

	UID uid = INVALID_UID;
	ResourceType type = ResourceType::Unknown;
	std::string name;
	std::string assetPath;
	std::string libraryPath;

	unsigned int referenceCount = 0;
	bool loaded = false;
	bool builtIn = false;
};
