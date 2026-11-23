#include <iostream>
#include "Engine/Engine.h"
#include "Engine/FileSystem/FileSystem.h"
#include "Engine/Project/HubRegistry.h"
#include "Engine/Project/ProjectInfo.h"
#include "Engine/UI/EditorPlatform.h"
#include "Hub/HubApp.h"
#include "Utils/Log.h"
#include "Version.h"

namespace
{
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
	//   (sin argumentos)        -> splash + Hub de proyectos
	//   --project <carpeta>     -> abre ese proyecto directamente en el editor
	//   --no-splash             -> Hub sin splash
	// ------------------------------------------------------------------------------------
	std::string projectRoot;
	bool showSplash = true;
	const std::vector<std::string> args = EditorPlatform::GetCommandLineArgs(argc, argv);
	for (size_t i = 0; i < args.size(); ++i)
	{
		if ((args[i] == "--project" || args[i] == "-p") && i + 1 < args.size()) projectRoot = args[++i];
		else if (args[i] == "--no-splash") showSplash = false;
	}

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

	//Initializes the engine state
	Engine::EngineState state = Engine::EngineState::CREATE;
	int result = EXIT_FAILURE;

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

	return result;
}
