#pragma once

#include "BuildManifest.h"

#include <string>
#include <vector>

// =====================================================================================
// Ajustes de build del proyecto (ProjectSettings/BuildSettings.json), lo que edita la ventana
// File > Build Settings: escenas incluidas, Player Settings, carpeta de salida y releases.
// =====================================================================================
struct SceneInBuild
{
	std::string path;     // ruta de proyecto ("Assets/Scenes/Level1.scene")
	bool enabled = true;
};

enum class VersionBump { None = 0, Patch, Minor, Major };

struct BuildSettings
{
	static constexpr const char* FILE = "ProjectSettings/BuildSettings.json";

	std::string productName;
	std::string companyName;
	std::vector<SceneInBuild> scenes;  // la primera activa es la escena de arranque
	std::string outputDir;             // ruta de disco; "" = <Proyecto>/Builds
	bool development = true;           // configuracion de "Build" (las releases son siempre Release)
	PlayerWindowSettings window;

	// Releases
	VersionBump bumpAfterRelease = VersionBump::Patch;

	// Lee el fichero del proyecto abierto; si no existe crea unos ajustes por defecto
	// (nombre del proyecto y todas sus escenas, empezando por la ultima abierta)
	static BuildSettings LoadOrCreate();
	bool Save() const;

	std::vector<std::string> GetEnabledScenes() const;
	std::string GetOutputDir() const;          // outputDir o <Proyecto>/Builds
	std::string GetProductDir() const;         // <salida>/<Producto>
	std::string GetExecutablePath() const;     // <salida>/<Producto>/<Producto>.exe

	// Anade las escenas de Assets/ que aun no estan en la lista (desactivadas si ya habia alguna)
	void AddMissingScenes();
};

const char* VersionBumpToString(VersionBump bump);
VersionBump VersionBumpFromString(const std::string& text);
