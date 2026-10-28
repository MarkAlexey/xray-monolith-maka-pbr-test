function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_world", "scope_reference_particle_display")
        :zb(false, false):blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_scene_source", "$user$svp_reference_scene_source")
    shader:dx10texture("scope_reference_main_meter", "$user$svp_reference_main_meter")
end
