#pragma once

#include <cstdint>
#include <string>
#include <vector>

// =====================================================================================
// FileSystem: utilidades de ficheros del motor.
//
// IDEA CLAVE: dentro del motor todas las rutas son "rutas de proyecto", es decir,
// relativas a la carpeta raiz del proyecto (la que contiene Assets/ y Library/) y
// siempre con '/' como separador. Ejemplos:
//      "Assets/Street/StreetEnvironment.fbx"
//      "Library/Meshes/123456789.mesh"
//
// Asi los ficheros .meta y las escenas guardadas no dependen de en que carpeta del disco
// este el proyecto. Cuando hay que abrir un fichero de verdad se usa ToDiskPath(), que
// antepone la carpeta raiz encontrada al arrancar (normalmente ".." al ejecutar desde
// Visual Studio, o "." si el .exe esta junto a la carpeta Assets en la build final).
// =====================================================================================
namespace FileSystem
{
	// Carpetas fijas del proyecto (rutas de proyecto)
	constexpr const char* ASSETS_DIR = "Assets";
	constexpr const char* SCENES_DIR = "Assets/Scenes";
	constexpr const char* LIBRARY_DIR = "Library";
	constexpr const char* LIBRARY_MESHES_DIR = "Library/Meshes";
	constexpr const char* LIBRARY_TEXTURES_DIR = "Library/Textures";
	constexpr const char* LIBRARY_MODELS_DIR = "Library/Models";

	// Recursos propios del editor (fuentes .ttf de la interfaz). Estan fuera de Assets/ a proposito:
	// no son assets del juego, asi que ni se importan a Library ni aparecen en la ventana Assets
	constexpr const char* EDITOR_FONTS_DIR = "EditorResources/Fonts";
	constexpr const char* ENGINE_RESOURCES_DIR = "EditorResources";

	// Configuracion del proyecto (fichero de proyecto, layout del editor...). Fuera de Assets/
	constexpr const char* PROJECT_SETTINGS_DIR = "ProjectSettings";
	constexpr const char* PROJECT_FILE = "ProjectSettings/project.json";

	// Extensiones de los ficheros propios del motor
	constexpr const char* META_EXTENSION = ".meta";
	constexpr const char* MESH_EXTENSION = ".mesh";
	constexpr const char* TEXTURE_EXTENSION = ".tex";
	constexpr const char* MODEL_EXTENSION = ".model";
	constexpr const char* SCENE_EXTENSION = ".scene";

	// Busca la carpeta Assets (junto al directorio de trabajo o junto al ejecutable) y crea
	// las carpetas de Library si no existen. Debe llamarse una vez al arrancar el motor.
	// Si antes se llamo a SetProjectRoot() (el Hub eligio un proyecto) se usa esa carpeta.
	bool Init();

	// Fija la carpeta raiz del proyecto (ruta de disco UTF-8). Llamar antes de Init().
	void SetProjectRoot(const std::string& diskPathUtf8);

	// Carpeta de disco con los recursos del motor (EditorResources/), buscada junto al ejecutable
	// y en sus carpetas padre. No depende del proyecto abierto. "" si no se encuentra.
	const std::string& GetEngineResourcesDir();

	// Carpeta del ejecutable en UTF-8 (con '/' final), "" si no se conoce
	std::string GetExecutableDir();

	// Carpeta raiz del proyecto tal y como se usa para abrir ficheros ("..", "." o absoluta)
	const std::string& GetProjectRoot();

	// Convierte una ruta de proyecto ("Assets/x.png") en una ruta usable para abrir el fichero
	std::string ToDiskPath(const std::string& projectPath);

	// Convierte una ruta de disco en UTF-8 (por ejemplo un fichero arrastrado desde el explorador
	// de Windows, SDL las da en UTF-8) a ruta de proyecto. "" si no esta dentro del proyecto.
	std::string ToProjectPath(const std::string& diskPathUtf8);

	// ---------------------------------------------------------------------------------
	// Consultas y operaciones sobre rutas de PROYECTO
	// ---------------------------------------------------------------------------------
	bool Exists(const std::string& projectPath);
	bool IsDirectory(const std::string& projectPath);
	bool CreateDirectories(const std::string& projectPath);
	bool RemoveFile(const std::string& projectPath);
	bool RemoveFolder(const std::string& projectPath); // borra la carpeta y todo su contenido
	bool Rename(const std::string& fromProjectPath, const std::string& toProjectPath);

	// Copia un fichero de cualquier parte del disco (ruta UTF-8) a una ruta de proyecto (sobrescribe)
	bool CopyExternalFile(const std::string& diskPathUtf8, const std::string& projectPath);

	bool ReadBinary(const std::string& projectPath, std::vector<char>& outData);
	bool WriteBinary(const std::string& projectPath, const void* data, size_t size);
	bool ReadText(const std::string& projectPath, std::string& outText);
	bool WriteText(const std::string& projectPath, const std::string& text);

	// Tamano en bytes y fecha de ultima modificacion (valor opaco, solo sirve para comparar)
	bool GetFileInfo(const std::string& projectPath, uint64_t& outSize, int64_t& outLastWrite);

	// Entrada de una carpeta (para el explorador de Assets)
	struct DirectoryEntry
	{
		std::string name;        // "StreetEnvironment.fbx"
		std::string path;        // "Assets/Street/StreetEnvironment.fbx"
		bool isDirectory = false;
	};

	// Lista el contenido de una carpeta (primero carpetas y luego ficheros, por orden alfabetico)
	std::vector<DirectoryEntry> ListDirectory(const std::string& projectPath);

	// Lista recursivamente todos los ficheros (no carpetas) que cuelgan de una carpeta
	void ListFilesRecursive(const std::string& projectPath, std::vector<std::string>& outFiles);

	// ---------------------------------------------------------------------------------
	// Utilidades de texto para rutas (no tocan el disco)
	// ---------------------------------------------------------------------------------
	std::string Normalize(const std::string& path);          // '\' -> '/', sin '/' final
	std::string Join(const std::string& a, const std::string& b);
	std::string GetFileName(const std::string& path);        // "a/b/c.fbx" -> "c.fbx"
	std::string GetFileNameNoExtension(const std::string& path); // "a/b/c.fbx" -> "c"
	std::string GetExtensionLower(const std::string& path);  // "a/b/c.FBX" -> ".fbx"
	std::string GetParentPath(const std::string& path);      // "a/b/c.fbx" -> "a/b"
	std::string ToLower(std::string text);
	bool IsInsideFolder(const std::string& path, const std::string& folder);

	// Si la ruta ya existe devuelve una alternativa libre: "x.png" -> "x (1).png"
	std::string MakeUniquePath(const std::string& projectPath);
}
