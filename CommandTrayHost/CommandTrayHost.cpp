﻿#include "stdafx.h"
#include "CommandTrayHost.h"
#include "configure.h"
#include "language.h"
#include "cache.h"
#include "utils.hpp"

#ifndef __cplusplus
#undef NULL
#define NULL 0
extern WINBASEAPI HWND WINAPI GetConsoleWindow();
#else
extern "C" WINBASEAPI HWND WINAPI GetConsoleWindow();
#endif

nlohmann::json global_stat;
nlohmann::json* global_cache_configs_pointer;
nlohmann::json* global_configs_pointer;
nlohmann::json* global_left_click_pointer;
nlohmann::json* global_groups_pointer;
HANDLE ghJob;
HANDLE ghMutex;
HICON gHicon;
//WCHAR szHIcon[MAX_PATH * 2];
//int icon_size;
bool is_runas_admin;
bool enable_groups_menu;
bool enable_left_click;
bool enable_cache;
bool conform_cache_expire;
bool disable_cache_position;
bool disable_cache_size;
bool disable_cache_enabled;
bool disable_cache_show;
bool disable_cache_alpha;
bool start_show_silent;
// during loading configuration file in configure_reader
// is_cache_valid true means that content in command_tray_host.cache is valid
// after that, its false means need to flush cache out to disk
bool is_cache_valid;
int cache_config_cursor;
int number_of_configs;

int start_show_timer_tick_cnt;

bool repeat_mod_hotkey;
int global_hotkey_alpha_step;

TCHAR szPathToExe[MAX_PATH * 10];
TCHAR szPathToExeToken[MAX_PATH * 10];

CHAR locale_name[LOCALE_NAME_MAX_LENGTH];
BOOL isZHCN, isENUS;

HINSTANCE hInst;
HWND hWnd;
HWND hConsole;
HANDLE hProcessCommandTrayHost;
WCHAR szTitle[64] = L"";
WCHAR szWindowClass[36] = L"command-tray-host";
WCHAR szCommandLine[1024] = L"";
WCHAR szTooltip[512] = L"";
WCHAR szBalloon[512] = L"";
WCHAR szEnvironment[1024] = L"";
//WCHAR szProxyString[2048] = L"";
//CHAR szRasPbk[4096] = "";
//WCHAR* lpProxyList[8] = { 0 };
volatile DWORD dwChildrenPid;

// I don't know why this. There is a API.
//  DWORD WINAPI GetProcessId(
//    _In_ HANDLE Process
//  );

#ifdef _DEBUG
static DWORD MyGetProcessId(HANDLE hProcess)
{
	// https://gist.github.com/kusma/268888
	typedef DWORD(WINAPI *pfnGPI)(HANDLE);
	typedef ULONG(WINAPI *pfnNTQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);

	static int first = 1;
	static pfnGPI pfnGetProcessId;
	static pfnNTQIP ZwQueryInformationProcess;
	if (first)
	{
		first = 0;
		pfnGetProcessId = (pfnGPI)GetProcAddress(
			GetModuleHandleW(L"KERNEL32.DLL"), "GetProcessId");
		if (!pfnGetProcessId)
			ZwQueryInformationProcess = (pfnNTQIP)GetProcAddress(
				GetModuleHandleW(L"NTDLL.DLL"),
				"ZwQueryInformationProcess");
	}
	if (pfnGetProcessId)
		return pfnGetProcessId(hProcess);
	if (ZwQueryInformationProcess)
	{
		struct
		{
			PVOID Reserved1;
			PVOID PebBaseAddress;
			PVOID Reserved2[2];
			ULONG UniqueProcessId;
			PVOID Reserved3;
		} pbi;
		ZwQueryInformationProcess(hProcess, 0, &pbi, sizeof(pbi), 0);
		return pbi.UniqueProcessId;
	}
	return 0;
}
#endif

#ifdef _DEBUG2
static BOOL MyEndTask(DWORD pid)
{
	return _wsystem((L"taskkill /f /pid " + std::to_wstring(pid)).c_str());
	/*WCHAR szCmd[1024] = { 0 };
	StringCchPrintf(szCmd, ARRAYSIZE(szCmd), L"taskkill /f /pid %d", pid);
	//wsprintf(szCmd, L"taskkill /f /pid %d", pid);
	return _wsystem(szCmd) == 0;*/
}
#endif

