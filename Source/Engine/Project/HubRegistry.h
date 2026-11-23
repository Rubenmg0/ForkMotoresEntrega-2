#pragma once

#include <cstdint>
#include <string>
#include <vector>

// =====================================================================================
// Registro del Hub: la lista de proyectos que conoce el usuario y sus preferencias.
//
// Se guarda por usuario (no por proyecto) en la carpeta de preferencias que da SDL:
//      Windows: %APPDATA%\Maruja\MarujaEngine\hub.json
// =====================================================================================
namespace HubRegistry
{
	struct Entry
	{
		std::string path;        // carpeta raiz del proyecto (absoluta, con '/')
		int64_t lastOpened = 0;  // segundos desde epoch (0 = nunca abierto desde el Hub)
		bool pinned = false;     // fijado arriba de la lista
	};

	// Lee hub.json. La primera vez registra el proyecto de ejemplo que acompana al motor
	// (una carpeta con Assets/ junto al ejecutable o en sus padres).
	void Load();
	bool Save();

	std::vector<Entry>& GetEntries();

	// Anade el proyecto si no estaba y actualiza su fecha de ultima apertura
	void AddOrTouch(const std::string& rootDir, bool touch = true);
	void Remove(const std::string& rootDir);
	Entry* Find(const std::string& rootDir);

	// Carpeta donde se crean los proyectos nuevos por defecto (Documentos/Maruja Projects)
	const std::string& GetDefaultProjectsDir();
	void SetDefaultProjectsDir(const std::string& dir);

	// Fichero hub.json (para mostrarlo en la ventana Acerca de / depurar)
	std::string GetRegistryFile();
}
