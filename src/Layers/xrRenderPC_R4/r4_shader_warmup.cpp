#include "stdafx.h"
#include <process.h>
#include <mutex>
#include <memory>
#include <vector>
#include "../../xrCore/ShaderSourceCRC.h"
#include "../xrRender/svp_console.h"
#include "r4_shader_warmup.h"

// The loader snapshots shader sources, includes, defines and cache paths before starting
// workers. Workers compile only that snapshot; the render thread consumes their bytecode
// through shader_compile. Pixel shaders that ignore skinning share bytecode across the
// engine's skinning-specific cache names.

namespace
{
// The blenders whose pixel shaders warm up (their .s files name them): the 3DSS sight materials, which compile when
// a weapon's HUD model first loads; with PiP, the lens glue (EnsureScopeShaders) and the Reference image passes,
// which compile on the first frame or aim. A blender that is not installed adds nothing.
const LPCSTR sight_blenders[] = { "models_scope_reticle", "models_scope_reticle_precise", "models_reflex_reticle",
	"models_reflex_reticle_3db", "models_reflex_reticle_simple", "models_reflex_reticle_simple_3db", "models_reflex_lens",
	"models_scope_glass", "models_scope_glass_deferred" };
const LPCSTR pip_blenders[] = { "scope_color_write", "scope_depth_write", "svp_distort_stamp", "svp_taa_stamp", "svp_lens_stencil",
	"svp_lens_stencil_glass", "scope_glass_fallback", "scope_reference_reticle", "scope_reference_reticle_deferred",
	"scope_reticle_emission", "scope_glass_fallback_emission", "scope_reference_reticle_emission",
	"scope_reference_reticle_deferred_emission", "scope_reference_capture_light", "scope_debug", "svp_nearblur", "scope_reference_mesh",
	"scope_reference_tonemap", "scope_reference_glyph", "scope_reference_filter", "scope_reference_sky", "scope_reference_sky_check",
	"scope_reference_fog", "scope_reference_fog_blur", "scope_reference_distortion", "scope_reference_volumetric",
	"scope_reference_particle_display", "scope_reference_particle_baseline", "scope_reference_particle_resolve" };
// pixel shaders of PiP passes whose blenders are built in C++: the Reference capture (svp_reference_renderer.inl),
// the lens glass composition (svp_optics.cpp) and the scope reflection capture (scope_vslr_renderer.cpp)
const LPCSTR pip_pixel_shaders[] = { "scope_reference_capture", "scope_glass_compose", "scope_glass_compose_nvg",
	"scope_vslr_resolve", "scope_vslr_sphere", "scope_vslr_mirror", "scope_vslr_light", "scope_vslr_sky2" };

// what _CreatePS passes to shader_compile
constexpr DWORD warmup_flags = D3D10_SHADER_PACK_MATRIX_ROW_MAJOR;
constexpr LPCSTR warmup_entry = "main";

struct WarmupJob
{
	xr_string name;
	xr_vector<char> source;
	u32 source_crc = 0;
	LPCSTR target = nullptr;
	CRender::ShaderCompileOptions options; // the defines point into it, so a job never moves
	xr_vector<xr_string> cache_files; // the source ignores the skinning mode: one name for each
	ID3DBlob* blob = nullptr;
	bool compile = true; // false: the bytecode came from the cache under another name
	bool reported = false;
	HANDLE done = nullptr;
	~WarmupJob()
	{
		_RELEASE(blob);
		if (done)
			CloseHandle(done);
	}
};

struct ShaderWarmup
{
	std::vector<std::unique_ptr<WarmupJob>> jobs; // fixed while the workers run
	xr_map<xr_string, xr_vector<char>> includes; // by the lower-case name a source includes it with
	std::atomic<u32> next{ 0 };
	xr_vector<HANDLE> workers;
	// start, stop and every take hold it whole, so a take never sees jobs being built or deleted; the workers
	// never take it
	std::mutex lock;
	bool running = false;
} warmup;

// The files a source may include, as the engine's includer finds them; conditional includes too, so the set covers
// every branch. skinned: some file of the closure mentions the skinning defines.
void gather_includes(const char* data, size_t size, xr_set<xr_string>& closure, bool& skinned)
{
	const char* cursor = data;
	const char* const end = data + size;
	while (cursor < end)
	{
		const char* line = cursor;
		while (cursor < end && *cursor != '\n' && *cursor != '\r')
			++cursor;
		const xr_string text(line, u32(cursor - line));
		while (cursor < end && (*cursor == '\n' || *cursor == '\r'))
			++cursor;
		const size_t hash = text.find_first_not_of(" \t");
		if (hash == xr_string::npos || text[hash] != '#' || text.find("#include", hash) == xr_string::npos)
			continue;
		const size_t open = text.find('"', hash), close = open == xr_string::npos ? open : text.find('"', open + 1);
		if (close == xr_string::npos)
			continue;
		string_path name;
		xr_strcpy(name, text.substr(open + 1, close - open - 1).c_str());
		xr_strlwr(name);
		if (!closure.insert(name).second)
			continue;
		auto known = warmup.includes.find(name);
		if (known == warmup.includes.end())
		{
			string_path path;
			strconcat(sizeof(path), path, RImplementation.getShaderPath(), name);
			IReader* file = FS.r_open("$game_shaders$", path);
			if (!file)
				file = FS.r_open("$game_shaders$", name);
			if (!file)
				continue;
			const char* bytes = static_cast<const char*>(file->pointer());
			known = warmup.includes.emplace(name, xr_vector<char>()).first;
			known->second.assign(bytes, bytes + file->length());
			FS.r_close(file);
		}
		const xr_vector<char>& include = known->second;
		skinned = skinned || std::search(include.begin(), include.end(), "SKIN_", "SKIN_" + 5) != include.end();
		gather_includes(include.data(), include.size(), closure, skinned);
	}
}

// the cache file shader_compile reads and writes for this shader, as it builds the name
void cache_file_name(string_path& file_name, LPCSTR name, LPCSTR sh_name)
{
	string_path file;
	strconcat(sizeof(file), file, "shaders_cache\\r4\\", name, ".ps\\", sh_name);
	FS.update_path(file_name, "$app_data_root$", file);
}

// the bytecode of a cache file shader_compile would accept for this source, or null
ID3DBlob* cached_bytecode(LPCSTR file_name, u32 source_crc)
{
	if (!FS.exist(file_name))
		return nullptr;
	IReader* file = FS.r_open(file_name);
	ID3DBlob* blob = nullptr;
	if (file && file->length() > 8 && file->r_u32() == source_crc)
	{
		const u32 crc = file->r_u32();
		if (crc32(file->pointer(), file->elapsed()) == crc && SUCCEEDED(D3DCreateBlob(file->elapsed(), &blob)))
			CopyMemory(blob->GetBufferPointer(), file->pointer(), file->elapsed());
	}
	if (file)
		FS.r_close(file);
	return blob;
}

void add_pixel_shader(LPCSTR name)
{
	for (const auto& job : warmup.jobs)
		if (job->name == name)
			return;
	string_path path;
	strconcat(sizeof(path), path, RImplementation.getShaderPath(), name, ".ps");
	IReader* file = FS.r_open("$game_shaders$", path);
	if (!file)
		return;
	auto job = std::make_unique<WarmupJob>();
	job->name = name;
	const char* bytes = static_cast<const char*>(file->pointer());
	job->source.assign(bytes, bytes + file->length());
	FS.r_close(file);
	job->source_crc = getShaderSourceCrc32(job->source.data(), u32(job->source.size()), RImplementation.getShaderPath());

	xr_set<xr_string> closure;
	bool skinned = false;
	gather_includes(job->source.data(), job->source.size(), closure, skinned);
	skinned = skinned || std::search(job->source.begin(), job->source.end(), "SKIN_", "SKIN_" + 5) != job->source.end();

	// the skinning mode only names the cache file, unless the source reads it: then only the render thread's
	// current mode (none) is known. A cache file under one name serves the others without a compile.
	bool all_cached = true;
	for (int skinning = -1; skinning <= (skinned ? -1 : 4); ++skinning)
	{
		CRender::ShaderCompileOptions options;
		RImplementation.shader_compile_options(options, skinning);
		string_path file_name;
		cache_file_name(file_name, name, options.sh_name);
		ID3DBlob* cached = cached_bytecode(file_name, job->source_crc);
		all_cached = all_cached && cached;
		if (cached && !job->blob)
			job->blob = cached;
		else
			_RELEASE(cached);
		job->cache_files.push_back(file_name);
	}
	if (all_cached)
		return;
	job->compile = !job->blob;
	RImplementation.shader_compile_options(job->options, -1);
	job->target = RImplementation.shader_compile_target(warmup_entry, "ps_2_0");
	job->done = CreateEvent(nullptr, TRUE, !job->compile, nullptr);
	if (!job->done)
	{
		Msg("! [SHADER-WARMUP] event creation failed for %s: %lu", name, GetLastError());
		return;
	}
	warmup.jobs.push_back(std::move(job));
}

// the pixel shaders a blender's .s file begins its passes with
void add_blender(LPCSTR blender)
{
	string_path path;
	strconcat(sizeof(path), path, RImplementation.getShaderPath(), blender, ".s");
	IReader* file = FS.r_open("$game_shaders$", path);
	if (!file)
		return;
	const xr_string text(static_cast<const char*>(file->pointer()), u32(file->length()));
	FS.r_close(file);
	for (size_t at = text.find("begin"); at != xr_string::npos; at = text.find("begin", at + 1))
	{
		// begin("vertex shader", "pixel shader")
		const size_t vs = text.find('"', at), vs_end = vs == xr_string::npos ? vs : text.find('"', vs + 1);
		const size_t ps = vs_end == xr_string::npos ? vs_end : text.find('"', vs_end + 1);
		const size_t ps_end = ps == xr_string::npos ? ps : text.find('"', ps + 1);
		if (ps_end == xr_string::npos || text.find(')', at) < ps_end)
			continue;
		add_pixel_shader(text.substr(ps + 1, ps_end - ps - 1).c_str());
	}
}

class WarmupIncluder : public ID3DInclude
{
public:
	HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR file, LPCVOID, LPCVOID* data, UINT* bytes) override
	{
		string_path name;
		xr_strcpy(name, file);
		xr_strlwr(name);
		const auto include = warmup.includes.find(name);
		if (include == warmup.includes.end())
			return E_FAIL;
		*data = include->second.data();
		*bytes = UINT(include->second.size());
		return S_OK;
	}
	HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};

