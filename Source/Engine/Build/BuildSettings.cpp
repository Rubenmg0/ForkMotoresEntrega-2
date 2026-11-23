#include "BuildSettings.h"
#include "../FileSystem/FileSystem.h"
#include "../Project/ProjectInfo.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../../Utils/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>

using json = nlohmann::json;

const char* VersionBumpToString(VersionBump bump)
{
	switch (bump)
	{
	case VersionBump::Patch: return "patch";
	case VersionBump::Minor: return "minor";
	case VersionBump::Major: return "major";
	default: return "none";
	}
}

VersionBump VersionBumpFromString(const std::string& text)
{
	if (text == "patch") return VersionBump::Patch;
	if (text == "minor") return VersionBump::Minor;
	if (text == "major") return VersionBump::Major;
	return VersionBump::None;
}

BuildSettings BuildSettings::LoadOrCreate()
{
	BuildSettings settings;
	const ProjectInfo* project = Project::HasActive() ? &Project::GetActive() : nullptr;
	settings.productName = project ? project->name : std::string("Game");

	std::string text;
	if (FileSystem::ReadText(FILE, text))
	{
		try
		{
			const json root = json::parse(text);
			settings.productName = root.value("productName", settings.productName);
			settings.companyName = root.value("companyName", settings.companyName);
			settings.outputDir = root.value("outputDir", settings.outputDir);
			settings.development = root.value("development", settings.development);
			settings.bumpAfterRelease = VersionBumpFromString(root.value("bumpAfterRelease", std::string("patch")));
			if (root.contains("scenes") && root["scenes"].is_array())
			{
				for (const json& item : root["scenes"])
				{
					SceneInBuild scene;
					scene.path = item.value("path", std::string());
					scene.enabled = item.value("enabled", true);
					if (!scene.path.empty()) settings.scenes.push_back(scene);
				}
			}
			if (root.contains("window") && root["window"].is_object())
			{
				const json& w = root["window"];
				settings.window.width = w.value("width", settings.window.width);
				settings.window.height = w.value("height", settings.window.height);
				settings.window.fullscreen = w.value("fullscreen", settings.window.fullscreen);
				settings.window.resizable = w.value("resizable", settings.window.resizable);
				settings.window.vsync = w.value("vsync", settings.window.vsync);
			}
			return settings;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("[Build] %s invalido (%s): se usan los ajustes por defecto", FILE, e.what());
			settings.scenes.clear();
		}
	}

	// Por defecto: la ultima escena abierta primero (sera la de arranque) y el resto despues
	if (project && !project->lastScene.empty() && FileSystem::Exists(project->lastScene))
	{
		settings.scenes.push_back({ project->lastScene, true });
	}
	std::vector<std::string> files;
	FileSystem::ListFilesRecursive(FileSystem::ASSETS_DIR, files);
	std::sort(files.begin(), files.end());
	for (const std::string& file : files)
	{
		if (!ResourceManager::IsSceneFile(file)) continue;
		const bool already = std::any_of(settings.scenes.begin(), settings.scenes.end(), [&](const SceneInBuild& s) { return s.path == file; });
		if (!already) settings.scenes.push_back({ file, true });
	}
	return settings;
}

bool BuildSettings::Save() const
{
	json root;
	root["productName"] = productName;
	root["companyName"] = companyName;
	root["outputDir"] = outputDir;
	root["development"] = development;
	root["bumpAfterRelease"] = VersionBumpToString(bumpAfterRelease);
	json list = json::array();
	for (const SceneInBuild& scene : scenes) list.push_back({ { "path", scene.path }, { "enabled", scene.enabled } });
	root["scenes"] = list;
	root["window"] = {
		{ "width", window.width }, { "height", window.height }, { "fullscreen", window.fullscreen },
		{ "resizable", window.resizable }, { "vsync", window.vsync } };

	FileSystem::CreateDirectories(FileSystem::PROJECT_SETTINGS_DIR);
	return FileSystem::WriteText(FILE, root.dump(4));
}

std::vector<std::string> BuildSettings::GetEnabledScenes() const
{
	std::vector<std::string> result;
	for (const SceneInBuild& scene : scenes) if (scene.enabled) result.push_back(scene.path);
	return result;
}

std::string BuildSettings::GetOutputDir() const
{
	if (!outputDir.empty()) return FileSystem::Normalize(outputDir);
	const std::string root = Project::HasActive() ? Project::GetActiveRoot() : Project::NormalizeRoot(FileSystem::GetProjectRoot());
	return root + "/Builds";
}

std::string BuildSettings::GetProductDir() const
{
	return GetOutputDir() + "/" + productName;
}

std::string BuildSettings::GetExecutablePath() const
{
	return GetProductDir() + "/" + productName + ".exe";
}

void BuildSettings::AddMissingScenes()
{
	std::vector<std::string> files;
	FileSystem::ListFilesRecursive(FileSystem::ASSETS_DIR, files);
	std::sort(files.begin(), files.end());
	const bool hadScenes = !scenes.empty();
	for (const std::string& file : files)
	{
		if (!ResourceManager::IsSceneFile(file)) continue;
		const bool already = std::any_of(scenes.begin(), scenes.end(), [&](const SceneInBuild& s) { return s.path == file; });
		if (!already) scenes.push_back({ file, !hadScenes });
	}
}
