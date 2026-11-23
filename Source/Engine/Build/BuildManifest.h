#pragma once

#include <cstdint>
#include <string>
#include <vector>

// =====================================================================================
// Build de un juego (lo que genera File > Build Settings).
//
// Una build es una carpeta autocontenida, igual que en Unity:
//
//      <Producto>/
//          <Producto>.exe              el mismo binario del motor, renombrado (el "player")
//          *.dll                       dependencias (SDL3, assimp, DevIL...)
//          <Producto>_Data/
//              build.json              ESTE manifiesto: producto, version, escenas, ventana...
//              resources.json          recursos "cocinados" que incluye la build (UID -> fichero)
//              Assets/Scenes/*.scene   solo las escenas marcadas en "Scenes In Build"
//              Library/...             solo los ficheros de Library que usan esas escenas
//
// Al arrancar, el ejecutable mira si a su lado hay "<nombre del exe>_Data/build.json". Si lo hay
// se ejecuta como PLAYER (sin Hub ni editor: ventana del juego a pantalla completa con la camara
// principal y la escena en modo Play). Si no, es el editor de siempre.
// =====================================================================================

// Ajustes de la ventana del juego (Player Settings de Unity)
struct PlayerWindowSettings
{
	int width = 1280;
	int height = 720;
	bool fullscreen = false;
	bool resizable = true;
	bool vsync = true;
};

struct BuildManifest
{
	static constexpr int FILE_VERSION = 1;

	std::string productName;
	std::string companyName;
	std::string version;            // version del proyecto en el momento de la build
	std::string engineVersion;
	std::string engineCommit;
	std::string buildId;            // identificador unico de esta build
	std::string buildDate;          // "2026-11-23 14:05"
	bool development = false;       // Development Build: overlay de estadisticas y log detallado
	std::vector<std::string> scenes; // rutas de proyecto, en orden. La primera es la de arranque
	PlayerWindowSettings window;

	const std::string& GetStartScene() const;

	bool LoadFromFile(const std::string& diskPathUtf8);
	bool SaveToFile(const std::string& diskPathUtf8) const;
};

// =====================================================================================
// Modo de ejecucion del proceso
// =====================================================================================
namespace Runtime
{
	enum class Mode
	{
		Editor,   // Hub + editor (lo normal)
		Player,   // build de un juego
		Batch     // editor sin ventana visible para builds por linea de comandos (--build / --release)
	};

	Mode GetMode();
	bool IsPlayer();
	bool IsBatch();

	void SetBatchMode();
	// Entra en modo player con la carpeta de datos (<Producto>_Data) y su manifiesto
	void EnterPlayerMode(const std::string& dataDir, const BuildManifest& manifest);
	const BuildManifest& GetBuild();
	const std::string& GetPlayerDataDir();

	// Busca "<carpeta del exe>/<nombre del exe>_Data/build.json". Devuelve la carpeta de datos o "".
	std::string FindPlayerDataNextToExecutable();
}
