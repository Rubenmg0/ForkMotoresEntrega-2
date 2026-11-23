#include "BuildPipeline.h"
#include "../Engine.h"
#include "../FileSystem/FileSystem.h"
#include "../Project/ProjectInfo.h"
#include "../ResourceManagement/ResourceManager.h"
#include "../UI/EditorPlatform.h"
#include "../../Utils/Log.h"
#include "../../Utils/PerfTimer.h"
#include "Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	fs::path U8(const std::string& path)
	{
		try { return fs::u8path(path); }
		catch (...) { return fs::path(); }
	}

	std::string ToU8(const fs::path& path)
	{
		try
		{
			const auto text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}
		catch (...) { return std::string(); }
	}

	std::string RandomBuildId()
	{
		std::random_device device;
		std::mt19937_64 generator((static_cast<uint64_t>(device()) << 32) ^ static_cast<uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		char text[17];
		snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(generator()));
		return std::string(text, 8);
	}

	// Recorre el JSON de una escena y apunta los UID de mallas ("mesh") y texturas ("texture")
	void CollectSceneResources(const json& node, std::set<UID>& meshes, std::set<UID>& textures)
	{
		if (node.is_object())
		{
			for (auto it = node.begin(); it != node.end(); ++it)
			{
				if (it.value().is_number_unsigned() || it.value().is_number_integer())
				{
					const UID uid = it.value().get<UID>();
					if (uid <= ResourceManager::BUILTIN_PYRAMID) continue; // 0 o primitivas internas
					if (it.key() == "mesh") meshes.insert(uid);
					else if (it.key() == "texture") textures.insert(uid);
				}
				else
				{
					CollectSceneResources(it.value(), meshes, textures);
				}
			}
		}
		else if (node.is_array())
		{
			for (const json& child : node) CollectSceneResources(child, meshes, textures);
		}
	}

	// Copia un fichero creando las carpetas. Suma ficheros y bytes al informe.
	bool CopyFileTo(const fs::path& from, const fs::path& to, BuildReport& report)
	{
		std::error_code ec;
		fs::create_directories(to.parent_path(), ec);
		fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			report.errors.push_back("Could not copy '" + ToU8(from) + "': " + ec.message());
			return false;
		}
		report.fileCount++;
		report.totalBytes += fs::file_size(to, ec);
		return true;
	}

	bool WriteTextFile(const fs::path& path, const std::string& text, BuildReport& report)
	{
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			report.errors.push_back("Could not write '" + ToU8(path) + "'");
			return false;
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		report.fileCount++;
		report.totalBytes += text.size();
		return true;
	}

	// true si 'child' esta dentro de 'parent' (o es la misma carpeta)
	bool IsSameOrInside(const fs::path& child, const fs::path& parent)
	{
		const std::string c = ToU8(child.lexically_normal());
		std::string p = ToU8(parent.lexically_normal());
		if (!p.empty() && p.back() == '/') p.pop_back();
		auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); }); return s; };
		const std::string lc = lower(c), lp = lower(p);
		return lc == lp || (lc.size() > lp.size() && lc.compare(0, lp.size(), lp) == 0 && lc[lp.size()] == '/');
	}

	void Step(BuildReport& report, const std::string& text)
	{
		report.steps.push_back(text);
		LOG("[Build] %s", text.c_str());
	}
}

