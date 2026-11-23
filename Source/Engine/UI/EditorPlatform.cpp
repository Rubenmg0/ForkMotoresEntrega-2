// =====================================================================================
// Funciones del editor que dependen del sistema operativo. Van en su propio fichero para que
// <windows.h> (y sus macros) no se mezclen con el resto del editor.
// =====================================================================================
#include "ModuleUI.h"
#include "../FileSystem/FileSystem.h"
#include "../../Utils/Log.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#include <shellapi.h>
	#include <filesystem>
#endif

void ModuleUI::RevealInExplorer(const std::string& projectPath)
{
	const std::string diskPath = FileSystem::ToDiskPath(projectPath);

#ifdef _WIN32
	// Ruta absoluta con separadores de Windows, en UTF-16 (nombres con acentos, espacios...)
	std::error_code ec;
	std::filesystem::path path = std::filesystem::absolute(std::filesystem::u8path(diskPath), ec);
	if (ec) path = std::filesystem::u8path(diskPath);
	path = path.make_preferred();
	const std::wstring widePath = path.wstring();

	HINSTANCE result = nullptr;
	if (std::filesystem::is_directory(path, ec))
	{
		// Carpeta: se abre en el Explorador
		result = ShellExecuteW(nullptr, L"explore", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	else
	{
		// Fichero: se abre su carpeta con el fichero seleccionado (como "Show in Explorer" de Unity)
		const std::wstring arguments = L"/select,\"" + widePath + L"\"";
		result = ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
	}
	if (reinterpret_cast<INT_PTR>(result) <= 32)
	{
		LOG_ERROR("[Assets] Could not open '%s' in the Explorer (error %d)", diskPath.c_str(), static_cast<int>(reinterpret_cast<INT_PTR>(result)));
	}
#else
	const std::string folder = FileSystem::IsDirectory(projectPath) ? diskPath : FileSystem::ToDiskPath(FileSystem::GetParentPath(projectPath));
	const std::string url = "file://" + folder;
	if (!SDL_OpenURL(url.c_str())) LOG_ERROR("[Assets] Could not open '%s': %s", folder.c_str(), SDL_GetError());
#endif
}
