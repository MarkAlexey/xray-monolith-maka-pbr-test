-- stamps the 3DSS lens stencil value (254) over the composited lens, so the 3DSS late-pass meshes
-- (transparent ADS housings, canted cutouts) skip the lens as they do over the stock lens shader;
-- depth-tested so lens glass hidden behind nearer geometry is not stamped
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "svp_lens_stencil")
	: zb(true, false)
	: blend(true, blend.zero, blend.one)
	: scopelense(2)
	shader:dx10color_write_enable(false, false, false, false)
	shader:dx10stencil(true, cmp_func.always, 255, 255,
		stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref(254)
end
