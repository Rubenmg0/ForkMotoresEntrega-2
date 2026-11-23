#pragma once

#include <cstdint>
#include <string>

// =====================================================================================
// Proyecto del motor.
//
// Un proyecto es una carpeta con esta estructura:
//      <Proyecto>/
//          Assets/                     assets del juego (+ sus .meta)
//          Assets/Scenes/              escenas (.scene)
//          Library/                    cache de importacion (se regenera, no se sube a git)
//          ProjectSettings/
//              project.json            ESTE fichero: nombre, version del proyecto, version del motor...
//              EditorLayout.ini        disposicion de las ventanas del editor
//
// El Hub lee y escribe project.json para listar los proyectos y gestionar su version; el editor
// lo carga al abrir el proyecto (Project::SetActive) y lo actualiza al guardar escenas.
// =====================================================================================

// Version semantica MAJOR.MINOR.PATCH (la "version del proyecto" que gestiona el usuario)
struct SemVer
{
	int major = 0;
	int minor = 1;
	int patch = 0;

	static bool Parse(const std::string& text, SemVer& out);
	std::string ToString() const;

	void BumpMajor() { ++major; minor = 0; patch = 0; }
	void BumpMinor() { ++minor; patch = 0; }
	void BumpPatch() { ++patch; }

	bool operator==(const SemVer& o) const { return major == o.major && minor == o.minor && patch == o.patch; }
	bool operator!=(const SemVer& o) const { return !(*this == o); }
};

struct ProjectInfo
{
	static constexpr int FILE_VERSION = 1;

	std::string name;
	std::string description;
	SemVer version;                 // version del proyecto
	std::string engineVersion;      // version del motor con la que se abrio por ultima vez
	std::string createdWithEngine;  // version del motor con la que se creo
	int64_t createdAt = 0;          // segundos desde epoch
	int64_t lastOpenedAt = 0;
	int64_t lastModifiedAt = 0;     // ultima vez que se guardo una escena / los ajustes
	std::string lastScene;          // ruta de proyecto de la ultima escena abierta/guardada

	// Lee / escribe <rootDir>/ProjectSettings/project.json (rootDir: ruta de disco UTF-8)
	bool Load(const std::string& rootDir);
	bool Save(const std::string& rootDir) const;
};

namespace Project
{
	// Momento actual en segundos desde epoch
	int64_t Now();

	// "2026-11-23 14:05"
	std::string FormatDate(int64_t timestamp);

	// "5 minutes ago", "Yesterday", "3 days ago"... (la interfaz del editor esta en ingles)
	std::string FormatRelative(int64_t timestamp);

	// Ruta absoluta y normalizada con '/' (para comparar rutas de proyectos)
	std::string NormalizeRoot(const std::string& diskPathUtf8);

	// true si la carpeta existe
	bool FolderExists(const std::string& diskPathUtf8);

	// true si la carpeta tiene ProjectSettings/project.json o, al menos, una carpeta Assets/
	bool LooksLikeProject(const std::string& rootDir);

	// Carga el project.json; si no existe (carpeta con Assets/ de antes del Hub) genera uno nuevo
	// con el nombre de la carpeta y lo guarda. false si la carpeta no es un proyecto.
	bool LoadOrAdopt(const std::string& rootDir, ProjectInfo& out);

	// Nombre valido para una carpeta de proyecto (sin \ / : * ? " < > |, no vacio)
	bool IsValidProjectName(const std::string& name, std::string* outError = nullptr);

	// Crea <parentDir>/<name>/ con Assets/, Assets/Scenes/, ProjectSettings/project.json y un
	// .gitignore que excluye Library/. Devuelve la carpeta creada en outRoot.
	bool CreateNewProject(const std::string& parentDir, const std::string& name, const std::string& description,
		std::string& outRoot, std::string* outError = nullptr);

	// Proyecto abierto en el editor
	void SetActive(const std::string& rootDir, const ProjectInfo& info);
	bool HasActive();
	const std::string& GetActiveRoot();
	ProjectInfo& GetActive();
	bool SaveActive(); // guarda el project.json del proyecto abierto
}
