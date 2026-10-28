-- A hybrid magnifier's reflex over the Reference image, added into the frame and the glow targets
-- (svp_reticle_emission.cpp)
function normal(shader, t_base, t_second, t_detail)
	shader:begin("scope_vertex", "scope_reference_capture_light")
	: zb(false, false)
	: blend(true, blend.one, blend.one)
	shader:dx10texture("scope_reference_ocular", "$user$svp_reference_ocular_input")
	shader:dx10texture("scope_reference_capture_tc", "$user$svp_reference_capture_tc")
	shader:dx10texture("s_capture_light", "$user$reticle_capture_light")
	shader:dx10texture("s_capture_emission", "$user$reticle_capture_emission")
	shader:dx10texture("s_capture_glow", "$user$reticle_capture_glow")
	shader:dx10texture("scope_reference_visible_glass", "$user$ssfx_motion_vectors")
	shader:dx10sampler("smp_rtlinear")
end
