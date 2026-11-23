#include <cstdio>
#include <iostream>
#include "Engine/Engine.h"
#include "Engine/Build/BuildManifest.h"
#include "Engine/Build/BuildPipeline.h"
#include "Engine/Build/ReleaseManager.h"
#include "Engine/FileSystem/FileSystem.h"
#include "Engine/Project/HubRegistry.h"
#include "Engine/Project/ProjectInfo.h"
#include "Engine/UI/EditorPlatform.h"
#include "Hub/HubApp.h"
#include "Utils/Log.h"
#include "Version.h"

namespace
{
	enum class BatchCommand { None, Build, Release };

	void PrintLines(const std::vector<std::string>& lines, const char* prefix)
	{
		for (const std::string& line : lines) std::printf("%s%s\n", prefix, line.c_str());
	}

	// --build / --release: se ejecuta con el motor ya arrancado (sin ventana visible).
	// Devuelve el codigo de salida del proceso (0 = correcto), util para scripts y CI.
	int RunBatchCommand(BatchCommand command, const std::string& outputDir, int developmentOverride,
		const std::string& notes, const std::string& bumpText)
	{
		BuildSettings settings = BuildSettings::LoadOrCreate();
		if (!outputDir.empty()) settings.outputDir = Project::NormalizeRoot(outputDir);

		if (command == BatchCommand::Build)
		{
			const bool development = (developmentOverride < 0) ? settings.development : (developmentOverride == 1);
			const BuildReport report = BuildPipeline::Build(settings, development);
			PrintLines(report.steps, "");
			PrintLines(report.warnings, "warning: ");
			PrintLines(report.errors, "error: ");
			if (report.success) std::printf("Executable: %s\n", report.executablePath.c_str());
			return report.success ? EXIT_SUCCESS : EXIT_FAILURE;
		}

		const VersionBump bump = bumpText.empty() ? settings.bumpAfterRelease : VersionBumpFromString(bumpText);
		const ReleaseReport report = ReleaseManager::CreateRelease(settings, notes, bump);
		PrintLines(report.steps, "");
		PrintLines(report.build.warnings, "warning: ");
		PrintLines(report.errors, "error: ");
		if (report.success)
		{
			std::printf("Archive: %s\nSHA-256: %s\n", report.entry.archive.c_str(), report.entry.sha256.c_str());
		}
		return report.success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	// Prepara el proyecto elegido (Hub o --project) antes de arrancar el editor:
	// carga / crea su project.json, apunta la version del motor y fija la raiz del FileSystem.
	bool PrepareProject(const std::string& requestedRoot)
	{
		const std::string root = Project::NormalizeRoot(requestedRoot);
		if (!Project::FolderExists(root))
		{
			LOG_ERROR("[Project] The project folder '%s' does not exist", root.c_str());
			return false;
		}

		ProjectInfo info;
		bool needsSave = false;
		if (!Project::LoadOrAdopt(root, info))
		{
			// Carpeta vacia pasada con --project: se convierte en un proyecto nuevo
			info = ProjectInfo();
			const size_t slash = root.find_last_of('/');
			info.name = (slash == std::string::npos) ? root : root.substr(slash + 1);
			info.createdWithEngine = ENGINE_VERSION_STRING;
			info.createdAt = Project::Now();
			info.lastModifiedAt = info.createdAt;
			needsSave = true;
		}

		// Se apunta la version del motor que abre el proyecto. La fecha de ultima apertura va a
		// hub.json (por usuario), no a project.json: abrir un proyecto no cambia ficheros versionados
		if (info.engineVersion != ENGINE_VERSION_STRING)
		{
			info.engineVersion = ENGINE_VERSION_STRING;
			needsSave = true;
		}
		if (needsSave) info.Save(root);

		HubRegistry::Load();
		HubRegistry::AddOrTouch(root);
		HubRegistry::Save();

		Project::SetActive(root, info);
		FileSystem::SetProjectRoot(root);
		LOG("[Project] Opening '%s' v%s (%s)", info.name.c_str(), info.version.ToString().c_str(), root.c_str());
		return true;
	}
}

int main(int argc, char* argv[]) {

	LOG("%s %s (%s) starting ...", ENGINE_NAME, ENGINE_VERSION_STRING, ENGINE_GIT_HASH);

	// ------------------------------------------------------------------------------------
	// Linea de comandos:
	//   (sin argumentos)          -> splash + Hub de proyectos
	//   --project <carpeta>       -> abre ese proyecto directamente en el editor
	//   --no-splash               -> Hub sin splash
	//   --player <carpeta _Data>  -> ejecuta una build (normalmente se detecta sola: <exe>_Data/)
	//
	// Builds sin interfaz (como -batchmode de Unity o --export-release de Godot), con --project:
	//   --build                   -> build con los ajustes de ProjectSettings/BuildSettings.json
	//   --release                 -> release: build Release + zip + SHA-256 + changelog
	//   --development / --release-config   configuracion de --build
	//   --output <carpeta>        -> carpeta de salida
	//   --notes "<texto>"         -> notas de la release
	//   --bump none|patch|minor|major   version del proyecto tras la release
	// ------------------------------------------------------------------------------------
	std::string projectRoot;
	std::string playerDataDir;
	std::string outputDir;
	std::string releaseNotes;
	std::string bumpText;
	BatchCommand batchCommand = BatchCommand::None;
	int developmentOverride = -1;
	bool showSplash = true;
	const std::vector<std::string> args = EditorPlatform::GetCommandLineArgs(argc, argv);
	for (size_t i = 0; i < args.size(); ++i)
	{
		const bool hasValue = i + 1 < args.size();
		if ((args[i] == "--project" || args[i] == "-p") && hasValue) projectRoot = args[++i];
		else if (args[i] == "--no-splash") showSplash = false;
		else if (args[i] == "--player" && hasValue) playerDataDir = args[++i];
		else if (args[i] == "--build") batchCommand = BatchCommand::Build;
		else if (args[i] == "--release") batchCommand = BatchCommand::Release;
		else if (args[i] == "--development") developmentOverride = 1;
		else if (args[i] == "--release-config") developmentOverride = 0;
		else if (args[i] == "--output" && hasValue) outputDir = args[++i];
		else if (args[i] == "--notes" && hasValue) releaseNotes = args[++i];
		else if (args[i] == "--bump" && hasValue) bumpText = args[++i];
	}

	// ----------------------------- Player (build de un juego) -----------------------------
	if (playerDataDir.empty()) playerDataDir = Runtime::FindPlayerDataNextToExecutable();
	if (!playerDataDir.empty())
	{
		BuildManifest manifest;
		const std::string dataDir = Project::NormalizeRoot(playerDataDir);
		if (!manifest.LoadFromFile(dataDir + "/build.json"))
		{
			LOG_ERROR("[Player] '%s' is not a valid build (build.json missing)", dataDir.c_str());
			return EXIT_FAILURE;
		}
		Runtime::EnterPlayerMode(dataDir, manifest);
		FileSystem::SetProjectRoot(dataDir);
	}
	else
	{
		// ----------------------------- Builds por linea de comandos -----------------------------
		if (batchCommand != BatchCommand::None)
		{
			if (projectRoot.empty())
			{
				std::printf("error: --build / --release need --project <folder>\n");
				return EXIT_FAILURE;
			}
			Runtime::SetBatchMode();
		}

		// ----------------------------- Hub -> editor -----------------------------
		if (projectRoot.empty())
		{
			HubApp hub;
			if (!hub.Run(showSplash, projectRoot))
			{
				LOG("Hub closed without opening a project");
				return EXIT_SUCCESS;
			}
		}

		if (!PrepareProject(projectRoot)) return EXIT_FAILURE;
	}

	//Initializes the engine state
	Engine::EngineState state = Engine::EngineState::CREATE;
	int result = EXIT_FAILURE;
	int batchResult = EXIT_SUCCESS;

	while (state != Engine::EngineState::EXIT)
	{
		switch (state)
		{
			// Allocate the engine --------------------------------------------
		case Engine::EngineState::CREATE:
			LOG("CREATION PHASE ===============================");
			state = Engine::EngineState::AWAKE;

			break;

			// Awake all modules -----------------------------------------------
		case Engine::EngineState::AWAKE:
			LOG("AWAKE PHASE ===============================");
			if (Engine::GetInstance().Awake() == true)
				state = Engine::EngineState::START;
			else
			{
				LOG_ERROR("[Engine] Fallo en la fase AWAKE de los módulos");
				state = Engine::EngineState::FAIL;
			}

			break;

			// Call all modules before first frame  ----------------------------
		case Engine::EngineState::START:
			LOG("START PHASE ===============================");
			if (Engine::GetInstance().Start() == true)
			{
				if (batchCommand != BatchCommand::None)
				{
					// Modo batch: los assets ya estan importados (ResourceManager::Start); se hace la
					// build / release y se sale sin abrir el editor
					batchResult = RunBatchCommand(batchCommand, outputDir, developmentOverride, releaseNotes, bumpText);
					state = Engine::EngineState::CLEAN;
					break;
				}
				state = Engine::EngineState::LOOP;
				LOG("UPDATE PHASE ===============================");
			}
			else
			{
				LOG_ERROR("[Engine] Fallo en la fase START de los módulos");
				state = Engine::EngineState::FAIL;
			}
			break;

			// Loop all modules until we are asked to leave ---------------------
		case Engine::EngineState::LOOP:
			if (Engine::GetInstance().Update() == false)
				state = Engine::EngineState::CLEAN;
			break;

			// Cleanup allocated memory -----------------------------------------
		case Engine::EngineState::CLEAN:
			LOG("CLEANUP PHASE ===============================");
			if (Engine::GetInstance().CleanUp() == true)
			{
				result = EXIT_SUCCESS;
				state = Engine::EngineState::EXIT;
			}
			else
			{
				LOG_ERROR("[Engine] Fallo en la fase CLEANUP de los módulos");
				state = Engine::EngineState::FAIL;
			}

			break;

			// Exit with errors and shame ---------------------------------------
		case Engine::EngineState::FAIL:
			LOG_ERROR("[Engine] El motor ha finalizado con errores críticos.");
			result = EXIT_FAILURE;
			state = Engine::EngineState::EXIT;
			break;
		}
	}

	LOG("Closing Engine ===============================");

	// En modo batch el codigo de salida es el de la build / release
	if (batchCommand != BatchCommand::None && result == EXIT_SUCCESS) result = batchResult;
	return result;
}
