function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_reference_filter", "scope_reference_filter")
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_filter_source", "$user$svp_reference_filter_source")
    shader:dx10sampler("smp_rtlinear")
end
