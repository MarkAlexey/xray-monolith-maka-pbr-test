function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_world", "scope_reference_particle_baseline")
        :zb(false, false):blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_particle_display_source", "$user$svp_reference_particle_display")
end
