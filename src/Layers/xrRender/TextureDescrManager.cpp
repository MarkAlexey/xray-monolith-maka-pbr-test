#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"
#include "profiler.h"
#include "../../xrCore/xr_ini.h"

// eye-params
float r__dtex_range = 50;

class cl_dt_scaler : public R_constant_setup
{
public:
	float scale;

	cl_dt_scaler(float s) : scale(s)
	{
	};

	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scale, scale, scale, 1 / r__dtex_range);
	}
};

void fix_texture_thm_name(LPSTR fn)
{
	LPSTR _ext = strext(fn);
	if (_ext &&
		(0 == stricmp(_ext, ".tga") ||
			0 == stricmp(_ext, ".thm") ||
			0 == stricmp(_ext, ".ltx") ||
			0 == stricmp(_ext, ".dds") ||
			0 == stricmp(_ext, ".bmp") ||
			0 == stricmp(_ext, ".ogm") ||
            0 == stricmp(_ext, ".gif")))
		*_ext = 0;
}

// IX-Ray: [shaders_options] of a texture's .ltx, one define per line (for example
// USE_DX_NORMAL_MAP = 1). A value of 0 leaves the define out.
void CTextureDescrMngr::ReadShaderOptions(CInifile& ini, texture_spec& spec)
{
	if (!ini.section_exist("shaders_options"))
		return;
	for (const CInifile::Item& item : ini.r_section("shaders_options").Data)
	{
		if (!item.first.size() || (item.second.size() && !xr_strcmp(item.second, "0")))
			continue;
		spec.m_shader_options.emplace_back(item.first, item.second.size() ? item.second : shared_str("1"));
	}
}

// THM loading reads tens of thousands of small files from the archives. Opening and
// decompressing them is what takes the time, so that part runs on worker threads (IX-Ray
// develop de8be597 parallelises the whole loop). Parsing and filling the maps stay on this
// thread, in the original order, because shared_str reference counts are not atomic in this
// engine (str_value: CounterPolicy::NonAtomic): creating or copying the same string (a shared
// detail texture name) on two threads at once could corrupt its count. The result is
// identical to the serial loop.
// Command line -thm_st: read the files on this thread only (to compare timings).
struct thm_raw_file
{
	xr_vector<u8> data;	// whole .thm file
	bool has_ltx = false;	// "<texture>.ltx" next to it ([shaders_options])
	bool opened = false;
};

