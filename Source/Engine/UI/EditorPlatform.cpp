// =====================================================================================
// Funciones del editor que dependen del sistema operativo. Van en su propio fichero para que
// <windows.h> (y sus macros) no se mezclen con el resto del editor.
// =====================================================================================
#include "EditorPlatform.h"
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
#endif
#include <filesystem>

void ModuleUI::RevealInExplorer(const std::string& projectPath)
{
	EditorPlatform::RevealInExplorer(FileSystem::ToDiskPath(projectPath));
}

#ifdef _WIN32
namespace
{
	std::wstring Utf8ToWide(const std::string& text)
	{
		if (text.empty()) return std::wstring();
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
		return result;
	}

	std::string WideToUtf8(const wchar_t* text)
	{
		if (text == nullptr || *text == 0) return std::string();
		const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(length > 0 ? length - 1 : 0), '\0');
		if (length > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
		return result;
	}

	// Anade un argumento a una linea de comandos de Windows con las comillas y escapes correctos
	void AppendQuotedArgument(std::wstring& commandLine, const std::wstring& argument)
	{
		commandLine += L" \"";
		size_t backslashes = 0;
		for (wchar_t c : argument)
		{
			if (c == L'\\') { ++backslashes; continue; }
			if (c == L'"') commandLine.append(backslashes * 2 + 1, L'\\');
			else commandLine.append(backslashes, L'\\');
			backslashes = 0;
			commandLine += c;
		}
		commandLine.append(backslashes * 2, L'\\');
		commandLine += L'"';
	}
}
#endif

void EditorPlatform::RevealInExplorer(const std::string& diskPath)
{
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
	std::error_code ec;
	const std::filesystem::path path = std::filesystem::u8path(diskPath);
	const std::string folder = std::filesystem::is_directory(path, ec) ? diskPath : path.parent_path().u8string();
	const std::string url = "file://" + folder;
	if (!SDL_OpenURL(url.c_str())) LOG_ERROR("[Assets] Could not open '%s': %s", folder.c_str(), SDL_GetError());
#endif
}

bool EditorPlatform::LaunchSelf(const std::vector<std::string>& argsUtf8)
{
#ifdef _WIN32
	wchar_t exePath[MAX_PATH * 4] = {};
	const DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
	if (length == 0 || length >= std::size(exePath))
	{
		LOG_ERROR("[Platform] Could not get the executable path");
		return false;
	}

	std::wstring commandLine = L"\"" + std::wstring(exePath) + L"\"";
	for (const std::string& argument : argsUtf8) AppendQuotedArgument(commandLine, Utf8ToWide(argument));

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(exePath, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
	{
		LOG_ERROR("[Platform] Could not launch a new instance (error %lu)", GetLastError());
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
#else
	(void)argsUtf8;
	LOG_ERROR("[Platform] LaunchSelf is only implemented on Windows");
	return false;
#endif
}

std::vector<std::string> EditorPlatform::GetCommandLineArgs(int argc, char* argv[])
{
	std::vector<std::string> result;
#ifdef _WIN32
	int count = 0;
	if (LPWSTR* wideArgs = CommandLineToArgvW(GetCommandLineW(), &count))
	{
		for (int i = 1; i < count; ++i) result.push_back(WideToUtf8(wideArgs[i]));
		LocalFree(wideArgs);
		return result;
	}
#endif
	for (int i = 1; i < argc; ++i) result.push_back(argv[i] ? argv[i] : "");
	return result;
}
