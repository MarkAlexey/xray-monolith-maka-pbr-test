// xrCore.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"
#pragma hdrstop

#include <mmsystem.h>
#include <objbase.h>
#include "xrCore.h"

#pragma comment(lib,"winmm.lib")

#ifdef DEBUG
# include <malloc.h>
#endif // DEBUG

#include<fstream>
#include <iostream>
#include <string>

XRCORE_API xrCore Core;
extern XRCORE_API u32 build_id;
extern XRCORE_API LPCSTR build_date;

namespace CPU
{
	extern void Detect();
};

static u32 init_counter = 0;

//extern char g_application_path[256];

//. extern xr_vector<shared_str>* LogFile;

// demonized: print modded exes version
extern int get_modded_exes_version();
extern xr_string get_modded_exes_version_string();
extern LPCSTR get_modded_exes_name();
extern xr_string timeInDMYHMSMMM();

#include <tbb/global_control.h>
#include <tbb/version.h>

// Monolith: oneTBB arenas (_thread_types.h)
static int tbb_param(const char* name, int low, int high)
{
	const char* found = Core.Params ? strstr(Core.Params, name) : nullptr;
	if (!found) return 0;
	int value = 0;
	if (sscanf(found + xr_strlen(name), " %d", &value) != 1) return 0;
	return value < low ? low : value > high ? high : value;
}

tbb::task_arena& xr_background_arena()
{
	// Created on first use and never destroyed: global background groups (textures_load_tasks)
	// wait in their destructors at DLL unload, after function-local statics are gone
	static tbb::task_arena* arena = []()
	{
		int workers = tbb_param("-bg_threads", 1, 16);
		if (!workers)
		{
			const int hardware = tbb::this_task_arena::max_concurrency();
			workers = hardware / 4 < 1 ? 1 : hardware / 4 > 4 ? 4 : hardware / 4;
		}
		// One more slot, reserved for a thread that waits on a group (wait_for joins the arena);
		// enqueue() never needs a slot
		auto* created = new tbb::task_arena(workers + 1, 1, tbb::task_arena::priority::low);
		created->initialize();
		Msg("* TBB: background loading arena, %d worker(s), low priority (-bg_threads N to change)", workers);
		return created;
	}();
	return *arena;
}

void xr_tbb_initialize()
{
	static tbb::global_control* limit = nullptr;
	const int threads = tbb_param("-tbb_threads", 2, 256);
	if (threads && !limit)
		limit = new tbb::global_control(tbb::global_control::max_allowed_parallelism, size_t(threads));
	Msg("* TBB %d.%d (interface %d): %d threads%s", TBB_VERSION_MAJOR, TBB_VERSION_MINOR, TBB_INTERFACE_VERSION,
	    int(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism)),
	    threads ? " (-tbb_threads)" : "");
}

