#include "ReleaseManager.h"
#include "Sha256.h"
#include "../FileSystem/FileSystem.h"
#include "../Project/ProjectInfo.h"
#include "../UI/EditorPlatform.h"
#include "../../Utils/Log.h"
#include "Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

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

	void Step(ReleaseReport& report, const std::string& text)
	{
		report.steps.push_back(text);
		LOG("[Release] %s", text.c_str());
	}

	bool SaveHistory(const std::vector<ReleaseEntry>& history)
	{
		json list = json::array();
		for (const ReleaseEntry& entry : history)
		{
			list.push_back({
				{ "version", entry.version }, { "productName", entry.productName }, { "date", entry.date },
				{ "notes", entry.notes }, { "archive", entry.archive }, { "sha256", entry.sha256 }, { "size", entry.size },
				{ "buildId", entry.buildId }, { "engineVersion", entry.engineVersion } });
		}
		json root;
		root["releases"] = list;
		return FileSystem::WriteText(ReleaseManager::HISTORY_FILE, root.dump(4));
	}

	// Inserta la seccion de la release al principio de CHANGELOG.md (formato "Keep a Changelog")
	void UpdateChangelog(const ReleaseEntry& entry)
	{
		std::string text;
		FileSystem::ReadText(ReleaseManager::CHANGELOG_FILE, text);
		const std::string header = "# Changelog\n\nAll notable changes to this project are documented in this file.\n";
		if (text.empty()) text = header;

		std::string date = Project::FormatDate(entry.date).substr(0, 10);
		std::string section = "\n## [" + entry.version + "] - " + date + "\n\n";
		std::string notes = entry.notes.empty() ? std::string("- Release ") + entry.version : entry.notes;
		// Cada linea de las notas como un punto de la lista
		std::istringstream stream(notes);
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			section += (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) ? line + "\n" : "- " + line + "\n";
		}
		section += "\nSHA-256 `" + entry.sha256 + "` (" + FileSystem::GetFileName(entry.archive) + ")\n";

		// Despues de la cabecera (la primera linea que empieza por "## " marca la release anterior)
		const size_t firstRelease = text.find("\n## ");
		if (firstRelease == std::string::npos) text += section;
		else text.insert(firstRelease, section);
		FileSystem::WriteText(ReleaseManager::CHANGELOG_FILE, text);
	}
}

namespace ReleaseManager
{
	std::string GetArchiveName(const std::string& productName, const std::string& version)
	{
		std::string name = productName;
		std::replace(name.begin(), name.end(), ' ', '-');
		return name + "-" + version + "-win64.zip";
	}

	std::vector<ReleaseEntry> LoadHistory()
	{
		std::vector<ReleaseEntry> history;
		std::string text;
		if (!FileSystem::ReadText(HISTORY_FILE, text)) return history;
		try
		{
			const json root = json::parse(text);
			if (!root.contains("releases") || !root["releases"].is_array()) return history;
			for (const json& item : root["releases"])
			{
				ReleaseEntry entry;
				entry.version = item.value("version", std::string());
				entry.productName = item.value("productName", std::string());
				entry.date = item.value("date", static_cast<int64_t>(0));
				entry.notes = item.value("notes", std::string());
				entry.archive = item.value("archive", std::string());
				entry.sha256 = item.value("sha256", std::string());
				entry.size = item.value("size", static_cast<uint64_t>(0));
				entry.buildId = item.value("buildId", std::string());
				entry.engineVersion = item.value("engineVersion", std::string());
				history.push_back(entry);
			}
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("[Release] %s invalido: %s", HISTORY_FILE, e.what());
		}
		std::sort(history.begin(), history.end(), [](const ReleaseEntry& a, const ReleaseEntry& b) { return a.date > b.date; });
		return history;
	}

	bool IsVersionReleased(const std::string& version)
	{
		const std::vector<ReleaseEntry> history = LoadHistory();
		return std::any_of(history.begin(), history.end(), [&](const ReleaseEntry& e) { return e.version == version; });
	}

