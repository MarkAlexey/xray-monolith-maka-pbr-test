-- the deferred-glass companion of svp_lens_stencil: the physical scope glass writes G-buffer depth in
-- front of the ocular, so that depth-tested stamp misses the lens. Like the deferred reticle, this
-- pass skips the depth test and keeps only pixels the glass G-buffer tag marks as visible lens glass.
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "svp_lens_stencil_glass")
	: zb(false, false)
	: blend(true, blend.zero, blend.one)
	: scopelense(2)
	shader:dx10color_write_enable(false, false, false, false)
	shader:dx10stencil(true, cmp_func.always, 255, 255,
		stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref(254)
	shader:dx10texture("scope_reference_visible_glass", "$user$ssfx_motion_vectors")
end
