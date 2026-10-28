#include "stdafx.h"
#include "bodycam_diagnostics.h"

// The scope renderer's svp_cop_diag console level: one switch covers both.
ENGINE_API extern int ps_r__svp_cop_diag;

namespace Bodycam
{
bool DiagnosticsEnabled(int level)
{
	return ps_r__svp_cop_diag >= level;
}
}
