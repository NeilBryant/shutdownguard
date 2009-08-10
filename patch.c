/*
	ShutdownGuard - Prevent Windows shutdown
	Copyright (C) 2009  Stefan Sundin (recover89@gmail.com)
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#define UNICODE
#define _UNICODE

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define _WIN32_WINNT 0x0500
#include <windows.h>

//App
#define APP_NAME L"ShutdownGuard"

//Cool stuff
HMODULE app;
PROC fnExitWindowsEx;
UINT WM_SHUTDOWNBLOCKED = 0;
int patched = 0;

wchar_t txt[1000];

//Error()
#ifdef DEBUG
#include "include/error.h"
#else
#define Error(a,b,c,d,e)
#endif

int PatchIAT(PCSTR pszCalleeModName, PROC pfnCurrent, PROC pfnNew, HMODULE hmodCaller) {
	//Load dbghelp.dll
	HINSTANCE dbghelp = LoadLibrary(L"dbghelp.dll");
	if (dbghelp == NULL) {
		Error(L"LoadLibrary('dbghelp.dll')", L"Can not find dbghelp.dll.", GetLastError(), TEXT(__FILE__), __LINE__);
		return 1;
	}
	//Get address to ImageDirectoryEntryToData
	PVOID WINAPI (*ImageDirectoryEntryToData)(PVOID,BOOLEAN,USHORT,PULONG)=NULL;
	ImageDirectoryEntryToData = (PVOID)GetProcAddress(dbghelp,"ImageDirectoryEntryToData");
	if (ImageDirectoryEntryToData == NULL) {
		Error(L"GetProcAddress('ImageDirectoryEntryToData')", L"Failed to load ImageDirectoryEntryToData() from dbghelp.dll.", GetLastError(), TEXT(__FILE__), __LINE__);
		return 1;
	}
	
	ULONG ulSize;
	PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(hmodCaller, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulSize);
	
	//Unload dbghelp
	FreeLibrary(dbghelp);
	
	if (pImportDesc == NULL) {
		Error(L"pImportDesc == NULL", L"This module has no import section.", 0, TEXT(__FILE__), __LINE__);
		return 1;
	}
	
	//Find the import descriptor containing references to callee's functions
	for (; pImportDesc->Name; pImportDesc++) {
		PSTR pszModName = (PSTR)((PBYTE) hmodCaller+pImportDesc->Name);
		if (lstrcmpiA(pszModName,pszCalleeModName) == 0) {
			break;
		}
	}
	
	if (pImportDesc->Name == 0) {
		Error(L"pImportDesc->Name == 0", L"This module doesn't import any functions from this callee.", 0, TEXT(__FILE__), __LINE__);
		return 1;
	}

	//Get the import address table (IAT)
	PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE) hmodCaller + pImportDesc->FirstThunk);

	//Loop through all functions
	for (; pThunk->u1.Function; pThunk++) {
		// Get the address of the function address.
		PROC* ppfn = (PROC*)&pThunk->u1.Function;

		if (*ppfn == pfnCurrent) {
			//The function pointers match
			
			//Replace function pointer
			WriteProcessMemory(GetCurrentProcess(), ppfn, &pfnNew, sizeof(pfnNew), NULL);

			//We did it; get out.
			#ifdef DEBUG
			MessageBox(NULL, L"Patching was successful", APP_NAME, MB_ICONINFORMATION|MB_OK);
			#endif
			return 0;
		}
	}

	Error(L"Patching was NOT successful", L"The function is not in the caller's import section.", 0, TEXT(__FILE__), __LINE__);
	return 1;
}

void ShutdownBlocked(UINT uFlags, DWORD dwReason) {
	HWND wnd = FindWindow(APP_NAME,NULL);
	PostMessage(wnd, WM_SHUTDOWNBLOCKED, uFlags, dwReason);
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		WM_SHUTDOWNBLOCKED = RegisterWindowMessage(L"ShutdownBlocked");
		app = GetModuleHandle(NULL);
		//Get address to ExitWindowsEx()
		HMODULE user32 = LoadLibrary(L"user32.dll");
		fnExitWindowsEx = (PROC)GetProcAddress(user32,"ExitWindowsEx");
		FreeLibrary(user32);
		//Patch IAT
		if (PatchIAT("user32.dll",fnExitWindowsEx,(PROC)ShutdownBlocked, app) == 0) {
			patched=1;
		}
		else {
			return FALSE;
		}
	}
	else if (reason == DLL_PROCESS_DETACH && patched) {
		//Unpatch
		PatchIAT("user32.dll", (PROC)ShutdownBlocked, fnExitWindowsEx, app);
	}
	return TRUE;
}