namespace BuildPipeline
{
	std::string FormatBytes(uint64_t bytes)
	{
		char text[32];
		if (bytes >= 1024ull * 1024ull * 1024ull) snprintf(text, sizeof(text), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
		else if (bytes >= 1024ull * 1024ull) snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
		else if (bytes >= 1024ull) snprintf(text, sizeof(text), "%.1f KB", bytes / 1024.0);
		else snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
		return text;
	}

	bool CleanBuild(const BuildSettings& settings, std::string* outError)
	{
		auto fail = [&](const std::string& message) { if (outError) *outError = message; return false; };
		std::error_code ec;
		const fs::path productDir = fs::absolute(U8(settings.GetProductDir()), ec).lexically_normal();
		if (!fs::exists(productDir, ec)) return true;

		// Proteccion: nunca se borra el proyecto ni una carpeta que no sea una build nuestra
		const fs::path projectRoot = fs::absolute(U8(Project::HasActive() ? Project::GetActiveRoot() : FileSystem::GetProjectRoot()), ec).lexically_normal();
		if (IsSameOrInside(projectRoot, productDir)) return fail("The output folder contains the project itself");
		const fs::path marker = productDir / U8(settings.productName + "_Data") / "build.json";
		if (!fs::is_regular_file(marker, ec))
		{
			if (fs::is_empty(productDir, ec)) return true;
			return fail("'" + ToU8(productDir) + "' already exists and is not a build. Choose another output folder.");
		}
		fs::remove_all(productDir, ec);
		if (ec) return fail("Could not delete the previous build (is the game still running?): " + ec.message());
		return true;
	}

	BuildReport Build(const BuildSettings& settings, bool development)
	{
		PerfTimer timer;
		BuildReport report;
		report.development = development;
		report.productName = settings.productName;
		report.timestamp = Project::Now();
		report.buildId = RandomBuildId();
		report.version = Project::HasActive() ? Project::GetActive().version.ToString() : std::string("0.1.0");

		auto finish = [&]() -> BuildReport&
		{
			report.seconds = timer.ReadMs() / 1000.0;
			report.success = report.errors.empty();
			char summary[256];
			snprintf(summary, sizeof(summary), "%s: %d files, %s in %.2f s (%zu warnings, %zu errors)",
				report.success ? "Build succeeded" : "Build failed", report.fileCount, FormatBytes(report.totalBytes).c_str(),
				report.seconds, report.warnings.size(), report.errors.size());
			report.steps.push_back(summary);
			if (report.success) LOG("[Build] %s", summary);
			else LOG_ERROR("[Build] %s", summary);
			for (const std::string& error : report.errors) LOG_ERROR("[Build] %s", error.c_str());
			for (const std::string& warning : report.warnings) LOG_WARNING("[Build] %s", warning.c_str());

			// Informe en <salida>/Logs/build-<fecha>-<id>.log
			std::error_code ec;
			const fs::path logDir = U8(settings.GetOutputDir()) / "Logs";
			fs::create_directories(logDir, ec);
			std::string date = Project::FormatDate(report.timestamp);
			std::replace(date.begin(), date.end(), ':', '-');
			std::replace(date.begin(), date.end(), ' ', '_');
			const fs::path logPath = logDir / U8("build-" + date + "-" + report.buildId + ".log");
			std::ofstream log(logPath, std::ios::binary);
			if (log)
			{
				log << ENGINE_NAME << " " << ENGINE_VERSION_STRING << " (" << ENGINE_GIT_HASH << ") build report\n";
				log << "Product: " << report.productName << " " << report.version << (development ? " [Development]" : " [Release]") << "\n";
				log << "Build id: " << report.buildId << "\nDate: " << Project::FormatDate(report.timestamp) << "\n\n";
				for (const std::string& line : report.steps) log << line << "\n";
				for (const std::string& line : report.warnings) log << "WARNING: " << line << "\n";
				for (const std::string& line : report.errors) log << "ERROR: " << line << "\n";
				report.logPath = ToU8(logPath);
			}
			return report;
		};

		// ------------------------------- 1. Validar -------------------------------
		Step(report, "Validating build settings...");
		std::string nameError;
		if (!Project::IsValidProjectName(settings.productName, &nameError)) report.errors.push_back("Product name: " + nameError);
		const std::vector<std::string> scenes = settings.GetEnabledScenes();
		if (scenes.empty()) report.errors.push_back("There are no scenes in the build. Add at least one scene in Build Settings.");
		for (const std::string& scene : scenes)
		{
			if (!FileSystem::Exists(scene)) report.errors.push_back("Scene not found: " + scene);
		}
		if (!report.errors.empty()) return finish();

		// ------------------------------- 2. Carpeta de salida -------------------------------
		std::error_code ec;
		const fs::path productDir = fs::absolute(U8(settings.GetProductDir()), ec).lexically_normal();
		const fs::path dataDir = productDir / U8(settings.productName + "_Data");
		report.productDir = ToU8(productDir);
		Step(report, "Output: " + report.productDir);
		std::string cleanError;
		if (!CleanBuild(settings, &cleanError))
		{
			report.errors.push_back(cleanError);
			return finish();
		}
		fs::create_directories(dataDir, ec);
		if (ec)
		{
			report.errors.push_back("Could not create the output folder: " + ec.message());
			return finish();
		}

		// ------------------------------- 3. Player (ejecutable + DLL) -------------------------------
		Step(report, "Copying player executable and runtime libraries...");
		const fs::path engineExe = U8(EditorPlatform::GetExecutablePath());
		const fs::path gameExe = productDir / U8(settings.productName + ".exe");
		CopyFileTo(engineExe, gameExe, report);
		report.executablePath = ToU8(gameExe);

		int dllCount = 0;
		for (const fs::directory_entry& entry : fs::directory_iterator(engineExe.parent_path(), ec))
		{
			if (!entry.is_regular_file(ec)) continue;
			std::string extension = ToU8(entry.path().extension());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".dll") continue;
			if (CopyFileTo(entry.path(), productDir / entry.path().filename(), report)) ++dllCount;
		}

		// Runtime de Visual C++ junto al juego (despliegue "app-local"): asi funciona en equipos
		// sin el Visual C++ Redistributable instalado
		const char* vcRuntime[] = { "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll" };
		if (std::string(ENGINE_BUILD_CONFIG) == "Release")
		{
			for (const char* dll : vcRuntime)
			{
				if (fs::exists(productDir / dll, ec)) continue;
				const std::string systemDll = EditorPlatform::FindSystemTool(dll);
				if (!systemDll.empty() && CopyFileTo(U8(systemDll), productDir / dll, report)) ++dllCount;
			}
		}
		else
		{
			report.warnings.push_back("The engine is a Debug build: the game uses the debug runtime and only runs on machines with "
				"Visual Studio. Build the engine in Release to ship games.");
		}
		Step(report, "  " + std::to_string(dllCount) + " libraries copied");

		// ------------------------------- 4. Cocinar escenas y recursos -------------------------------
		Step(report, "Cooking " + std::to_string(scenes.size()) + " scene(s)...");
		std::set<UID> meshes, textures;
		for (const std::string& scene : scenes)
		{
			std::string text;
			if (!FileSystem::ReadText(scene, text))
			{
				report.errors.push_back("Could not read scene " + scene);
				continue;
			}
			try
			{
				const json root = json::parse(text);
				const size_t before = meshes.size() + textures.size();
				CollectSceneResources(root, meshes, textures);
				const size_t objects = root.contains("gameObjects") && root["gameObjects"].is_array() ? root["gameObjects"].size() : 0;
				Step(report, "  " + scene + ": " + std::to_string(objects) + " GameObjects, " +
					std::to_string(meshes.size() + textures.size() - before) + " new resources");
			}
			catch (const std::exception& e)
			{
				report.errors.push_back("Invalid scene " + scene + ": " + e.what());
				continue;
			}
			CopyFileTo(U8(FileSystem::ToDiskPath(scene)), dataDir / U8(scene), report);
			report.sceneCount++;
		}

		ResourceManager* resources = Engine::GetInstance().resources.get();
		json resourceList = json::array();
		auto cookResource = [&](UID uid, ResourceType type)
		{
			const Resource* resource = resources ? resources->GetResource(uid) : nullptr;
			const std::string libraryPath = (resource && !resource->GetLibraryPath().empty()) ? resource->GetLibraryPath() : GetLibraryPathFor(type, uid);
			if (resource && resource->GetType() != type)
			{
				report.warnings.push_back("Resource " + std::to_string(uid) + " has an unexpected type; skipped");
				return;
			}
			if (!FileSystem::Exists(libraryPath))
			{
				report.warnings.push_back(std::string("Missing ") + ResourceTypeToString(type) + " " + std::to_string(uid) +
					(resource ? " ('" + resource->GetName() + "')" : std::string()) + ": open the project in the editor so it is imported. It will render as missing.");
				return;
			}
			if (!CopyFileTo(U8(FileSystem::ToDiskPath(libraryPath)), dataDir / U8(libraryPath), report)) return;
			resourceList.push_back({
				{ "uid", uid }, { "type", ResourceTypeToString(type) },
				{ "name", resource ? resource->GetName() : std::to_string(uid) },
				{ "asset", resource ? resource->GetAssetPath() : std::string() },
				{ "library", libraryPath } });
			report.resourceCount++;
		};
		for (UID uid : meshes) cookResource(uid, ResourceType::Mesh);
		for (UID uid : textures) cookResource(uid, ResourceType::Texture);
		Step(report, "  " + std::to_string(report.resourceCount) + " resources cooked (" + std::to_string(meshes.size()) + " meshes, " +
			std::to_string(textures.size()) + " textures referenced)");

		// ------------------------------- 5. Manifiestos -------------------------------
		Step(report, "Writing build manifest...");
		json resourcesRoot;
		resourcesRoot["fileVersion"] = 1;
		resourcesRoot["resources"] = resourceList;
		WriteTextFile(dataDir / ResourceManager::BUILD_RESOURCES_FILE, resourcesRoot.dump(4), report);

		BuildManifest manifest;
		manifest.productName = settings.productName;
		manifest.companyName = settings.companyName;
		manifest.version = report.version;
		manifest.engineVersion = ENGINE_VERSION_STRING;
		manifest.engineCommit = ENGINE_GIT_HASH;
		manifest.buildId = report.buildId;
		manifest.buildDate = Project::FormatDate(report.timestamp);
		manifest.development = development;
		manifest.scenes = scenes;
		manifest.window = settings.window;
		const fs::path manifestPath = dataDir / "build.json";
		if (manifest.SaveToFile(ToU8(manifestPath)))
		{
			report.fileCount++;
			report.totalBytes += fs::file_size(manifestPath, ec);
		}
		else report.errors.push_back("Could not write build.json");

		Step(report, "Start scene: " + manifest.GetStartScene() + (development ? "  [Development Build]" : "  [Release]"));
		return finish();
	}
}
