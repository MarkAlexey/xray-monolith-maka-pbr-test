function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_world", "scope_reference_fog_blur")
        :zb(false,false):blend(false,blend.one,blend.zero)
    shader:dx10texture("s_image", "$user$svp_reference_scene_source")
    shader:dx10texture("scope_reference_main_meter", "$user$svp_reference_main_meter")
    shader:dx10sampler("smp_nofilter")
    shader:dx10sampler("smp_rtlinear")
end