BOOL ShowTrayIcon(LPCWSTR lpszProxy, DWORD dwMessage)
{
	LOGMESSAGE(L"%s %d", lpszProxy, dwMessage);
	NOTIFYICONDATA nid;
	ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
	nid.cbSize = (DWORD)sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = NID_UID;
	nid.uFlags = NIF_ICON | NIF_MESSAGE;
	nid.dwInfoFlags = NIIF_INFO;
	nid.uCallbackMessage = WM_TASKBARNOTIFY;
	HICON hIcon = NULL;
	/*if (szHIcon[0] != NULL)
	{
		LOGMESSAGE(L"ShowTrayIcon Load from file %s\n", szHIcon);
		hIcon = reinterpret_cast<HICON>(LoadImage(NULL,
			szHIcon,
			IMAGE_ICON,
			icon_size ? icon_size : 256,
			icon_size ? icon_size : 256,
			LR_LOADFROMFILE)
			);
		if (hIcon == NULL)
		{
			LOGMESSAGE(L"Load IMAGE_ICON failed!\n");
		}
	}*/
	hIcon = gHicon;
	if (hIcon == NULL && is_runas_admin)
	{
		GetStockIcon(hIcon);
	}

	nid.hIcon = (hIcon == NULL) ? LoadIcon(hInst, (LPCTSTR)IDI_SMALL) : hIcon;

	nid.uTimeout = 3 * 1000 | NOTIFYICON_VERSION;
	//lstrcpy(nid.szInfoTitle, szTitle);
	assert(sizeof(nid.szInfoTitle) == 64 * sizeof(WCHAR));
	//assert(1 == 2);
	assert(sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) == ARRAYSIZE(nid.szInfoTitle));
	StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), szTitle);
	if (lpszProxy)
	{
		nid.uFlags |= NIF_INFO | NIF_TIP;
		if (lstrlen(lpszProxy) > 0)
		{
			//lstrcpy(nid.szTip, lpszProxy);
			StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), lpszProxy);
			//lstrcpy(nid.szInfo, lpszProxy);
			StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), lpszProxy);
		}
		else
		{
			//lstrcpy(nid.szInfo, szBalloon);
			StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), szBalloon);
			//lstrcpy(nid.szTip, szTooltip);
			StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), szTooltip);
		}
	}
	Shell_NotifyIcon(dwMessage ? dwMessage : NIM_ADD, &nid);
	BOOL hSuccess = NULL;
	if (gHicon)
	{
		if (true == enable_left_click)
		{
			hSuccess = DestroyIcon(hIcon);
			gHicon = NULL;
		}
	}
	else if (hIcon)
	{
		hSuccess = DestroyIcon(hIcon);
	}
	if (NULL == hSuccess)
	{
		LOGMESSAGE(L"DestroyIcon Failed! %d\n", GetLastError());
	}
	/*
	if (hIcon)
	{
		BOOL hSuccess = DestroyIcon(hIcon);
		if (NULL == hSuccess)
		{
			LOGMESSAGE(L"DestroyIcon Failed! %d\n", GetLastError());
		}
	}*/
	return TRUE;
}

BOOL DeleteTrayIcon()
{
	NOTIFYICONDATA nid;
	nid.cbSize = (DWORD)sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = NID_UID;
	Shell_NotifyIcon(NIM_DELETE, &nid);
	return TRUE;
}

#ifdef _DEBUG2
LPCTSTR GetWindowsProxy()
{
	static WCHAR szProxy[1024] = { 0 };
	HKEY hKey;
	DWORD dwData = 0;
	DWORD dwSize = sizeof(DWORD);

	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
		0,
		KEY_READ | 0x0200,
		&hKey))
	{
		szProxy[0] = 0;
		//dwSize = sizeof(szProxy) / sizeof(szProxy[0]);
		dwSize = ARRAYSIZE(szProxy);
		RegQueryValueExW(hKey, L"AutoConfigURL", NULL, 0, (LPBYTE)&szProxy, &dwSize);
		if (wcslen(szProxy))
		{
			RegCloseKey(hKey);
			return szProxy;
		}
		dwData = 0;
		RegQueryValueExW(hKey, L"ProxyEnable", NULL, 0, (LPBYTE)&dwData, &dwSize);
		if (dwData == 0)
		{
			RegCloseKey(hKey);
			return L"";
		}
		szProxy[0] = 0;
		//dwSize = sizeof(szProxy) / sizeof(szProxy[0]);
		dwSize = ARRAYSIZE(szProxy);
		RegQueryValueExW(hKey, L"ProxyServer", NULL, 0, (LPBYTE)&szProxy, &dwSize);
		if (wcslen(szProxy))
		{
			RegCloseKey(hKey);
			return szProxy;
		}
	}
	return szProxy;
}


