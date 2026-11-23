#pragma once

#include "BuildPipeline.h"

#include <cstdint>
#include <string>
#include <vector>

// =====================================================================================
// Releases: una build Release del juego, empaquetada y versionada para distribuirla.
//
//  1. Comprueba que la version del proyecto no se ha publicado ya
//  2. Hace una build Release (sin overlay de desarrollo)
//  3. Anade RELEASE_NOTES.txt a la build y la comprime: <salida>/Releases/<Producto>-<version>-win64.zip
//  4. Calcula el SHA-256 del zip y lo escribe en <zip>.sha256 (formato de sha256sum)
//  5. Apunta la release en ProjectSettings/ReleaseHistory.json y en CHANGELOG.md del proyecto
//  6. Sube la version del proyecto (patch / minor / major) para el siguiente desarrollo
// =====================================================================================
struct ReleaseEntry
{
	std::string version;
	std::string productName;
	int64_t date = 0;
	std::string notes;
	std::string archive;       // ruta del zip
	std::string sha256;
	uint64_t size = 0;
	std::string buildId;
	std::string engineVersion;
};

struct ReleaseReport
{
	bool success = false;
	BuildReport build;
	ReleaseEntry entry;
	std::string checksumPath;
	std::string nextVersion;   // version del proyecto tras la release
	std::vector<std::string> steps;
	std::vector<std::string> errors;
};

namespace ReleaseManager
{
	static constexpr const char* HISTORY_FILE = "ProjectSettings/ReleaseHistory.json";
	static constexpr const char* CHANGELOG_FILE = "CHANGELOG.md";

	ReleaseReport CreateRelease(const BuildSettings& settings, const std::string& notes, VersionBump bumpAfter);

	// Releases publicadas, de la mas nueva a la mas antigua
	std::vector<ReleaseEntry> LoadHistory();
	bool IsVersionReleased(const std::string& version);

	// "<Producto>-<version>-win64.zip" (espacios -> '-')
	std::string GetArchiveName(const std::string& productName, const std::string& version);
}
