#include "FileSystem.h"
#include "../../Utils/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
	// Carpeta raiz del proyecto (la que contiene Assets/). Se rellena en FileSystem::Init()
	std::string projectRoot = ".";

	// Convierte una ruta de std::filesystem a texto con separadores '/'.
	// En Windows la conversion puede fallar con caracteres raros: en ese caso devolvemos "".
	std::string PathToString(const fs::path& path)
	{
		try
		{
			return path.generic_string();
		}
		catch (...)
		{
			return std::string();
		}
	}

	bool DirectoryExistsOnDisk(const fs::path& path)
	{
		std::error_code ec;
		return fs::is_directory(path, ec);
	}

	// Comparacion sin distinguir mayusculas para ordenar el listado de carpetas
	bool LessCaseInsensitive(const std::string& a, const std::string& b)
	{
		return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
			[](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
	}
}

namespace FileSystem
{
	bool Init()
	{
		// Posibles ubicaciones de la carpeta raiz del proyecto, por orden de preferencia:
		//  1) El directorio de trabajo        (build final: el .exe esta junto a Assets/)
		//  2) El padre del directorio de trabajo (Visual Studio ejecuta desde la carpeta build/)
		//  3) La carpeta del ejecutable y sus padres (por si se lanza el .exe desde otro sitio)
		std::vector<fs::path> candidates = { fs::path("."), fs::path("..") };

		if (const char* basePath = SDL_GetBasePath())
		{
			try
			{
				// SDL devuelve la ruta en UTF-8
				const fs::path exeDir = fs::u8path(basePath);
				candidates.push_back(exeDir);
				candidates.push_back(exeDir / "..");
				candidates.push_back(exeDir / ".." / "..");
			}
			catch (...)
			{
				LOG_WARNING("[FileSystem] No se pudo interpretar la carpeta del ejecutable");
			}
		}

		bool found = false;
		for (const fs::path& candidate : candidates)
		{
			if (DirectoryExistsOnDisk(candidate / ASSETS_DIR))
			{
				std::string root = PathToString(candidate.lexically_normal());
				if (root.empty()) continue;
				if (root.size() > 1 && root.back() == '/') root.pop_back();
				projectRoot = root;
				found = true;
				break;
			}
		}

		if (!found)
		{
			// No hay carpeta Assets en ningun sitio: la creamos en el directorio de trabajo
			projectRoot = ".";
			LOG_WARNING("[FileSystem] No se encontro la carpeta Assets. Se crea una vacia en el directorio de trabajo.");
		}

		// Carpetas que el motor necesita siempre
		bool ok = CreateDirectories(ASSETS_DIR);
		ok &= CreateDirectories(SCENES_DIR);
		ok &= CreateDirectories(LIBRARY_MESHES_DIR);
		ok &= CreateDirectories(LIBRARY_TEXTURES_DIR);
		ok &= CreateDirectories(LIBRARY_MODELS_DIR);

		LOG("[FileSystem] Raiz del proyecto: '%s' (Assets: '%s', Library: '%s')",
			projectRoot.c_str(), ToDiskPath(ASSETS_DIR).c_str(), ToDiskPath(LIBRARY_DIR).c_str());
		return ok;
	}

	const std::string& GetProjectRoot()
	{
		return projectRoot;
	}

	std::string ToDiskPath(const std::string& projectPath)
	{
		if (projectRoot.empty() || projectRoot == ".") return projectPath;
		if (projectPath.empty()) return projectRoot;
		return projectRoot + "/" + projectPath;
	}

	std::string ToProjectPath(const std::string& diskPathUtf8)
	{
		std::error_code ec;
		fs::path inputPath;
		try
		{
			inputPath = fs::u8path(diskPathUtf8);
		}
		catch (...)
		{
			return std::string();
		}
		const fs::path absoluteFile = fs::weakly_canonical(inputPath, ec);
		if (ec) return std::string();
		const fs::path absoluteRoot = fs::weakly_canonical(fs::path(projectRoot), ec);
		if (ec) return std::string();

		const fs::path relative = absoluteFile.lexically_relative(absoluteRoot);
		const std::string result = PathToString(relative);

		// Si para llegar al fichero hay que "salir" de la raiz, no es un fichero del proyecto
		if (result.empty() || result == "." || result.rfind("..", 0) == 0) return std::string();
		return Normalize(result);
	}