BOOL SetWindowsProxy(WCHAR* szProxy, const WCHAR* szProxyInterface)
{
	INTERNET_PER_CONN_OPTION_LIST conn_options;
	BOOL bReturn;
	DWORD dwBufferSize = sizeof(conn_options);

	if (wcslen(szProxy) == 0)
	{
		conn_options.dwSize = dwBufferSize;
		conn_options.pszConnection = (WCHAR*)szProxyInterface;
		conn_options.dwOptionCount = 1;
		conn_options.pOptions = (INTERNET_PER_CONN_OPTION*)malloc(
			sizeof(INTERNET_PER_CONN_OPTION) * conn_options.dwOptionCount);
		conn_options.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
		conn_options.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT;
	}
	else if (wcsstr(szProxy, L"://") != NULL)
	{
		conn_options.dwSize = dwBufferSize;
		conn_options.pszConnection = (WCHAR*)szProxyInterface;
		conn_options.dwOptionCount = 3;
		conn_options.pOptions = (INTERNET_PER_CONN_OPTION*)malloc(
			sizeof(INTERNET_PER_CONN_OPTION) * conn_options.dwOptionCount);
		conn_options.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
		conn_options.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_AUTO_PROXY_URL;
		conn_options.pOptions[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
		conn_options.pOptions[1].Value.pszValue = szProxy;
		conn_options.pOptions[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
		conn_options.pOptions[2].Value.pszValue = (LPWSTR)L"<local>";
	}
	else
	{
		conn_options.dwSize = dwBufferSize;
		conn_options.pszConnection = (WCHAR*)szProxyInterface;
		conn_options.dwOptionCount = 3;
		conn_options.pOptions = (INTERNET_PER_CONN_OPTION*)malloc(
			sizeof(INTERNET_PER_CONN_OPTION) * conn_options.dwOptionCount);
		conn_options.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
		conn_options.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;
		conn_options.pOptions[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
		conn_options.pOptions[1].Value.pszValue = szProxy;
		conn_options.pOptions[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
		conn_options.pOptions[2].Value.pszValue = (LPWSTR)L"<local>";
	}

	bReturn = InternetSetOption(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &conn_options, dwBufferSize);
	free(conn_options.pOptions);
	InternetSetOption(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
	InternetSetOption(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
	return bReturn;
}

//#pragma warning( push )
//#pragma warning( disable : 4996)
BOOL SetWindowsProxyForAllRasConnections(WCHAR* szProxy)
{
	for (LPCSTR lpRasPbk = szRasPbk; *lpRasPbk; lpRasPbk += strlen(lpRasPbk) + 1)
	{
		char szPath[2048] = "";
		//if (ExpandEnvironmentStringsA(lpRasPbk, szPath, sizeof(szPath) / sizeof(szPath[0])))
		if (ExpandEnvironmentStringsA(lpRasPbk, szPath, ARRAYSIZE(szPath)))
		{
			char line[2048] = "";
			size_t length = 0;
			//FILE* fp = fopen(szPath, "r");
			//if (fp != NULL)
			FILE* fp = NULL;
			if (0 == fopen_s(&fp, szPath, "r"))
			{
				while (!feof(fp))
				{
					//if (fgets(line, sizeof(line) / sizeof(line[0]) - 1, fp))
					if (fgets(line, ARRAYSIZE(line) - 1, fp))
					{
						length = strlen(line);
						if (length > 3 && line[0] == '[' && line[length - 2] == ']')
						{
							line[length - 2] = 0;
							WCHAR szSection[64] = L"";
							//MultiByteToWideChar(CP_UTF8, 0, line + 1, -1, szSection, sizeof(szSection) / sizeof(szSection[0]));
							MultiByteToWideChar(CP_UTF8, 0, line + 1, -1, szSection, ARRAYSIZE(szSection));
							SetWindowsProxy(szProxy, szSection);
						}
					}
				}
				fclose(fp);
			}
		}
	}
	return TRUE;
}
//#pragma warning( pop )
#endif

BOOL ShowPopupMenuJson4()
{
	POINT pt;
	HMENU hSubMenu = NULL;
	//const LCID cur_lcid = GetSystemDefaultLCID();
	//const BOOL isZHCN = cur_lcid == 2052;
	//LPCTSTR lpCurrentProxy = GetWindowsProxy();
	std::vector<HMENU> vctHmenu;
	get_command_submenu(vctHmenu);

	auto& menu_ref = global_stat["menu"];
	AppendMenu(vctHmenu[0], MF_SEPARATOR, NULL, NULL);
	//AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_HIDEALL, (isZHCN ? L"隐藏全部" : translate_w2w(L"Hide All").c_str()));
	AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_HIDEALL, utf8_to_wstring(menu_ref[mHideAll]).c_str());
	hSubMenu = CreatePopupMenu();
	/*AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_DISABLEALL, (isZHCN ? L"全部禁用" : translate_w2w(L"Disable All").c_str()));
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ENABLEALL, (isZHCN ? L"全部启动" : translate_w2w(L"Enable All").c_str()));
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_SHOWALL, (isZHCN ? L"全部显示" : translate_w2w(L"Show All").c_str()));
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_RESTARTALL, (isZHCN ? L"全部重启" : translate_w2w(L"Restart All").c_str()));
	AppendMenu(vctHmenu[0], MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), (isZHCN ? L"全部" : translate_w2w(L"All").c_str()));
	*/
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_DISABLEALL, utf8_to_wstring(menu_ref[mDisableAll]).c_str());
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ENABLEALL, utf8_to_wstring(menu_ref[mEnableAll]).c_str());
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_SHOWALL, utf8_to_wstring(menu_ref[mShowall]).c_str());
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_RESTARTALL, utf8_to_wstring(menu_ref[mRestartALL]).c_str());
	AppendMenu(vctHmenu[0], MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), utf8_to_wstring(menu_ref[mAll]).c_str());
	vctHmenu.push_back(hSubMenu);
	AppendMenu(vctHmenu[0], MF_SEPARATOR, NULL, NULL);

	UINT uFlags = IsMyProgramRegisteredForStartup(szPathToExeToken) ? (MF_STRING | MF_CHECKED) : (MF_STRING);
	//AppendMenu(vctHmenu[0], uFlags, WM_TASKBARNOTIFY_MENUITEM_STARTUP, (isZHCN ? L"开机启动" : translate_w2w(L"Start on Boot").c_str()));
	AppendMenu(vctHmenu[0], uFlags, WM_TASKBARNOTIFY_MENUITEM_STARTUP, utf8_to_wstring(menu_ref[mStartOnBoot]).c_str());
	{
		//AppendMenu(vctHmenu[0], is_runas_admin ? (MF_STRING | MF_CHECKED) : MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ELEVATE, (isZHCN ? L"提权" : translate_w2w(L"Elevate").c_str()));
		AppendMenu(vctHmenu[0], is_runas_admin ? (MF_STRING | MF_CHECKED) : MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ELEVATE, utf8_to_wstring(menu_ref[mElevate]).c_str());
		/*HICON shieldIcon;
		if (GetStockIcon(shieldIcon))
		{
		AppendMenu(vctHmenu[0], is_runas_admin ? (MF_BITMAP | MF_CHECKED) : MF_BITMAP, WM_TASKBARNOTIFY_MENUITEM_ELEVATE, reinterpret_cast<LPCTSTR>(BitmapFromIcon(shieldIcon)));
		}*/

	}
	//AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_SHOW, (isZHCN ? L"\x663e\x793a" : L"Show"));
	//AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_HIDE, (isZHCN ? L"\x9690\x85cf" : L"Hide"));
	//AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_RELOAD, (isZHCN ? L"\x91cd\x65b0\x8f7d\x5165" : L"Reload"));
	AppendMenu(vctHmenu[0], MF_SEPARATOR, NULL, NULL);

	hSubMenu = CreatePopupMenu();
	/*AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_OPENURL, (isZHCN ? L"主页" : translate_w2w(L"Home").c_str()));
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ABOUT, (isZHCN ? L"关于" : translate_w2w(L"About").c_str()));
	AppendMenu(vctHmenu[0], MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), (isZHCN ? L"帮助" : translate_w2w(L"Help").c_str()));
	*/
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_OPENURL, utf8_to_wstring(menu_ref[mHome]).c_str());
	AppendMenu(hSubMenu, MF_STRING, WM_TASKBARNOTIFY_MENUITEM_ABOUT, utf8_to_wstring(menu_ref[mAbout]).c_str());
	AppendMenu(vctHmenu[0], MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), utf8_to_wstring(menu_ref[mHelp]).c_str());
	vctHmenu.push_back(hSubMenu);

	AppendMenu(vctHmenu[0], MF_SEPARATOR, NULL, NULL);
	//AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_EXIT, (isZHCN ? L"\x9000\x51fa" : translate_w2w(L"Exit").c_str()));
	AppendMenu(vctHmenu[0], MF_STRING, WM_TASKBARNOTIFY_MENUITEM_EXIT, utf8_to_wstring(menu_ref[mExit]).c_str());

	GetCursorPos(&pt);
	TrackPopupMenu(vctHmenu[0], TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
	PostMessage(hWnd, WM_NULL, 0, 0);
	LOGMESSAGE(L"hWnd:0x%x\n", hWnd);

	for (auto it : vctHmenu)
	{
		if (it != NULL)
		{
			DestroyMenu(it);
		}
	}
	//free vctHmenu memory now
	return TRUE;
}

