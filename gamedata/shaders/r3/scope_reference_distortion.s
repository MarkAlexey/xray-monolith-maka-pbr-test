function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_world", "scope_reference_distortion")
        :zb(false, false):blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_scene_source", "$user$svp_reference_scene_source")
    shader:dx10texture("s_distort", "$user$generic1")
    shader:dx10texture("s_position", "$user$position")
    shader:dx10sampler("smp_rtlinear")
    shader:dx10sampler("smp_nofilter")
end