void xrCore::_initialize(LPCSTR _ApplicationName, xrLogger::LogCallback cb, BOOL init_fs, LPCSTR fs_fname)
{
	PROF_EVENT("xrCore::_initialize");
	xr_strcpy(ApplicationName, _ApplicationName);
	if (0 == init_counter)
	{
#ifdef XRCORE_STATIC
		_clear87();
		_control87(_PC_53, MCW_PC);
		_control87(_RC_CHOP, MCW_RC);
		_control87(_RC_NEAR, MCW_RC);
		_control87(_MCW_EM, MCW_EM);
#endif
		Params = xr_strdup(GetCommandLine());
		xr_strlwr(Params);
		if (!strstr(Params, "-editor"))
			CoInitializeEx(NULL, COINIT_MULTITHREADED);

		string_path fn, dr, di;

		// application path
		GetModuleFileName(GetModuleHandle(MODULE_NAME), fn, sizeof(fn));
		_splitpath(fn, dr, di, 0, 0);
		strconcat(sizeof(ApplicationPath), ApplicationPath, dr, di);

		GetCurrentDirectory(sizeof(WorkingPath), WorkingPath);

		// User/Comp Name
		DWORD sz_user = sizeof(UserName);
		GetUserName(UserName, &sz_user);

		DWORD sz_comp = sizeof(CompName);
		GetComputerName(CompName, &sz_comp);

		// Mathematics & PSI detection
		CPU::Detect();

		Memory._initialize(Core.ParamsData.test(ECoreParams::mem_debug));

		DUMP_PHASE;

		xrLogger::InitLog();
		_initialize_cpu();

		rtc_initialize();

		time_t _time = time(NULL);
		tm* time = localtime(&_time);
		april1 = time ? (time->tm_mday == 1 && time->tm_mon == 3) : false;

		xr_FS = xr_new<CLocatorAPI>();

		xr_EFS = xr_new<EFS_Utils>();
		//. R_ASSERT (co_res==S_OK);

		//Load cmd line from file if it exists
		std::ifstream cmdlineTxt;
		char path_A[MAX_PATH];
		strcpy(path_A, Core.ApplicationPath);
		strcat(path_A, "\\..\\commandline.txt");
		cmdlineTxt.open(path_A);
		
		if (!cmdlineTxt)
		{
			cmdlineTxt.close();
			strcpy(path_A, Core.WorkingPath);
			strcat(path_A, "\\commandline.txt");
			cmdlineTxt.open(path_A);
		}

		if (cmdlineTxt)
		{
			Msg("Found commandline file!");
			std::string line;
			char temp[2048];
			sprintf(temp, Params);
			strcat(temp, " ");
			while (std::getline(cmdlineTxt, line))
			{
				strcat(temp, line.c_str());
				strcat(temp, " ");
			}
			Params = xr_strdup(temp);
		}
		cmdlineTxt.close();

		xrParams::LoadParams();
	}
	if (init_fs)
	{
		u32 flags = 0;
		if (Core.ParamsData.test(ECoreParams::build))
			flags |= CLocatorAPI::flBuildCopy;
		if (Core.ParamsData.test(ECoreParams::ebuild))
			flags |= CLocatorAPI::flBuildCopy | CLocatorAPI::flEBuildCopy;
#ifdef _EDITOR // for EDITORS - no cache
		flags &= ~CLocatorAPI::flCacheFiles;
#endif // _EDITOR
		flags |= CLocatorAPI::flScanAppRoot;
		FS._initialize(flags, 0, fs_fname);
		Msg("'%s' build %d, %s\n", "xrCore", build_id, build_date);

		// demonized: Print modded exes version
		Msg("%s version %s\n", get_modded_exes_name(), get_modded_exes_version_string().c_str());
		Msg("Game started: %s\n", timeInDMYHMSMMM().c_str());
		xr_tbb_initialize(); // -tbb_threads, before anything starts TBB work
		EFS._initialize();
#ifdef DEBUG
#ifndef _EDITOR
		Msg("Process heap 0x%08x", GetProcessHeap());
#endif
#endif // DEBUG
	}
	xrLogger::AddLogCallback(cb);
	init_counter++;
}

#ifndef _EDITOR
#include "compression_ppmd_stream.h"
extern compression::ppmd::stream* trained_model;
#endif
void xrCore::_destroy()
{
	--init_counter;
	if (0 == init_counter)
	{
		FS._destroy();
		EFS._destroy();
		xr_delete(xr_FS);
		xr_delete(xr_EFS);

#ifndef _EDITOR
		if (trained_model)
		{
			void* buffer = trained_model->buffer();
			xr_free(buffer);
			xr_delete(trained_model);
		}
#endif
		xr_free(Params);
		Memory._destroy();
	}
}

#ifndef XRCORE_STATIC

//. why ???
#ifdef _EDITOR
BOOL WINAPI DllEntryPoint(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
#else
//BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
BOOL DllMainXrCore(HANDLE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
#endif
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		{
			_clear87();
			_control87(_PC_53, MCW_PC);
			_control87(_RC_CHOP, MCW_RC);
			_control87(_RC_NEAR, MCW_RC);
			_control87(_MCW_EM, MCW_EM);
		}
		//. LogFile.reserve (256);
		break;
	case DLL_THREAD_ATTACH:
		if (!strstr(GetCommandLine(), "-editor"))
			CoInitializeEx(NULL, COINIT_MULTITHREADED);
		timeBeginPeriod(1);
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
#ifdef USE_MEMORY_MONITOR
        memory_monitor::flush_each_time(true);
#endif // USE_MEMORY_MONITOR
		break;
	}
	return TRUE;
}
#endif // XRCORE_STATIC