void CTextureDescrMngr::LoadTHM(LPCSTR initial, map_TD& s_texture_details, map_CS& s_detail_scalers)
{
	PROF_EVENT();
	CTimer timer;
	timer.Start();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.thm");

	xr_vector<const FS_File*> files;
	files.reserve(flist.size());
	for (const FS_File& fs_iter : flist)
		files.push_back(&fs_iter);
	xr_vector<thm_raw_file> raw(files.size());

	// 1. File reads (worker threads): no shared_str is created or copied here
	auto read_one = [&](size_t i)
	{
		string_path path, name, ltx_name;
		FS.update_path(path, initial, files[i]->name.c_str());
		IReader* R = FS.r_open(path);
		if (!R)
			return;
		raw[i].data.assign((u8*)R->pointer(), (u8*)R->pointer() + R->length());
		FS.r_close(R);
		raw[i].opened = true;

		xr_strcpy(name, files[i]->name.c_str());
		fix_texture_thm_name(name);
		raw[i].has_ltx = !!FS.exist(ltx_name, initial, name, ".ltx");
	};
	const bool single_thread = Core.Params && strstr(Core.Params, "-thm_st");
	if (single_thread)
	{
		for (size_t i = 0; i < files.size(); ++i)
			read_one(i);
	}
	else
		xr_parallel_for(size_t(0), files.size(), read_one);
	const u32 read_ms = timer.GetElapsed_ms();

	// 2. Parse and register (this thread, original order)
	STextureParams tp;
	string_path fn;

	for (size_t i = 0; i < files.size(); ++i)
	{
		const FS_File& fs_iter = *files[i];
		R_ASSERT3(raw[i].opened, "Can't open THM", fs_iter.name.c_str());
		IReader thm(raw[i].data.data(), int(raw[i].data.size()));
		IReader* F = &thm;
		xr_strcpy(fn, fs_iter.name.c_str());
		fix_texture_thm_name(fn);

		R_ASSERT3(F->find_chunk(THM_CHUNK_TYPE), "THM without its type chunk", fs_iter.name.c_str());
		F->r_u32();
		tp.Clear();
		tp.Load(*F);
		if (STextureParams::ttImage == tp.type || STextureParams::ttTerrain == tp.type || STextureParams::ttNormalMap ==
			tp.type)
		{
			texture_desc& desc = s_texture_details[fn];
			cl_dt_scaler*& dts = s_detail_scalers[fn];

			if (tp.detail_name.size() && tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail)
			)
			{
				if (desc.m_assoc)
					xr_delete(desc.m_assoc);

				desc.m_assoc = xr_new<texture_assoc>();
				desc.m_assoc->detail_name = tp.detail_name;
				if (dts)
					dts->scale = tp.detail_scale;
				else
					/*desc.m_assoc->cs*/dts = xr_new<cl_dt_scaler>(tp.detail_scale);

				desc.m_assoc->usage = 0;

				if (tp.flags.is(STextureParams::flDiffuseDetail))
					desc.m_assoc->usage |= (1 << 0);

				if (tp.flags.is(STextureParams::flBumpDetail))
					desc.m_assoc->usage |= (1 << 1);
			}
			if (desc.m_spec)
				xr_delete(desc.m_spec);

			desc.m_spec = xr_new<texture_spec>();
			desc.m_spec->m_material = tp.material + (tp.material < 4 ? tp.material_weight : 0);
			desc.m_spec->m_use_steep_parallax = false;
			desc.m_spec->m_use_pbr = (tp.material == STextureParams::tmPBR_Material);
			// The legacy material LUT has four slots; keep the G-buffer material id in range for
			// anything that still reads it from a PBR pixel (same default as GetMaterial)
			if (desc.m_spec->m_use_pbr)
				desc.m_spec->m_material = 1.0f;

			// IX-Ray: optional "<texture>.ltx" next to the THM, section [shaders_options], one
			// define per line (for example USE_DX_NORMAL_MAP = 1). A value of 0 leaves it out.
			string_path ltx_name;
			if (raw[i].has_ltx && FS.exist(ltx_name, initial, fn, ".ltx"))
			{
				CInifile ini(ltx_name, TRUE, TRUE, FALSE);
				ReadShaderOptions(ini, *desc.m_spec);
			}

			if (tp.bump_mode == STextureParams::tbmUse)
			{
				desc.m_spec->m_bump_name = tp.bump_name;
			}
			else if (tp.bump_mode == STextureParams::tbmUseParallax)
			{
				desc.m_spec->m_bump_name = tp.bump_name;
				desc.m_spec->m_use_steep_parallax = true;
			}
		}
	}

	Msg("* THM: %u files from '%s' in %u ms (reads %u ms, %s)", u32(files.size()), initial, timer.GetElapsed_ms(),
		read_ms, single_thread ? "one thread, -thm_st" : "parallel");
}