unsigned __stdcall warmup_worker(void*)
{
	WarmupIncluder includer;
	for (u32 index = warmup.next++; index < warmup.jobs.size(); index = warmup.next++)
	{
		WarmupJob& job = *warmup.jobs[index];
		if (!job.compile)
			continue;
		ID3DBlob* errors = nullptr;
		if (FAILED(D3DCompile(job.source.data(), job.source.size(), "", job.options.defines, &includer, warmup_entry, job.target,
				warmup_flags, 0, &job.blob, &errors)))
			_RELEASE(job.blob);
		_RELEASE(errors);
		SetEvent(job.done);
	}
	return 0;
}

void stop_workers()
{
	warmup.running = false;
	if (!warmup.workers.empty())
	{
		warmup.next = u32(warmup.jobs.size()); // the workers finish the shader they are on
		WaitForMultipleObjects(DWORD(warmup.workers.size()), warmup.workers.data(), TRUE, INFINITE);
		for (HANDLE thread : warmup.workers)
			CloseHandle(thread);
		warmup.workers.clear();
	}
	warmup.jobs.clear();
	warmup.includes.clear();
	warmup.next = 0;
}
} // namespace

void shader_warmup_start()
{
	std::lock_guard<std::mutex> guard(warmup.lock);
	stop_workers();
	if (r__shader_debug)
		return;
	// the lens glue first: the level's first frame needs it, and scope_color_write is the longest compile
	if (scope_svp_enabled)
	{
		for (LPCSTR blender : pip_blenders)
			add_blender(blender);
		for (LPCSTR shader : pip_pixel_shaders)
			add_pixel_shader(shader);
	}
	for (LPCSTR blender : sight_blenders)
		add_blender(blender);
	const u32 compiles = u32(std::count_if(warmup.jobs.begin(), warmup.jobs.end(), [](const auto& job) { return job->compile; }));
	// D3DCompile runs single-threaded per call; half the cores leave the level loader its share
	const u32 count = _min(_max(std::thread::hardware_concurrency() / 2, 1u), _min(4u, compiles));
	for (u32 i = 0; i < count; ++i)
	{
		// a deep stack for the compiler's recursion on the large lens shaders
		const uintptr_t thread = _beginthreadex(nullptr, 16 << 20, warmup_worker, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
		if (thread)
			warmup.workers.push_back(reinterpret_cast<HANDLE>(thread));
	}
	if (warmup.jobs.empty())
		return;
	Msg("* [SHADER-WARMUP] %u pixel shaders, %u to compile on %u threads", u32(warmup.jobs.size()), compiles, u32(warmup.workers.size()));
	if (compiles && warmup.workers.empty())
		stop_workers();
	else
		warmup.running = true;
}

ID3DBlob* shader_warmup_take(LPCSTR cache_file, u32 source_crc, LPCSTR entry, DWORD flags)
{
	if (flags != warmup_flags || xr_strcmp(entry, warmup_entry))
		return nullptr;
	std::lock_guard<std::mutex> guard(warmup.lock);
	if (!warmup.running)
		return nullptr;
	for (const auto& job : warmup.jobs)
	{
		if (job->source_crc != source_crc)
			continue;
		for (const xr_string& file : job->cache_files)
		{
			if (_stricmp(file.c_str(), cache_file))
				continue;
			if (WaitForSingleObject(job->done, INFINITE) != WAIT_OBJECT_0)
				return nullptr;
			if (!job->reported)
				Msg("* [SHADER-WARMUP] %s %s", job->name.c_str(), job->blob ? "taken" : "failed, compiling here");
			job->reported = true;
			if (job->blob)
				job->blob->AddRef(); // the caller's reference; the job keeps its own for other cache names
			return job->blob;
		}
	}
	return nullptr;
}

void shader_warmup_stop()
{
	std::lock_guard<std::mutex> guard(warmup.lock);
	stop_workers();
}
