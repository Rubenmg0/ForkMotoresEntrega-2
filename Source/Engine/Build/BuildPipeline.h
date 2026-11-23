#pragma once

#include "BuildSettings.h"

#include <cstdint>
#include <string>
#include <vector>

// =====================================================================================
// BuildPipeline: genera la build de un juego a partir del proyecto abierto.
//
//  1. Valida       escenas en la build, nombre del producto, carpeta de salida
//  2. Prepara      <salida>/<Producto>/ (solo borra una build anterior, nunca otra carpeta)
//  3. Player       copia el ejecutable del motor como <Producto>.exe + sus DLL
//  4. Cocina       copia las escenas y SOLO los ficheros de Library que usan (mallas y texturas)
//  5. Manifiesto   build.json + resources.json en <Producto>_Data/
//  6. Informe      ficheros, tamano, tiempo, avisos y errores (+ <salida>/Logs/build-*.log)
//
// Los Assets originales (FBX, PNG...) nunca se incluyen: el juego solo lleva el formato propio.
// =====================================================================================
struct BuildReport
{
	bool success = false;
	bool development = false;
	std::string productName;
	std::string version;
	std::string buildId;
	std::string productDir;
	std::string executablePath;
	std::string logPath;
	int64_t timestamp = 0;
	double seconds = 0.0;
	int sceneCount = 0;
	int resourceCount = 0;
	int fileCount = 0;
	uint64_t totalBytes = 0;
	std::vector<std::string> steps;     // lineas del informe, en orden
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
};

namespace BuildPipeline
{
	// development: Development Build (overlay de estadisticas) o Release
	BuildReport Build(const BuildSettings& settings, bool development);

	// Borra <salida>/<Producto> si es una build (tiene <Producto>_Data/build.json)
	bool CleanBuild(const BuildSettings& settings, std::string* outError = nullptr);

	// "12.4 MB"
	std::string FormatBytes(uint64_t bytes);
}
