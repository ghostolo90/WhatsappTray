// ****************************************************************************
// 
// WhatsappTray
// Copyright (C) 1998-2016  Sebastian Amann, Nikolay Redko, J.D. Purcell
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// 
// ****************************************************************************

#include <windows.h>
#include "WhatsappTray.h"
#include "resource.h"
#include <string>
#include <fstream>

#define MAXTRAYITEMS 64

static HINSTANCE _hInstance;
static HMODULE _hLib;
static HWND _hwndHook;
static HWND _hwndItems[MAXTRAYITEMS];
static HWND _hwndForMenu;
static std::wstring _filepath;

HMODULE GetCurrentModule()
{ // NB: XP+ solution!
	HMODULE hModule = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)GetCurrentModule, &hModule);
	return hModule;
}

// Da es nur sehr schwer ist, Daten in die Dll zu bekommen lese ich den Pfad jetzt direkt in der Dll aus.
void GetPath()
{
	wchar_t filepathArray[FILEPATH_MAX_LENGTH];
	GetModuleFileName(GetCurrentModule(), filepathArray, FILEPATH_MAX_LENGTH);

	_filepath = std::wstring(filepathArray);
	size_t cutpos = _filepath.find_last_of(L'\\', _filepath.length());
	_filepath = _filepath.substr(0, cutpos + 1);

	_filepath.append(L"WT_log.txt");
}

int FindInTray(HWND hwnd) {
	for (int i = 0; i < MAXTRAYITEMS; i++) {
		if (_hwndItems[i] == hwnd) return i;
	}
	return -1;
}

HICON GetWindowIcon(HWND hwnd) {
	HICON icon;
	if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0)) return icon;
	if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0)) return icon;
	if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM)) return icon;
	if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON)) return icon;
	return LoadIcon(NULL, IDI_WINLOGO);
}

static void AddToTray(int i) {
	NOTIFYICONDATA nid;
	ZeroMemory(&nid, sizeof(nid));
	nid.cbSize           = NOTIFYICONDATA_V2_SIZE;
	nid.hWnd             = _hwndHook;
	nid.uID              = (UINT)i;
	nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYCMD;
	nid.hIcon            = GetWindowIcon(_hwndItems[i]);
	GetWindowText(_hwndItems[i], nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
	nid.uVersion         = NOTIFYICON_VERSION;
	Shell_NotifyIcon(NIM_ADD, &nid);
	Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

static void AddWindowToTray(HWND hwnd) {
	int i = FindInTray(NULL);
	if (i == -1) return;
	_hwndItems[i] = hwnd;
	AddToTray(i);
}

static void MinimizeWindowToTray(HWND hwnd) {
	// Don't minimize MDI child windows
	if ((UINT)GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_MDICHILD) return;

	// If hwnd is a child window, find parent window (e.g. minimize button in
	// Office 2007 (ribbon interface) is in a child window)
	if ((UINT)GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) {
		hwnd = GetAncestor(hwnd, GA_ROOT);
	}

	// Add icon to tray if it's not already there
	if (FindInTray(hwnd) == -1) {
		AddWindowToTray(hwnd);
	}

	// Hide window
	ShowWindow(hwnd, SW_HIDE);
}

static void RemoveFromTray(int i) {
	NOTIFYICONDATA nid;
	ZeroMemory(&nid, sizeof(nid));
	nid.cbSize = NOTIFYICONDATA_V2_SIZE;
	nid.hWnd   = _hwndHook;
	nid.uID    = (UINT)i;
	Shell_NotifyIcon(NIM_DELETE, &nid);
}

static void RemoveWindowFromTray(HWND hwnd) {
	int i = FindInTray(hwnd);
	if (i == -1) return;
	RemoveFromTray(i);
	_hwndItems[i] = 0;
}

static void RestoreWindowFromTray(HWND hwnd) {
	ShowWindow(hwnd, SW_RESTORE);
	SetForegroundWindow(hwnd);
	RemoveWindowFromTray(hwnd);

}

static void CloseWindowFromTray(HWND hwnd) {
	// Use PostMessage to avoid blocking if the program brings up a dialog on exit.
	// Also, Explorer windows ignore WM_CLOSE messages from SendMessage.
	PostMessage(hwnd, WM_CLOSE, 0, 0);

	Sleep(50);
	if (IsWindow(hwnd)) Sleep(50);

	if (!IsWindow(hwnd)) {
		// Closed successfully
		RemoveWindowFromTray(hwnd);
	}
}

void RefreshWindowInTray(HWND hwnd) {
	int i = FindInTray(hwnd);
	if (i == -1) return;
	if (!IsWindow(hwnd) || IsWindowVisible(hwnd)) {
		RemoveWindowFromTray(hwnd);
	}
	else {
		NOTIFYICONDATA nid;
		ZeroMemory(&nid, sizeof(nid));
		nid.cbSize = NOTIFYICONDATA_V2_SIZE;
		nid.hWnd   = _hwndHook;
		nid.uID    = (UINT)i;
		nid.uFlags = NIF_TIP;
		GetWindowText(hwnd, nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
		Shell_NotifyIcon(NIM_MODIFY, &nid);
	}
}

void ExecuteMenu() {
	HMENU hMenu;
	POINT point;

	hMenu = CreatePopupMenu();
	if (!hMenu) {
		MessageBox(NULL, L"Error creating menu.", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return;
	}
	AppendMenu(hMenu, MF_STRING, IDM_ABOUT,   L"About WhatsappTray");
	AppendMenu(hMenu, MF_STRING, IDM_EXIT,    L"Exit WhatsappTray");
	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); //--------------
	AppendMenu(hMenu, MF_STRING, IDM_CLOSE,   L"Close Window");
	AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"Restore Window");

	GetCursorPos(&point);
	SetForegroundWindow(_hwndHook);

	TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, _hwndHook, NULL);

	PostMessage(_hwndHook, WM_USER, 0, 0);
	DestroyMenu(hMenu);
}

BOOL CALLBACK AboutDlgProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
	switch (Msg) {
		case WM_CLOSE:
			PostMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
			break;
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDOK:
					EndDialog(hWnd, TRUE);
					break;
				case IDCANCEL:
					EndDialog(hWnd, FALSE);
					break;
			}
			break;
		default:
			return FALSE;
	}
	return TRUE;
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg)
	{
		OutputDebugString(L"HookWndProc() - Message Received\n");

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDM_RESTORE:
					RestoreWindowFromTray(_hwndForMenu);
					break;
				case IDM_CLOSE:
					CloseWindowFromTray(_hwndForMenu);
					break;
				case IDM_ABOUT:
					DialogBox(_hInstance, MAKEINTRESOURCE(IDD_ABOUT), _hwndHook, (DLGPROC)AboutDlgProc);
					break;
				case IDM_EXIT:
					SendMessage(_hwndHook, WM_DESTROY, 0, 0);
					break;
			}
			break;
		case WM_ADDTRAY:
			//MessageBox(NULL, L"HookWndProc() WM_ADDTRAY", L"WhatsappTray", MB_OK | MB_ICONINFORMATION);

			MinimizeWindowToTray((HWND)lParam);
			break;
		case WM_REMTRAY:
			RestoreWindowFromTray((HWND)lParam);
			break;
		case WM_REFRTRAY:
			RefreshWindowInTray((HWND)lParam);
			break;
		case WM_TRAYCMD:
			switch ((UINT)lParam) {
				case NIN_SELECT:
					RestoreWindowFromTray(_hwndItems[wParam]);
					break;
				case WM_CONTEXTMENU:
					_hwndForMenu = _hwndItems[wParam];
					ExecuteMenu();
					break;
				case WM_MOUSEMOVE:
					RefreshWindowInTray(_hwndItems[wParam]);
					break;
			}
			break;
		case WM_DESTROY:
			MessageBox(NULL, L"HookWndProc() WM_DESTROY", L"WhatsappTray", MB_OK | MB_ICONINFORMATION);
			for (int i = 0; i < MAXTRAYITEMS; i++) {
				if (_hwndItems[i]) {
					RestoreWindowFromTray(_hwndItems[i]);
				}
			}
			UnRegisterHook();
			FreeLibrary(_hLib);
			PostQuitMessage(0);
			break;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// TODO(niedrige Prio): Manifestdatei, damit "Operating System context" nicht Windows Vista ist. Den Kontext kann ich mit dem Taskmanager anschaun