#ifdef _DEBUG2
//#pragma warning( push )
//#pragma warning( disable : 4996)
BOOL ParseProxyList()
{
	WCHAR* tmpProxyString = _wcsdup(szProxyString);
	//ExpandEnvironmentStrings(tmpProxyString, szProxyString, sizeof(szProxyString) / sizeof(szProxyString[0]));
	ExpandEnvironmentStrings(tmpProxyString, szProxyString, ARRAYSIZE(szProxyString));
	free(tmpProxyString);
	const WCHAR* sep = L"\n";
	//WCHAR* pos = _wcstok(szProxyString, sep);
	WCHAR* next_token = NULL;
	WCHAR* pos = wcstok_s(szProxyString, sep, &next_token);
	UINT i = 0;
	//lpProxyList[i++] = (LPWSTR)L"";
	lpProxyList[i++] = L"";
	//while (pos && i < sizeof(lpProxyList) / sizeof(lpProxyList[0]))
	while (pos && i < ARRAYSIZE(lpProxyList))
	{
		lpProxyList[i++] = pos;
		//pos = _wcstok(NULL, sep);
		pos = wcstok_s(nullptr, sep, &next_token);
	}
	lpProxyList[i] = 0;

	for (LPSTR ptr = szRasPbk; *ptr; ptr++)
	{
		if (*ptr == '\n')
		{
			*ptr++ = 0;
		}
	}
	return TRUE;
}
//#pragma warning( pop )
#endif

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPED | WS_SYSMENU,
		NULL, NULL, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}


BOOL CDCurrentDirectory()
{

	WCHAR* szPath = _wcsdup(szPathToExe);

	/*WCHAR szPath[4096] = L"";
	//GetModuleFileName(NULL, szPath, sizeof(szPath) / sizeof(szPath[0]) - 1);
	GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath));*/

	*wcsrchr(szPath, L'\\') = 0;
	SetCurrentDirectory(szPath);
	SetEnvironmentVariableW(L"CWD", szPath);
	LOGMESSAGE(L"CWD: %s\n", szPath);
	free(szPath);
	return TRUE;
}


