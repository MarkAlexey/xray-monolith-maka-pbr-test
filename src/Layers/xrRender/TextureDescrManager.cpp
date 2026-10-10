#include "stdafx.h"
#pragma hdrstop
#include "TextureDescrManager.h"
#include "ETextureParams.h"
#include "profiler.h"

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
			0 == stricmp(_ext, ".dds") ||
			0 == stricmp(_ext, ".bmp") ||
			0 == stricmp(_ext, ".ogm") ||
            0 == stricmp(_ext, ".gif")))
		*_ext = 0;
}

struct TH_LoadTHM
{
	using map_TD = xr_map<shared_str, CTextureDescrMngr::texture_desc>;
	using map_CS = xr_map<shared_str, cl_dt_scaler*>;

	LPCSTR initial;
	map_TD& s_texture_details;
	map_CS& s_detail_scalers;
};

void CTextureDescrMngr::LoadTHMThread(void* args)
{
	PROF_EVENT();

	TH_LoadTHM* p = (TH_LoadTHM*)args;
	LoadTHM(p->initial, p->s_texture_details, p->s_detail_scalers);
}

void CTextureDescrMngr::LoadTHM(LPCSTR initial, map_TD& s_texture_details, map_CS& s_detail_scalers)
{
	PROF_EVENT();

	FS_FileSet flist;
	FS.file_list(flist, initial, FS_ListFiles, "*.thm");

	STextureParams tp;
	string_path fn;

	for (const FS_File& fs_iter : flist)
	{
		FS.update_path(fn, initial, fs_iter.name.c_str());
		IReader* F = FS.r_open(fn);
		xr_strcpy(fn, fs_iter.name.c_str());
		fix_texture_thm_name(fn);

		R_ASSERT(F->find_chunk(THM_CHUNK_TYPE));
		F->r_u32();
		tp.Clear();
		tp.Load(*F);
		FS.r_close(F);
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
			if (FS.exist(ltx_name, initial, fn, ".ltx"))
			{
				CInifile ini(ltx_name, TRUE, TRUE, FALSE);
				if (ini.section_exist("shaders_options"))
				{
					for (const CInifile::Item& item : ini.r_section("shaders_options").Data)
					{
						if (!item.first.size() || (item.second.size() && !xr_strcmp(item.second, "0")))
							continue;
						desc.m_spec->m_shader_options.emplace_back(item.first,
							item.second.size() ? item.second : shared_str("1"));
					}
				}
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
}

void CTextureDescrMngr::Load()
{
	TH_LoadTHM* gtex = new TH_LoadTHM({"$game_textures$", m_texture_details, m_detail_scalers});
	TH_LoadTHM* lvl = new TH_LoadTHM({"$level$", m_texture_details, m_detail_scalers});
	thread_spawn(LoadTHMThread, "X-Ray THM Loader 1", 0, gtex);
	thread_spawn(LoadTHMThread, "X-Ray THM Loader 2", 0, lvl);
	Sleep(5);
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
