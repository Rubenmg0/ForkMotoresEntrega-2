#pragma once

#include <string>
#include <vector>

// =====================================================================================
// Funciones que dependen del sistema operativo, compartidas por el editor y el Hub.
// La implementacion esta en EditorPlatform.cpp para que <windows.h> no se mezcle con el resto.
// =====================================================================================
namespace EditorPlatform
{
	// Abre una carpeta en el Explorador o, si es un fichero, su carpeta con el fichero seleccionado.
	// diskPathUtf8: ruta de disco (absoluta o relativa al directorio de trabajo) en UTF-8.
	void RevealInExplorer(const std::string& diskPathUtf8);

	// Lanza otra instancia del motor (el ejecutable actual) con estos argumentos.
	// Sin argumentos arranca el Hub. Devuelve false si no se pudo crear el proceso.
	bool LaunchSelf(const std::vector<std::string>& argsUtf8);

	// Ruta completa (UTF-8, con '/') del ejecutable que esta corriendo
	std::string GetExecutablePath();

	// Lanza un programa (sin esperar). workingDir "" = el del proceso actual.
	bool LaunchProcess(const std::string& exePathUtf8, const std::vector<std::string>& argsUtf8, const std::string& workingDirUtf8);

	// Ejecuta un programa sin ventana y espera a que acabe. Devuelve su codigo de salida (-1 si no
	// se pudo lanzar). Si outOutput no es nullptr recoge lo que escribio por stdout/stderr.
	int RunProcessAndWait(const std::string& exePathUtf8, const std::vector<std::string>& argsUtf8,
		const std::string& workingDirUtf8, std::string* outOutput = nullptr);

	// Ruta de una herramienta del sistema (p.ej. "tar.exe" en System32). "" si no existe.
	std::string FindSystemTool(const std::string& fileName);

	// Argumentos de la linea de comandos en UTF-8 (en Windows 'argv' viene en la pagina de codigos
	// ANSI y estropea rutas con acentos, asi que se leen de GetCommandLineW)
	std::vector<std::string> GetCommandLineArgs(int argc, char* argv[]);
}
