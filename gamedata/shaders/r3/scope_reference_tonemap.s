function normal(shader, t_base, t_second, t_detail)
    shader:begin("scope_vertex", "scope_reference_tonemap")
        :zb(true, false)
        :blend(false, blend.one, blend.zero)
    shader:dx10texture("scope_reference_ocular", "$user$svp_reference_ocular_input")
    shader:dx10texture("s_tonemap", "$user$tonemap")
    shader:dx10sampler("smp_nofilter")
end