void CTextureDescrMngr::LoadLTX(LPCSTR initial, map_TD& s_texture_details)
{
	PROF_EVENT();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.ltx");
	for (const FS_File& file : flist)
	{
		string_path name;
		xr_strcpy(name, file.name.c_str());
		fix_texture_thm_name(name);
		if (s_texture_details.find(name) != s_texture_details.end())
			continue;

		string_path path;
		FS.update_path(path, initial, file.name.c_str());
		CInifile ini(path, TRUE, TRUE, FALSE);
		if (!ini.section_exist("texture"))
			continue;

		LPCSTR mode = ini.line_exist("texture", "bump_mode")
			? ini.r_string("texture", "bump_mode") : "none";
		if (!mode || (stricmp(mode, "none") && stricmp(mode, "use") && stricmp(mode, "parallax")))
		{
			Msg("! Invalid texture bump_mode in '%s': expected none, use or parallax", path);
			continue;
		}

		const bool use_bump = stricmp(mode, "none") != 0;
		string_path bump_name = {};
		if (use_bump)
		{
			LPCSTR configured_name = ini.line_exist("texture", "bump_name")
				? ini.r_string("texture", "bump_name") : nullptr;
			if (configured_name && configured_name[0])
			{
				xr_strcpy(bump_name, configured_name);
				fix_texture_thm_name(bump_name);
			}
			else
				strconcat(sizeof(bump_name), bump_name, name, "_bump");
		}

		// IX-Ray PBR: "material = pbr" (or 4, the THM value) marks a metallic/roughness texture,
		// as the THM material "PBR" does. Its G-buffer material id stays 1.0 (see LoadTHM).
		LPCSTR material = ini.line_exist("texture", "material") ? ini.r_string("texture", "material") : nullptr;
		const bool use_pbr = material && (0 == stricmp(material, "pbr") ||
			fsimilar(float(atof(material)), float(STextureParams::tmPBR_Material)));

		texture_desc& desc = s_texture_details[name];
		desc.m_spec = xr_new<texture_spec>();
		desc.m_spec->m_use_pbr = use_pbr;
		desc.m_spec->m_material = use_pbr ? 1.0f : (material ? float(atof(material)) : 1.0f);
		desc.m_spec->m_bump_name = bump_name;
		desc.m_spec->m_use_steep_parallax = stricmp(mode, "parallax") == 0;
		ReadShaderOptions(ini, *desc.m_spec);
	}
}

void CTextureDescrMngr::Load()
{
	LoadTHM("$game_textures$", m_texture_details, m_detail_scalers);
	LoadTHM("$level$", m_texture_details, m_detail_scalers);
	LoadLTX("$game_textures$", m_texture_details);
	LoadLTX("$level$", m_texture_details);
}

void CTextureDescrMngr::UnLoad()
{
	for (auto& it : m_texture_details)
	{
		xr_delete(it.second.m_assoc);
		xr_delete(it.second.m_spec);
	}
	m_texture_details.clear();
}

CTextureDescrMngr::~CTextureDescrMngr()
{
	map_CS::iterator I = m_detail_scalers.begin();
	map_CS::iterator E = m_detail_scalers.end();

	for (; I != E; ++I)
		xr_delete(I->second);

	m_detail_scalers.clear();
}

shared_str CTextureDescrMngr::GetBumpName(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_bump_name;
		}
	}
	return "";
}

BOOL CTextureDescrMngr::UseSteepParallax(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_use_steep_parallax;
		}
	}
	return FALSE;
}

BOOL CTextureDescrMngr::UsePBRTextures(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_use_pbr;
		}
	}
	return FALSE;
}

const CTextureDescrMngr::shader_options* CTextureDescrMngr::GetShaderOptions(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end() && I->second.m_spec && !I->second.m_spec->m_shader_options.empty())
		return &I->second.m_spec->m_shader_options;
	return NULL;
}

float CTextureDescrMngr::GetMaterial(const shared_str& tex_name) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_spec)
		{
			return I->second.m_spec->m_material;
		}
	}
	return 1.0f;
}

void CTextureDescrMngr::GetTextureUsage(const shared_str& tex_name, BOOL& bDiffuse, BOOL& bBump) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			u8 usage = I->second.m_assoc->usage;
			bDiffuse = !!(usage & (1 << 0));
			bBump = !!(usage & (1 << 1));
		}
	}
}

BOOL CTextureDescrMngr::GetDetailTexture(const shared_str& tex_name, LPCSTR& res, R_constant_setup* & CS) const
{
	map_TD::const_iterator I = m_texture_details.find(tex_name);
	if (I != m_texture_details.end())
	{
		if (I->second.m_assoc)
		{
			texture_assoc* TA = I->second.m_assoc;
			res = TA->detail_name.c_str();
			map_CS::const_iterator It2 = m_detail_scalers.find(tex_name);
			CS = It2 == m_detail_scalers.end() ? 0 : It2->second; //TA->cs;
			return TRUE;
		}
	}
	return FALSE;
}