	ReleaseReport CreateRelease(const BuildSettings& settings, const std::string& notes, VersionBump bumpAfter)
	{
		ReleaseReport report;
		const std::string version = Project::HasActive() ? Project::GetActive().version.ToString() : std::string("0.1.0");

		// ------------------------------- 1. Version -------------------------------
		Step(report, "Release " + settings.productName + " " + version);
		if (IsVersionReleased(version))
		{
			report.errors.push_back("Version " + version + " was already released. Bump the project version before creating a new release.");
			return report;
		}

		// ------------------------------- 2. Build Release -------------------------------
		report.build = BuildPipeline::Build(settings, false);
		for (const std::string& line : report.build.steps) report.steps.push_back("  " + line);
		if (!report.build.success)
		{
			report.errors.insert(report.errors.end(), report.build.errors.begin(), report.build.errors.end());
			report.errors.push_back("The release build failed");
			return report;
		}

		// ------------------------------- 3. Empaquetar -------------------------------
		std::error_code ec;
		const fs::path productDir = U8(report.build.productDir);
		const int64_t now = Project::Now();
		{
			std::ofstream notesFile(productDir / "RELEASE_NOTES.txt", std::ios::binary);
			notesFile << settings.productName << " " << version << "\n";
			if (!settings.companyName.empty()) notesFile << settings.companyName << "\n";
			notesFile << "Released " << Project::FormatDate(now) << " - built with " << ENGINE_NAME << " " << ENGINE_VERSION_STRING << "\n\n";
			notesFile << (notes.empty() ? std::string("No release notes.") : notes) << "\n\n";
			notesFile << "Controls: F11 / Alt+Enter fullscreen, Esc quit.\n";
		}

		const fs::path releasesDir = U8(settings.GetOutputDir()) / "Releases";
		fs::create_directories(releasesDir, ec);
		const fs::path archive = releasesDir / U8(GetArchiveName(settings.productName, version));
		fs::remove(archive, ec);

		const std::string tar = EditorPlatform::FindSystemTool("tar.exe");
		if (tar.empty())
		{
			report.errors.push_back("tar.exe was not found in the system folder (needs Windows 10 1803 or newer) to create the .zip");
			return report;
		}
		Step(report, "Packaging " + ToU8(archive.filename()) + "...");
		std::string output;
		// tar de Windows (bsdtar) elige el formato por la extension con -a: .zip
		const int exitCode = EditorPlatform::RunProcessAndWait(tar,
			{ "-a", "-c", "-f", ToU8(archive), "-C", ToU8(productDir.parent_path()), ToU8(productDir.filename()) }, std::string(), &output);
		if (exitCode != 0 || !fs::is_regular_file(archive, ec))
		{
			report.errors.push_back("Could not create the zip (tar exit code " + std::to_string(exitCode) + "): " + output);
			return report;
		}

		// ------------------------------- 4. Checksum -------------------------------
		ReleaseEntry& entry = report.entry;
		entry.version = version;
		entry.productName = settings.productName;
		entry.date = now;
		entry.notes = notes;
		entry.archive = ToU8(archive);
		entry.size = fs::file_size(archive, ec);
		entry.buildId = report.build.buildId;
		entry.engineVersion = ENGINE_VERSION_STRING;
		entry.sha256 = Sha256::HashFile(entry.archive);
		report.checksumPath = entry.archive + ".sha256";
		{
			std::ofstream checksum(U8(report.checksumPath), std::ios::binary);
			checksum << entry.sha256 << " *" << ToU8(archive.filename()) << "\n";
		}
		Step(report, "  " + BuildPipeline::FormatBytes(entry.size) + ", SHA-256 " + entry.sha256);

		// ------------------------------- 5. Historial y changelog -------------------------------
		std::vector<ReleaseEntry> history = LoadHistory();
		history.insert(history.begin(), entry);
		SaveHistory(history);
		UpdateChangelog(entry);
		Step(report, std::string("Recorded in ") + HISTORY_FILE + " and " + CHANGELOG_FILE);

		// ------------------------------- 6. Siguiente version -------------------------------
		if (Project::HasActive())
		{
			ProjectInfo& project = Project::GetActive();
			switch (bumpAfter)
			{
			case VersionBump::Patch: project.version.BumpPatch(); break;
			case VersionBump::Minor: project.version.BumpMinor(); break;
			case VersionBump::Major: project.version.BumpMajor(); break;
			default: break;
			}
			project.lastModifiedAt = Project::Now();
			Project::SaveActive();
			report.nextVersion = project.version.ToString();
			if (bumpAfter != VersionBump::None) Step(report, "Project version is now " + report.nextVersion);
		}

		report.success = true;
		Step(report, "Release " + version + " created: " + entry.archive);
		return report;
	}
}
