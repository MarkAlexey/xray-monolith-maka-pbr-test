#pragma once

// compile time alias of def_aref onto the live m_AlphaRef constant so every
// alpha tested body honors its pass authored ref whatever stack serves the file
extern ECORE_API int ps_r__alpha_ref_live;
extern ECORE_API int ps_r__alpha_dither;

// armed around a pixel shader compile so the served headers carry the dithered edge
static thread_local bool aref_patch_dither = false;

IC const char* aref_patch_leaf(LPCSTR path)
{
	const char* leaf = path;
	for (const char* p = path; *p; ++p)
		if (*p == '\\' || *p == '/')
			leaf = p + 1;
	return leaf;
}

// finds a struct definition by whole word name, space or tab separated
IC const char* aref_patch_struct(const char* text, const char* name)
{
	const u32 nlen = u32(xr_strlen(name));
	const char* s = strstr(text, "struct");
	while (s)
	{
		const char before = s > text ? s[-1] : '\n';
		const char sep = s[6];
		if ((before == '\n' || before == '\r' || before == ' ' || before == '\t') && (sep == ' ' || sep == '\t'))
		{
			const char* n = s + 6;
			while (*n == ' ' || *n == '\t')
				++n;
			if (0 == strncmp(n, name, nlen))
			{
				const char t = n[nlen];
				const bool word = (t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || (t >= '0' && t <= '9') || t == '_';
				if (!word)
					return s;
			}
		}
		s = strstr(s + 1, "struct");
	}
	return nullptr;
}

// rewrites the define line so def_aref reads the live constant, dithered when armed
IC u8* aref_patch_defines(const u8* data, u32 size, u32& out_size)
{
	const char* text = (const char*)data;
	const char* def = strstr(text, "def_aref");
	while (def)
	{
		const char* ls = def;
		while (ls > text && ls[-1] != '\n')
			--ls;
		const char after = def[8];
		if (0 == strncmp(ls, "#define", 7) && (after == ' ' || after == '\t'))
			break;
		def = strstr(def + 1, "def_aref");
	}
	if (!def)
		return nullptr;
	const char* tail = def + 8;
	const char* eol = strchr(tail, '\n');
	if (!eol)
		eol = text + size;
	const char* alias = aref_patch_dither
		? " (m_AlphaRef + aref_dither(I) * (1.0f - m_AlphaRef))"
		: " m_AlphaRef";
	const u32 alias_len = u32(xr_strlen(alias));
	const u32 head = u32(tail - text);
	const u32 rest = u32(text + size - eol);
	out_size = head + alias_len + rest;
	u8* patched = xr_alloc<u8>(out_size + 1);
	CopyMemory(patched, text, head);
	CopyMemory(patched + head, alias, alias_len);
	CopyMemory(patched + head + alias_len, eol, rest);
	patched[out_size] = 0;
	return patched;
}

// appends a dither overload after each aref input struct so the threshold hashes
// the pixel from struct texcoords through fwidth, no signature is touched
IC u8* aref_patch_iostructs(const u8* data, u32 size, u32& out_size)
{
	struct row
	{
		const char* name;
		const char* member;
	};
	static const row rows[] = {{"p_flat", "tcdh"}, {"p_bumped", "tcdh"}, {"p_shadow_direct_aref", "tc0"}};
	struct ins
	{
		u32 at;
		string1024 body;
	};
	const char* text = (const char*)data;
	ins list[3];
	u32 count = 0;
	u32 extra = 0;
	for (u32 i = 0; i < 3; ++i)
	{
		const char* s = aref_patch_struct(text, rows[i].name);
		if (!s)
			continue;
		const char* close = strstr(s, "};");
		if (!close)
			continue;
		ins& it = list[count++];
		it.at = u32(close + 2 - text);
		xr_sprintf(it.body,
		           "\nfloat aref_dither(%s i)\n{\n"
		           "\tfloat2 px = floor(i.%s.xy / max(fwidth(i.%s.xy), 1e-7));\n"
		           "\treturn frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));\n}\n",
		           rows[i].name, rows[i].member, rows[i].member);
		extra += u32(xr_strlen(it.body));
	}
	if (!count)
		return nullptr;
	for (u32 a = 0; a < count; ++a)
		for (u32 b = a + 1; b < count; ++b)
			if (list[b].at < list[a].at)
			{
				ins t = list[a];
				list[a] = list[b];
				list[b] = t;
			}
	out_size = size + extra;
	u8* patched = xr_alloc<u8>(out_size + 1);
	u32 src = 0;
	u32 dst = 0;
	for (u32 a = 0; a < count; ++a)
	{
		const u32 seg = list[a].at - src;
		CopyMemory(patched + dst, text + src, seg);
		dst += seg;
		src += seg;
		const u32 blen = u32(xr_strlen(list[a].body));
		CopyMemory(patched + dst, list[a].body, blen);
		dst += blen;
	}
	CopyMemory(patched + dst, text + src, size - src);
	dst += size - src;
	patched[dst] = 0;
	return patched;
}

// returns a fresh buffer with the served header rewritten or null when not applicable
IC u8* aref_patch_serve(LPCSTR file_name, const u8* data, u32 size, u32& out_size)
{
	if (!ps_r__alpha_ref_live)
		return nullptr;
	const char* leaf = aref_patch_leaf(file_name);
	if (0 == _stricmp(leaf, "common_defines.h"))
		return aref_patch_defines(data, size, out_size);
	if (aref_patch_dither && 0 == _stricmp(leaf, "common_iostructs.h"))
		return aref_patch_iostructs(data, size, out_size);
	return nullptr;
}
