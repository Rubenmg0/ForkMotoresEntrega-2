#include "BuildManifest.h"
#include "../UI/EditorPlatform.h"
#include "../../Utils/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	Runtime::Mode mode = Runtime::Mode::Editor;
	BuildManifest activeBuild;
	std::string playerDataDir;
}

const std::string& BuildManifest::GetStartScene() const
{
	static const std::string empty;
	return scenes.empty() ? empty : scenes.front();
}

bool BuildManifest::LoadFromFile(const std::string& diskPathUtf8)
{
	std::ifstream file(fs::u8path(diskPathUtf8), std::ios::binary);
	if (!file) return false;

	try
	{
		std::ostringstream stream;
		stream << file.rdbuf();
		const json root = json::parse(stream.str());
		productName = root.value("productName", std::string("Game"));
		companyName = root.value("companyName", std::string());
		version = root.value("version", std::string("0.1.0"));
		engineVersion = root.value("engineVersion", std::string());
		engineCommit = root.value("engineCommit", std::string());
		buildId = root.value("buildId", std::string());
		buildDate = root.value("buildDate", std::string());
		development = root.value("development", false);
		scenes.clear();
		if (root.contains("scenes") && root["scenes"].is_array())
		{
			for (const json& scene : root["scenes"]) if (scene.is_string()) scenes.push_back(scene.get<std::string>());
		}
		if (root.contains("window") && root["window"].is_object())
		{
			const json& w = root["window"];
			window.width = w.value("width", window.width);
			window.height = w.value("height", window.height);
			window.fullscreen = w.value("fullscreen", window.fullscreen);
			window.resizable = w.value("resizable", window.resizable);
			window.vsync = w.value("vsync", window.vsync);
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("[Build] build.json invalido: %s", e.what());
		return false;
	}
	return true;
}

bool BuildManifest::SaveToFile(const std::string& diskPathUtf8) const
{
	json root;
	root["fileVersion"] = FILE_VERSION;
	root["productName"] = productName;
	root["companyName"] = companyName;
	root["version"] = version;
	root["engineVersion"] = engineVersion;
	root["engineCommit"] = engineCommit;
	root["buildId"] = buildId;
	root["buildDate"] = buildDate;
	root["development"] = development;
	root["scenes"] = scenes;
	root["window"] = {
		{ "width", window.width }, { "height", window.height }, { "fullscreen", window.fullscreen },
		{ "resizable", window.resizable }, { "vsync", window.vsync } };

	std::ofstream file(fs::u8path(diskPathUtf8), std::ios::binary | std::ios::trunc);
	if (!file) return false;
	const std::string text = root.dump(4);
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(file);
}

namespace Runtime
{
	Mode GetMode() { return mode; }
	bool IsPlayer() { return mode == Mode::Player; }
	bool IsBatch() { return mode == Mode::Batch; }

	void SetBatchMode() { mode = Mode::Batch; }

	void EnterPlayerMode(const std::string& dataDir, const BuildManifest& manifest)
	{
		mode = Mode::Player;
		playerDataDir = dataDir;
		activeBuild = manifest;
	}

	const BuildManifest& GetBuild() { return activeBuild; }
	const std::string& GetPlayerDataDir() { return playerDataDir; }

	std::string FindPlayerDataNextToExecutable()
	{
		// Nombre del ejecutable sin extension (el player se renombra como el producto)
		const std::string exePath = EditorPlatform::GetExecutablePath();
		if (exePath.empty()) return std::string();
		try
		{
			const fs::path exe = fs::u8path(exePath);
			const fs::path dataDir = exe.parent_path() / fs::u8path(exe.stem().u8string() + "_Data");
			std::error_code ec;
			if (!fs::is_regular_file(dataDir / "build.json", ec)) return std::string();
			const auto text = dataDir.generic_u8string();
			return std::string(text.begin(), text.end());
		}
		catch (...)
		{
			return std::string();
		}
	}
}