// http://stackoverflow.com/questions/15808967/application-running-in-windows-vista-context-by-default
// http://stackoverflow.com/questions/28347039/how-do-i-get-manifest-file-to-generate-in-visual-studio-2013
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	WNDCLASS wc;
	MSG msg;

	_hInstance = hInstance;
	_hwndHook = FindWindow(NAME, NAME);
	if (_hwndHook)
	{
		if (strstr(szCmdLine, "--exit"))
		{
			SendMessage(_hwndHook, WM_CLOSE, 0, 0);
		}
		else
		{
			MessageBox(NULL, L"WhatsappTray is already running.", L"WhatsappTray", MB_OK | MB_ICONINFORMATION);
		}
		return 0;
	}

	GetPath();

	std::wofstream logfile;
	logfile.open(_filepath.c_str(), std::ios::app);

	if (!(_hLib = LoadLibrary(L"Hook.dll")))
	{
		MessageBox(NULL, L"Error loading Hook.dll.", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	logfile << L"\nLoadLibrary(Hook.dll) successful.";

	// Damit nicht alle Prozesse gehookt werde, verwende ich jetzt die ThreadID des WhatsApp-Clients.
	HWND hwndWhatsapp = FindWindow(NULL, WHATSAPP_CLIENT_NAME);
	if (hwndWhatsapp == NULL)
	{
		MessageBox(NULL, L"WhatsApp-Window not found.", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	logfile << L"\nFindWindow successful. [0x" << std::hex << hwndWhatsapp << "]";

	DWORD threadId = GetWindowThreadProcessId(hwndWhatsapp, NULL);
	if (threadId == NULL)
	{
		MessageBox(NULL, L"ThreadID of WhatsApp-Window not found.", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	logfile << L"\nGetWindowThreadProcessId successful. [0x" << std::hex << threadId << "]";

	if (RegisterHook(_hLib, threadId, L"") == false)
	{
		MessageBox(NULL, L"Error setting hook procedure.", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	logfile << L"\nRegister Hook successful.";

	logfile.close();

	wc.style         = 0;
	wc.lpfnWndProc   = HookWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = NULL;
	wc.hCursor       = NULL;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = NAME;
	if (!RegisterClass(&wc))
	{
		MessageBox(NULL, L"Error creating window class", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	if (!(_hwndHook = CreateWindow(NAME, NAME, WS_OVERLAPPED, 0, 0, 0, 0, (HWND)NULL, (HMENU)NULL, (HINSTANCE)hInstance, (LPVOID)NULL)))
	{
		MessageBox(NULL, L"Error creating window", L"WhatsappTray", MB_OK | MB_ICONERROR);
		return 0;
	}
	for (int i = 0; i < MAXTRAYITEMS; i++)
	{
		_hwndItems[i] = NULL;
	}

	while (IsWindow(_hwndHook) && GetMessage(&msg, _hwndHook, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