//#pragma warning( push )
//#pragma warning( disable : 4996)
BOOL SetEenvironment()
{
	//LoadString(hInst, IDS_CMDLINE, szCommandLine, sizeof(szCommandLine) / sizeof(szCommandLine[0]) - 1);
	//LoadString(hInst, IDS_ENVIRONMENT, szEnvironment, sizeof(szEnvironment) / sizeof(szEnvironment[0]) - 1);
	//LoadString(hInst, IDS_PROXYLIST, szProxyString, sizeof(szProxyString) / sizeof(szEnvironment[0]) - 1);
	//LoadStringA(hInst, IDS_RASPBK, szRasPbk, sizeof(szRasPbk) / sizeof(szRasPbk[0]) - 1);

	LoadString(hInst, IDS_CMDLINE, szCommandLine, ARRAYSIZE(szCommandLine) - 1);
	LoadString(hInst, IDS_ENVIRONMENT, szEnvironment, ARRAYSIZE(szEnvironment) - 1);
	//LoadString(hInst, IDS_PROXYLIST, szProxyString, ARRAYSIZE(szProxyString) - 1);
	//LoadStringA(hInst, IDS_RASPBK, szRasPbk, ARRAYSIZE(szRasPbk) - 1);

	const wchar_t* sep = L"\n";
	wchar_t* pos = NULL;
	//WCHAR *token = wcstok(szEnvironment, sep);
	wchar_t* next_token = NULL;
	wchar_t* token = wcstok_s(szEnvironment, sep, &next_token);
	while (token != NULL)
	{
		if ((pos = wcschr(token, L'=')) != NULL)
		{
			*pos = 0;
			SetEnvironmentVariable(token, pos + 1);
			//wprintf(L"[%s] = [%s]\n", token, pos+1);
			LOGMESSAGE(L"[%s] = [%s]\n", token, pos + 1);
		}
		//token = wcstok(NULL, sep);
		token = wcstok_s(nullptr, sep, &next_token);
		LOGMESSAGE(L"loop token:%s\n", token);
	}
	LOGMESSAGE(L"Get out of loop!\n");

	//GetEnvironmentVariableW(L"TASKBAR_TITLE", szTitle, sizeof(szTitle) / sizeof(szTitle[0]) - 1);
	//GetEnvironmentVariableW(L"TASKBAR_TOOLTIP", szTooltip, sizeof(szTooltip) / sizeof(szTooltip[0]) - 1);
	//GetEnvironmentVariableW(L"TASKBAR_BALLOON", szBalloon, sizeof(szBalloon) / sizeof(szBalloon[0]) - 1);

	GetEnvironmentVariable(L"TASKBAR_TITLE", szTitle, ARRAYSIZE(szTitle) - 1);
	GetEnvironmentVariable(L"TASKBAR_TOOLTIP", szTooltip, ARRAYSIZE(szTooltip) - 1);
	GetEnvironmentVariable(L"TASKBAR_BALLOON", szBalloon, ARRAYSIZE(szBalloon) - 1);

	return TRUE;
}
//#pragma warning( pop )

#ifdef _DEBUG2
BOOL WINAPI ConsoleHandler(DWORD CEvent)
{
	switch (CEvent)
	{
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_CLOSE_EVENT:
		SendMessage(hWnd, WM_CLOSE, NULL, NULL);
		break;
	}
	return TRUE;
}

//#pragma warning( push )
//#pragma warning( disable : 4996)
BOOL CreateConsole()
{
	WCHAR szVisible[BUFSIZ] = L"";

	AllocConsole();
	//_wfreopen(L"CONIN$", L"r+t", stdin);
	//_wfreopen(L"CONOUT$", L"w+t", stdout);
	FILE* fp;
	_wfreopen_s(&fp, L"CONIN$", L"r+t", stdin);
	_wfreopen_s(&fp, L"CONOUT$", L"w+t", stdout);

	hConsole = GetConsoleWindow();
	ShowWindow(hConsole, SW_SHOW);

	if (GetEnvironmentVariableW(L"TASKBAR_VISIBLE", szVisible, BUFSIZ - 1) && szVisible[0] == L'0')
	{
		ShowWindow(hConsole, SW_HIDE);
	}
	else
	{
		SetForegroundWindow(hConsole);
	}

	if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE) == FALSE)
	{
		LOGMESSAGE(L"Unable to install handler!\n");
		return FALSE;
	}

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi))
	{
		COORD size = csbi.dwSize;
		if (size.Y < 2048)
		{
			size.Y = 2048;
			if (!SetConsoleScreenBufferSize(GetStdHandle(STD_ERROR_HANDLE), size))
			{
				LOGMESSAGE(L"Unable to set console screen buffer size!\n");
			}
		}
	}
	/*HICON hIcon = NULL;
	if (szHIcon[0] != NULL)
	{
		LOGMESSAGE(L"CreateConsole Load from file %s\n", szHIcon);
		hIcon = reinterpret_cast<HICON>(LoadImage(NULL,
			szHIcon, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED)
			);
		if (hIcon == NULL)
		{
			LOGMESSAGE(L"CreateConsole Load IMAGE_ICON failed!\n");
		}
	}*/
	if (gHicon)
	{
		//ChangeIcon(hIcon);
		//https://social.msdn.microsoft.com/Forums/vstudio/en-US/dee0ac69-4236-49aa-a2a2-0ac672147769/win32-c-how-do-i-change-the-window-icon-during-runtime?forum=vcgeneral
		SendMessage(hConsole, WM_SETICON, ICON_BIG, (LPARAM)gHicon);
		SendMessage(hConsole, WM_SETICON, ICON_SMALL, (LPARAM)gHicon);
	}

	return TRUE;
}
//#pragma warning( pop )
#endif

BOOL ExecCmdline()
{
	//SetWindowText(hConsole, szTitle);
	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	BOOL bRet = CreateProcess(NULL, szCommandLine, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi);
	if (bRet)
	{
#ifdef _DEBUG
		dwChildrenPid = MyGetProcessId(pi.hProcess);
		assert(dwChildrenPid == pi.dwProcessId);
		assert(dwChildrenPid == GetProcessId(pi.hProcess));
#else
		dwChildrenPid = GetProcessId(pi.hProcess);
#endif
		LOGMESSAGE(L"pid %d\n", dwChildrenPid);
		if (ghJob)
		{
			if (0 == AssignProcessToJobObject(ghJob, pi.hProcess))
			{
				msg_prompt(/*NULL,*/ L"Could not AssignProcessToObject", L"Error", MB_OK | MB_ICONERROR);
			}
		}
	}
	else
	{
#ifdef _DEBUG
		wprintf(L"ExecCmdline \"%s\" failed!\n", szCommandLine);
#endif
		msg_prompt(/*NULL,*/ szCommandLine, L"Error: Cannot execute!", MB_OK | MB_ICONERROR);
		ExitProcess(0);
	}
	CloseHandle(pi.hThread);
	//CloseHandle(pi.hProcess);
	hProcessCommandTrayHost = pi.hProcess;
	return TRUE;
}

