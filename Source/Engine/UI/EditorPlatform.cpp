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

std::string EditorPlatform::GetExecutablePath()
{
#ifdef _WIN32
	wchar_t exePath[MAX_PATH * 4] = {};
	const DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
	if (length == 0 || length >= std::size(exePath)) return std::string();
	std::string result = WideToUtf8(exePath);
	for (char& c : result) if (c == '\\') c = '/';
	return result;
#else
	const char* basePath = SDL_GetBasePath();
	return basePath ? std::string(basePath) + "MotorVideojuego" : std::string();
#endif
}

bool EditorPlatform::LaunchSelf(const std::vector<std::string>& argsUtf8)
{
	return LaunchProcess(GetExecutablePath(), argsUtf8, std::string());
}

bool EditorPlatform::LaunchProcess(const std::string& exePathUtf8, const std::vector<std::string>& argsUtf8, const std::string& workingDirUtf8)
{
#ifdef _WIN32
	const std::wstring exe = Utf8ToWide(exePathUtf8);
	std::wstring commandLine = L"\"" + exe + L"\"";
	for (const std::string& argument : argsUtf8) AppendQuotedArgument(commandLine, Utf8ToWide(argument));
	const std::wstring workingDir = Utf8ToWide(workingDirUtf8);

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
		workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &process))
	{
		LOG_ERROR("[Platform] Could not launch '%s' (error %lu)", exePathUtf8.c_str(), GetLastError());
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
#else
	(void)exePathUtf8; (void)argsUtf8; (void)workingDirUtf8;
	LOG_ERROR("[Platform] LaunchProcess is only implemented on Windows");
	return false;
#endif
}

int EditorPlatform::RunProcessAndWait(const std::string& exePathUtf8, const std::vector<std::string>& argsUtf8,
	const std::string& workingDirUtf8, std::string* outOutput)
{
#ifdef _WIN32
	const std::wstring exe = Utf8ToWide(exePathUtf8);
	std::wstring commandLine = L"\"" + exe + L"\"";
	for (const std::string& argument : argsUtf8) AppendQuotedArgument(commandLine, Utf8ToWide(argument));
	const std::wstring workingDir = Utf8ToWide(workingDirUtf8);

	// Tuberia para recoger stdout + stderr del proceso hijo
	SECURITY_ATTRIBUTES security = {};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE readPipe = nullptr, writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return -1;
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = writePipe;
	startup.hStdError = writePipe;
	startup.hStdInput = nullptr;
	PROCESS_INFORMATION process = {};
	const BOOL created = CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &process);
	CloseHandle(writePipe);
	if (!created)
	{
		LOG_ERROR("[Platform] Could not run '%s' (error %lu)", exePathUtf8.c_str(), GetLastError());
		CloseHandle(readPipe);
		return -1;
	}

	std::string output;
	char buffer[4096];
	DWORD read = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) output.append(buffer, read);
	CloseHandle(readPipe);

	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exitCode = static_cast<DWORD>(-1);
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	if (outOutput) *outOutput = output;
	return static_cast<int>(exitCode);
#else
	(void)exePathUtf8; (void)argsUtf8; (void)workingDirUtf8; (void)outOutput;
	return -1;
#endif
}

std::string EditorPlatform::FindSystemTool(const std::string& fileName)
{
#ifdef _WIN32
	wchar_t systemDir[MAX_PATH] = {};
	const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return std::string();
	std::error_code ec;
	const std::filesystem::path tool = std::filesystem::path(systemDir) / std::filesystem::u8path(fileName);
	if (!std::filesystem::is_regular_file(tool, ec)) return std::string();
	std::string result = WideToUtf8(tool.c_str());
	for (char& c : result) if (c == '\\') c = '/';
	return result;
#else
	(void)fileName;
	return std::string();
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
