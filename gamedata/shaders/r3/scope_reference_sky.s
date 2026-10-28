function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_filter", "scope_reference_sky")
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_sky_display", "$user$svp_reference_sky_display")
    shader:dx10texture("scope_reference_main_meter", "$user$svp_reference_main_meter")
end