#ifdef _DEBUG2
BOOL TryDeleteUpdateFiles()
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;

	hFind = FindFirstFile(L"~*.tmp", &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		LOGMESSAGE(L"No FindFirstFile\n");
		return TRUE;
	}

	do
	{
		LOGMESSAGE(L"%s", FindFileData.cFileName);
		DeleteFile(FindFileData.cFileName);
		if (!FindNextFile(hFind, &FindFileData))
		{
			break;
		}
	} while (TRUE);
	FindClose(hFind);

	return TRUE;
}

BOOL ReloadCmdline()
{
	//HANDLE hProcess = OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE, FALSE, dwChildrenPid);
	//if (hProcess)
	//{
	//	TerminateProcess(hProcess, 0);
	//}
	ShowWindow(hConsole, SW_SHOW);
	SetForegroundWindow(hConsole);
#ifdef _DEBUG
	wprintf(L"\n\n");
#endif
	MyEndTask(dwChildrenPid);
#ifdef _DEBUG
	wprintf(L"\n\n");
#endif
	Sleep(200);
	ExecCmdline();
	return TRUE;
}

#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static UINT WM_TASKBARCREATED = 0;
	if (WM_TASKBARCREATED == 0)
		WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");

	UINT nID;
	switch (message)
	{
	case WM_TASKBARNOTIFY:
		if (lParam == WM_LBUTTONUP)
		{
			LOGMESSAGE(L"WM_TASKBARNOTIFY\n");
			if (enable_left_click)
			{
				left_click_toggle();
			}
			else
			{
				if (hConsole == 0)
				{
					size_t num_of_windows_;
					hConsole = GetHwnd(hProcessCommandTrayHost, num_of_windows_);
					if (hConsole)
					{
						CloseHandle(hProcessCommandTrayHost);
						if (gHicon)set_wnd_icon(hConsole, gHicon);
					}
				}
				if (hConsole)
				{
					ShowWindow(hConsole, !IsWindowVisible(hConsole));
					SetForegroundWindow(hConsole);
				}
			}
		}
		else if (lParam == WM_RBUTTONUP)
		{
			// https://msdn.microsoft.com/en-us/library/windows/desktop/ms648002(v=vs.85).aspx
			SetForegroundWindow(hWnd);
			ShowPopupMenuJson4();
		}
		break;
	case WM_COMMAND:
		nID = LOWORD(wParam);
		/*if (nID == WM_TASKBARNOTIFY_MENUITEM_SHOW)
		{
			ShowWindow(hConsole, SW_SHOW);
			SetForegroundWindow(hConsole);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_HIDE)
		{
			ShowWindow(hConsole, SW_HIDE);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_RELOAD)
		{
			ReloadCmdline();
		}
		else */
		if (nID == WM_TASKBARNOTIFY_MENUITEM_STARTUP)
		{
			if (IsMyProgramRegisteredForStartup(szPathToExeToken))
			{
				DisableStartUp();
			}
			else
			{
				EnableStartup();
			}
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_ELEVATE)
		{
			ElevateNow();
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_OPENURL)
		{
			ShellExecute(NULL, L"open", L"https://github.com/rexdf/CommandTrayHost", NULL, NULL, SW_SHOWMAXIMIZED);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_ABOUT)
		{
			PCWSTR msg = (isZHCN) ?
				(L"CommandTrayHost\n\n" L"版本: " VERSION_NUMS L"\n\n作者: rexdf" L"\n\n编译时间: " BUILD_TIME_CN) :
				(L"CommandTrayHost\n\n" L"Version: " VERSION_NUMS L"\n\nAuthor: rexdf" L"\n\nBuild Timestamp: " BUILD_TIME_EN);

			MessageBox(hWnd, msg, isZHCN ? L"关于" : translate_w2w(L"About").c_str(), 0);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_HIDEALL)
		{
			hideshow_all();
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_DISABLEALL)
		{
			kill_all(false);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_ENABLEALL)
		{
			start_all(ghJob, true);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_SHOWALL)
		{
			hideshow_all(false);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_RESTARTALL)
		{
			restart_all(ghJob);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_EXIT)
		{
			/*kill_all(global_stat);
			DeleteTrayIcon();
			delete_lockfile();*/
			//CLEANUP_BEFORE_QUIT(2);
			//PostMessage(hConsole, WM_CLOSE, 0, 0);
			PostMessage(hWnd, WM_CLOSE, 0, 0);
		}
		/*else if (WM_TASKBARNOTIFY_MENUITEM_PROXYLIST_BASE <= nID && nID <= WM_TASKBARNOTIFY_MENUITEM_PROXYLIST_BASE + ARRAYSIZE(
			lpProxyList))
		{
			WCHAR* szProxy = lpProxyList[nID - WM_TASKBARNOTIFY_MENUITEM_PROXYLIST_BASE];
			SetWindowsProxy(szProxy, NULL);
			SetWindowsProxyForAllRasConnections(szProxy);
			ShowTrayIcon(szProxy, NIM_MODIFY);
		}*/
		else if (WM_TASKBARNOTIFY_MENUITEM_COMMAND_BASE <= nID && nID < WM_APP_END)
		{
			int menu_idx = (nID - WM_TASKBARNOTIFY_MENUITEM_COMMAND_BASE) / 0x10;
			int submenu_idx = (nID - WM_TASKBARNOTIFY_MENUITEM_COMMAND_BASE) % 0x10;
			LOGMESSAGE(L"%x Clicked. %d %d\n", nID, menu_idx, submenu_idx);
			nlohmann::json& js = (*global_configs_pointer)[menu_idx];
			cache_config_cursor = menu_idx;
			if (submenu_idx < 3)
			{
				show_hide_toggle(js);
			}
			else if (submenu_idx == 3)
			{
				disable_enable_menu(js, ghJob);
			}
			else if (submenu_idx == 4)
			{
				create_process(js, ghJob);
			}
			else if (submenu_idx == 5)
			{
				//if (!is_runas_admin)  //comment out to just let it go
				{
					disable_enable_menu(js, ghJob, true);
				}
			}
			if (enable_cache && false == is_cache_valid)
			{
				/*static int cache_write_cnt = 0;
				if (cache_write_cnt < 10)
				{
					cache_write_cnt++;
				}
				else*/
				{
					//cache_write_cnt = 0;
					flush_cache();
				}
			}
		}
		else
		{
			LOGMESSAGE(L"%x Clicked\n", nID);
		}
		break;
	case WM_HOTKEY:
		//SendMessageCallback(hWnd,)
		nID = LOWORD(wParam);
		if (nID == WM_HOTKEY_LEFT_CLICK)
		{
			SendMessage(hWnd, WM_TASKBARNOTIFY, NULL, WM_LBUTTONUP);
		}
		else if (nID == WM_HOTKEY_RIGHT_CLICK)
		{
			SendMessage(hWnd, WM_TASKBARNOTIFY, NULL, WM_RBUTTONUP);
		}
		else if (nID == WM_TASKBARNOTIFY_MENUITEM_EXIT)
		{
			//PostMessage(hConsole, WM_CLOSE, 0, 0);
			PostMessage(hWnd, WM_CLOSE, 0, 0);
		}
		else if (WM_TASKBARNOTIFY_MENUITEM_ELEVATE <= nID && nID <= WM_TASKBARNOTIFY_MENUITEM_RESTARTALL)
		{
			SendMessage(hWnd, WM_COMMAND, nID, NULL);
		}
		else if (WM_TASKBARNOTIFY_MENUITEM_COMMAND_BASE <= nID && nID <= WM_APP_END)
		{
			LOGMESSAGE(L"nId:0x%x\n", nID);
			SendMessage(hWnd, WM_COMMAND, nID, NULL);
		}
		else if (WM_HOTKEY_ADD_ALPHA <= nID && nID <= WM_HOTKEY_TOPMOST)
		{
			HWND cur_hwnd = GetForegroundWindow();
			LOGMESSAGE(L"cur_hwnd:0x%x\n", cur_hwnd);
			if (cur_hwnd)
			{
				DWORD dwExStyle = GetWindowLong(cur_hwnd, GWL_EXSTYLE);
				LOGMESSAGE(L"dwExStyle:0x%x\n", dwExStyle);
				if (nID == WM_HOTKEY_ADD_ALPHA || nID == WM_HOTKEY_MINUS_ALPHA) {
					BYTE alpha;
					if (0 == (dwExStyle | WS_EX_LAYERED))
					{
						SetWindowLong(cur_hwnd, GWL_EXSTYLE, dwExStyle | WS_EX_LAYERED);
					}
					if (GetLayeredWindowAttributes(cur_hwnd, NULL, &alpha, NULL))
					{
						if (0 == (dwExStyle & WS_EX_LAYERED) && alpha == 0)alpha = 255;
						if (nID == WM_HOTKEY_ADD_ALPHA)
						{
							if (alpha < 255 - global_hotkey_alpha_step)alpha += global_hotkey_alpha_step;
							else alpha = 255;
						}
						else if (nID == WM_HOTKEY_MINUS_ALPHA)
						{
							if (alpha > global_hotkey_alpha_step)alpha -= global_hotkey_alpha_step;
							else alpha = 0;
						}
						LOGMESSAGE(L"alpha:%d\n", alpha);
						//SetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
						set_wnd_alpha(cur_hwnd, alpha);
					}
				}
				else if (nID == WM_HOTKEY_TOPMOST)
				{
					LOGMESSAGE(L"(dwExStyle & WS_EX_TOPMOST):%d\n", (dwExStyle & WS_EX_TOPMOST));
					//set_wnd_pos(cur_hwnd, 0, 0, 0, 0, WS_EX_TOPMOST != (dwExStyle & WS_EX_TOPMOST), false, false);
					/*SetWindowLong(
						cur_hwnd,
						GWL_EXSTYLE,
						(dwExStyle & WS_EX_TOPMOST) ? (dwExStyle - WS_EX_LAYERED) : (dwExStyle | WS_EX_LAYERED)
					);*/
					SetWindowPos(cur_hwnd,
						(dwExStyle & WS_EX_TOPMOST) ? HWND_NOTOPMOST : HWND_TOPMOST,
						0,
						0,
						0,
						0,
						SWP_NOSIZE | SWP_NOMOVE | SWP_ASYNCWINDOWPOS
					);
				}
			}

		}
		else
		{
			LOGMESSAGE(L"unkown key pressed, id:%d=0x%x\n", nID, nID);
		}

		break;
	case WM_TIMER:
		LOGMESSAGE(L"WM_TIMER tick %d\n", wParam);
		if (wParam == VM_TIMER_CREATEPROCESS_SHOW)
		{
			update_hwnd_all();

		}
		if (VM_TIMER_BASE <= wParam && wParam <= 0xBF00)
		{
			int idx = static_cast<int>(wParam - VM_TIMER_BASE);
			if (idx >= number_of_configs || idx < 0)
			{
				msg_prompt(L"Crontab has some fatal error unknown idx! Please report this windows screenshot to author!",
					L"Crontab Error",
					MB_OK
				);
			}
			else
			{
				handle_crontab(idx);
			}
		}
		else
		{
			//default:
			//LOGMESSAGE(L"WM_TIMER tick %d\n", wParam);
		}
		break;
	case WM_CLOSE:
		/*delete_lockfile();
		kill_all(global_stat);
		DeleteTrayIcon();*/
		CLEANUP_BEFORE_QUIT(3);
		PostQuitMessage(0);
		break;
	case WM_DESTROY:
		CLEANUP_BEFORE_QUIT(4);
		PostQuitMessage(0);
		break;
	default:
		if (message == WM_TASKBARCREATED)
		{
			ShowTrayIcon(NULL, NIM_ADD);
			break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = (WNDPROC)WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	/*HICON hIcon = NULL, hIconSm = NULL;
	if (szHIcon[0] != NULL)
	{
		LOGMESSAGE(L"MyRegisterClass Load from file %s\n", szHIcon);
		hIcon = reinterpret_cast<HICON>(LoadImage(NULL,
			szHIcon, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED)
			);
		if (hIcon == NULL)
		{
			LOGMESSAGE(L"MyRegisterClass Load IMAGE_ICON failed!\n");
		}
		hIconSm = reinterpret_cast<HICON>(LoadImage(NULL,
			szHIcon, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED)
			);
		if (hIconSm == NULL)
		{
			LOGMESSAGE(L"MyRegisterClass Load hIconSm IMAGE_ICON failed!\n");
		}
		if (hIconSm && hIcon)
		{
			LOGMESSAGE(L"MyRegisterClass icon load ok!\n");
		}
	}*/
	//wcex.hIcon = (hIcon == NULL) ? LoadIcon(hInstance, (LPCTSTR)IDI_TASKBAR) : hIcon;
	wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_TASKBAR);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = (LPCTSTR)NULL;
	wcex.lpszClassName = szWindowClass;
	//wcex.hIconSm = (hIconSm == NULL) ? LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL) : hIconSm;
	wcex.hIconSm = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

	ATOM ret = RegisterClassEx(&wcex);
	/*if (hIcon)
	{
		DestroyIcon(hIcon);
	}
	if (hIconSm)
	{
		DestroyIcon(hIconSm);
	}*/
	return ret;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	hInst = hInstance;
	is_runas_admin = check_runas_admin();
	if (is_runas_admin)
	{
		Sleep(100); // Wait for self Elevate to cleanup.
	}
	if (!init_cth_path())
	{
		msg_prompt(/*NULL,*/ L"Initialization CommandTrayHost Path failed!", L"Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	CDCurrentDirectory();
	makeSingleInstance3();
	SetEenvironment();
	//ParseProxyList();

	MyRegisterClass(hInstance);
	if (!InitInstance(hInstance, SW_HIDE))
	{
		return FALSE;
	}

	//if (NULL == init_global(ghJob, szHIcon, icon_size))
	if (NULL == init_global(ghJob, gHicon))
	{
		msg_prompt(/*NULL, */L"Initialization failed!", L"Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	check_admin(is_runas_admin);
	//initialize_local();
	//
	start_all(ghJob);
	//CreateConsole();
	if (!enable_left_click)ExecCmdline();
	//ShowTrayIcon(GetWindowsProxy(), NIM_ADD);
	ShowTrayIcon(L"", NIM_ADD);
	//TryDeleteUpdateFiles();
#ifdef _DEBUG
	{
		cron_expr expr;
		ZeroMemory(&expr, sizeof(expr));
		const char* err = NULL;
		cron_parse_expr("8 */2 15-16 29 2 *", &expr, &err);
		if (err)LOGMESSAGE(L"cron_parse_expr err: %S\n", err);
		else LOGMESSAGE(L"cron_parse_expr ok!\n");
		assert(0 == err);
		time_t cur = time(NULL);
		time_t next = cron_next(&expr, cur);

		LOGMESSAGE(L"%lld -> %lld diff:%lld", next, cur, next - cur);
		double dif = difftime(next, cur);
		char buffer[80];
		tm t1, t2;
		localtime_s(&t1, &cur);
		localtime_s(&t2, &next);
		strftime(buffer, ARRAYSIZE(buffer), "%Y-%m-%d_%H:%M:%S", &t1);
		LOGMESSAGE(L"t1:%S %f\n", buffer, dif);
		strftime(buffer, ARRAYSIZE(buffer), "%Y-%m-%d_%H:%M:%S", &t2);
		LOGMESSAGE(L"t2:%S %f\n", buffer, dif);
		LOGMESSAGE(L"%lld\n", ((time_t)-1));
		LOGMESSAGE(L"ARRAYSIZE %d\n", ARRAYSIZE("start") - 1);
	}
#endif
	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (!is_cache_valid)
	{
		flush_cache(/*true*/);
	}
#ifdef _DEBUG
	std::ofstream o("before_quit.txt");
	o << "ok!" << std::endl;
#endif
	return 0;// msg.wParam;
}