	bool Exists(const std::string& projectPath)
	{
		std::error_code ec;
		return fs::exists(fs::path(ToDiskPath(projectPath)), ec);
	}

	bool IsDirectory(const std::string& projectPath)
	{
		return DirectoryExistsOnDisk(fs::path(ToDiskPath(projectPath)));
	}

	bool CreateDirectories(const std::string& projectPath)
	{
		std::error_code ec;
		const fs::path path(ToDiskPath(projectPath));
		if (fs::is_directory(path, ec)) return true;
		fs::create_directories(path, ec);
		if (ec)
		{
			LOG_ERROR("[FileSystem] No se pudo crear la carpeta '%s': %s", projectPath.c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

	bool RemoveFile(const std::string& projectPath)
	{
		std::error_code ec;
		const bool removed = fs::remove(fs::path(ToDiskPath(projectPath)), ec);
		if (ec)
		{
			LOG_ERROR("[FileSystem] No se pudo borrar '%s': %s", projectPath.c_str(), ec.message().c_str());
			return false;
		}
		return removed;
	}

	bool RemoveFolder(const std::string& projectPath)
	{
		std::error_code ec;
		fs::remove_all(fs::path(ToDiskPath(projectPath)), ec);
		if (ec)
		{
			LOG_ERROR("[FileSystem] No se pudo borrar la carpeta '%s': %s", projectPath.c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

	bool Rename(const std::string& fromProjectPath, const std::string& toProjectPath)
	{
		std::error_code ec;
		fs::rename(fs::path(ToDiskPath(fromProjectPath)), fs::path(ToDiskPath(toProjectPath)), ec);
		if (ec)
		{
			LOG_ERROR("[FileSystem] No se pudo renombrar '%s' a '%s': %s",
				fromProjectPath.c_str(), toProjectPath.c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

	bool CopyExternalFile(const std::string& diskPathUtf8, const std::string& projectPath)
	{
		CreateDirectories(GetParentPath(projectPath));

		std::error_code ec;
		try
		{
			fs::copy_file(fs::u8path(diskPathUtf8), fs::path(ToDiskPath(projectPath)), fs::copy_options::overwrite_existing, ec);
		}
		catch (...)
		{
			ec = std::make_error_code(std::errc::invalid_argument);
		}
		if (ec)
		{
			LOG_ERROR("[FileSystem] No se pudo copiar '%s' a '%s': %s", diskPathUtf8.c_str(), projectPath.c_str(), ec.message().c_str());
			return false;
		}
		return true;
	}

	bool ReadBinary(const std::string& projectPath, std::vector<char>& outData)
	{
		outData.clear();
		std::ifstream file(fs::path(ToDiskPath(projectPath)), std::ios::binary | std::ios::ate);
		if (!file.is_open()) return false;

		const std::streamsize size = file.tellg();
		if (size < 0) return false;
		file.seekg(0, std::ios::beg);

		outData.resize(static_cast<size_t>(size));
		if (size > 0 && !file.read(outData.data(), size))
		{
			outData.clear();
			return false;
		}
		return true;
	}

	bool WriteBinary(const std::string& projectPath, const void* data, size_t size)
	{
		CreateDirectories(GetParentPath(projectPath));

		std::ofstream file(fs::path(ToDiskPath(projectPath)), std::ios::binary | std::ios::trunc);
		if (!file.is_open())
		{
			LOG_ERROR("[FileSystem] No se pudo abrir '%s' para escribir", projectPath.c_str());
			return false;
		}
		if (size > 0)
		{
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		}
		return file.good();
	}

	bool ReadText(const std::string& projectPath, std::string& outText)
	{
		std::vector<char> data;
		if (!ReadBinary(projectPath, data)) return false;
		outText.assign(data.begin(), data.end());
		return true;
	}

	bool WriteText(const std::string& projectPath, const std::string& text)
	{
		return WriteBinary(projectPath, text.data(), text.size());
	}

	bool GetFileInfo(const std::string& projectPath, uint64_t& outSize, int64_t& outLastWrite)
	{
		std::error_code ec;
		const fs::path path(ToDiskPath(projectPath));

		const auto size = fs::file_size(path, ec);
		if (ec) return false;
		const auto time = fs::last_write_time(path, ec);
		if (ec) return false;

		outSize = static_cast<uint64_t>(size);
		outLastWrite = static_cast<int64_t>(time.time_since_epoch().count());
		return true;
	}

	std::vector<DirectoryEntry> ListDirectory(const std::string& projectPath)
	{
		std::vector<DirectoryEntry> entries;

		std::error_code ec;
		fs::directory_iterator it(fs::path(ToDiskPath(projectPath)), ec);
		if (ec) return entries;

		for (const fs::directory_entry& item : it)
		{
			const std::string name = PathToString(item.path().filename());
			if (name.empty()) continue; // nombre no representable: se ignora

			DirectoryEntry entry;
			entry.name = name;
			entry.path = Join(projectPath, name);
			std::error_code typeError;
			entry.isDirectory = item.is_directory(typeError);
			entries.push_back(std::move(entry));
		}

		std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b)
			{
				if (a.isDirectory != b.isDirectory) return a.isDirectory; // carpetas primero
				return LessCaseInsensitive(a.name, b.name);
			});
		return entries;
	}

	void ListFilesRecursive(const std::string& projectPath, std::vector<std::string>& outFiles)
	{
		const fs::path start(ToDiskPath(projectPath));
		std::error_code ec;
		fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, ec);
		if (ec) return;

		const fs::recursive_directory_iterator end;
		while (it != end)
		{
			std::error_code typeError;
			if (it->is_regular_file(typeError))
			{
				const std::string relative = PathToString(it->path().lexically_relative(start));
				if (!relative.empty())
				{
					outFiles.push_back(Join(projectPath, relative));
				}
			}
			it.increment(ec);
			if (ec) break;
		}
		std::sort(outFiles.begin(), outFiles.end());
	}

	std::string Normalize(const std::string& path)
	{
		std::string result;
		result.reserve(path.size());
		for (char c : path)
		{
			const char current = (c == '\\') ? '/' : c;
			// Evita "//" duplicados
			if (current == '/' && !result.empty() && result.back() == '/') continue;
			result.push_back(current);
		}
		while (result.rfind("./", 0) == 0) result.erase(0, 2);
		while (result.size() > 1 && result.back() == '/') result.pop_back();
		return result;
	}

	std::string Join(const std::string& a, const std::string& b)
	{
		if (a.empty()) return Normalize(b);
		if (b.empty()) return Normalize(a);
		return Normalize(a + "/" + b);
	}

	std::string GetFileName(const std::string& path)
	{
		const std::string normalized = Normalize(path);
		const size_t slash = normalized.find_last_of('/');
		return (slash == std::string::npos) ? normalized : normalized.substr(slash + 1);
	}

	std::string GetFileNameNoExtension(const std::string& path)
	{
		const std::string name = GetFileName(path);
		const size_t dot = name.find_last_of('.');
		return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
	}

	std::string GetExtensionLower(const std::string& path)
	{
		const std::string name = GetFileName(path);
		const size_t dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0) return std::string();
		return ToLower(name.substr(dot));
	}

	std::string GetParentPath(const std::string& path)
	{
		const std::string normalized = Normalize(path);
		const size_t slash = normalized.find_last_of('/');
		return (slash == std::string::npos) ? std::string() : normalized.substr(0, slash);
	}

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	bool IsInsideFolder(const std::string& path, const std::string& folder)
	{
		const std::string p = Normalize(path);
		const std::string f = Normalize(folder);
		if (p == f) return true;
		return p.size() > f.size() && p.compare(0, f.size(), f) == 0 && p[f.size()] == '/';
	}

	std::string MakeUniquePath(const std::string& projectPath)
	{
		if (!Exists(projectPath)) return projectPath;

		const std::string folder = GetParentPath(projectPath);
		const std::string stem = GetFileNameNoExtension(projectPath);
		const std::string name = GetFileName(projectPath);
		const size_t dot = name.find_last_of('.');
		const std::string extension = (dot == std::string::npos || dot == 0) ? std::string() : name.substr(dot);

		for (int i = 1; i < 10000; ++i)
		{
			const std::string candidate = Join(folder, stem + " (" + std::to_string(i) + ")" + extension);
			if (!Exists(candidate)) return candidate;
		}
		return projectPath;
	}
}
