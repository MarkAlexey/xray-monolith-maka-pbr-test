// Replays the forward particle list of this frame into the raw capture. Only
// shaders that declare scope_reference_particle_transfer take part.
void CRenderTarget::scope_reference_capture_particles()
{
    // phase_svp_capture overwrites rt_secondVP after the combine, so until
    // then it serves as the display-space target for the particles.
    if (!rt_secondVP || !rt_Generic_0 || !rt_scope_reference_sky_raw
        || rt_secondVP->dwWidth != Width || rt_secondVP->dwHeight != Height
        || rt_secondVP->fmt != rt_Generic_0->fmt) return;
    if (!scope_reference_capture_begin()) return;
    svp_reference_target(rt_scope_reference_scene_temp, "$user$svp_reference_scene_temp", Width, Height);
    if (!t_scope_reference_scene_source) t_scope_reference_scene_source.create("$user$svp_reference_scene_source");
    if (!t_scope_reference_particle_display)
        t_scope_reference_particle_display.create("$user$svp_reference_particle_display");
    if (!s_scope_reference_particle_display)
        s_scope_reference_particle_display.create("scope_reference_particle_display");
    if (!s_scope_reference_particle_baseline)
        s_scope_reference_particle_baseline.create("scope_reference_particle_baseline");
    if (!s_scope_reference_particle_resolve)
        s_scope_reference_particle_resolve.create("scope_reference_particle_resolve");
    const float gamma=RImplementation.TargetMain->scope_reference_packet.toneMapInputGamma;
    auto draw = [&](const ref_rt& target,const ref_shader& shader) {
        svp_reference_unbind();
        u_setrt(target,nullptr,nullptr,nullptr);
        set_viewport_size(HW.pContext,float(Width),float(Height));
        RCache.set_Element(shader->E[0]);
        RCache.set_c("scope_reference_transfer",gamma,0.f,0.f,0.f);
        svp_reference_fullscreen(g_combine);
    };
    t_scope_reference_scene_source->surface_set(rt_scope_reference_raw->pSurface);
    draw(rt_secondVP,s_scope_reference_particle_display);
    t_scope_reference_particle_display->surface_set(rt_secondVP->pSurface);
    // The deferred capture has consumed the sky target, so it is reused for
    // the display image before particles. The resolve compares against it
    // and leaves unchanged pixels at their raw value.
    draw(rt_scope_reference_sky_raw,s_scope_reference_particle_baseline);

    svp_reference_unbind();
    u_setrt(rt_secondVP,nullptr,nullptr,baseZB);
    set_viewport_size(HW.pContext,float(Width),float(Height));
    RCache.set_CullMode(CULL_CCW);
    g_svp_reference_particle_replay = true;
    auto& graph = RImplementation.GMBase;
    graph.r_dsgraph_render_static(1, false);
    graph.r_dsgraph_render_dynamic(1, false);
    graph.r_dsgraph_render_sorted(false);
    g_svp_reference_particle_replay = false;
    RCache.set_ColorWriteEnable();
    draw(rt_scope_reference_scene_temp,s_scope_reference_particle_resolve);
    svp_reference_unbind();
    HW.pContext->CopyResource(rt_scope_reference_raw->pSurface,rt_scope_reference_scene_temp->pSurface);
    SetActive(true);
    u_setrt(rt_Generic_0, rt_Heat, rt_ssfx_motion_vectors, baseZB);
}

void CRenderTarget::scope_reference_capture_distortion()
{
    if (!scope_reference_capture_begin()) return;
    svp_reference_target(rt_scope_reference_scene_temp, "$user$svp_reference_scene_temp", Width, Height);
    if (!t_scope_reference_scene_source) t_scope_reference_scene_source.create("$user$svp_reference_scene_source");
    if (!s_scope_reference_distortion) s_scope_reference_distortion.create("scope_reference_distortion");
    svp_reference_unbind();
    t_scope_reference_scene_source->surface_set(rt_scope_reference_raw->pSurface);
    u_setrt(rt_scope_reference_scene_temp, nullptr, nullptr, nullptr);
    set_viewport_size(HW.pContext, float(Width), float(Height));
    RCache.set_Element(s_scope_reference_distortion->E[0]);
    svp_reference_fullscreen(g_combine);
    svp_reference_unbind();
    HW.pContext->CopyResource(rt_scope_reference_raw->pSurface, rt_scope_reference_scene_temp->pSurface);
    SetActive(true);
    u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, baseZB);
}

void CRenderTarget::scope_reference_capture_volumetric(u32 offset)
{
    if (!scope_reference_capture_begin()) return;
    if (!s_scope_reference_volumetric) s_scope_reference_volumetric.create("scope_reference_volumetric");
    // RCache does not track these stages, and particle draws leave them bound.
    HW.pContext->GSSetShader(nullptr,nullptr,0);
    HW.pContext->HSSetShader(nullptr,nullptr,0);
    HW.pContext->DSSetShader(nullptr,nullptr,0);
    RCache.set_Element(s_scope_reference_volumetric->E[0]);
    RCache.set_c("scope_reference_transfer", RImplementation.TargetMain->scope_reference_packet.toneMapInputGamma,0.f,0.f,0.f);
    RCache.set_ColorWriteEnable(D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST,offset,0,4,0,2);
    SetActive(true);
    u_setrt(rt_Generic_0, rt_Generic_1, nullptr, baseZB);
}

void CRenderTarget::scope_reference_capture_fog()
{
    if (!scope_reference_capture_begin()) return;
    if (!s_scope_reference_fog) s_scope_reference_fog.create("scope_reference_fog");
    if (!s_scope_reference_fog_blur) s_scope_reference_fog_blur.create("scope_reference_fog_blur");
    if (!t_scope_reference_scene_source) t_scope_reference_scene_source.create("$user$svp_reference_scene_source");
    if (!t_scope_reference_fog_blur) t_scope_reference_fog_blur.create("$user$svp_reference_fog_blur");
    svp_reference_target(rt_scope_reference_scene_temp, "$user$svp_reference_scene_temp", Width, Height);
    const float gamma=RImplementation.TargetMain->scope_reference_packet.toneMapInputGamma;
    // Unbinds, points the scene-source alias at source and draws shader into target.
    auto draw = [&](const ref_rt& target, const ref_rt& source, const ref_shader& shader, bool prepare = false) {
        svp_reference_unbind();
        t_scope_reference_scene_source->surface_set(source->pSurface);
        u_setrt(target,nullptr,nullptr,nullptr);
        set_viewport_size(HW.pContext,float(target->dwWidth),float(target->dwHeight));
        RCache.set_Element(shader->E[0]);
        RCache.set_c("blur_setup",float(target->dwWidth),float(target->dwHeight),prepare ? 1.f : 0.f,0.f);
        RCache.set_c("ssfx_scattering_setup",ps_ssfx_fog_scattering,0.f,0.f,0.f);
        RCache.set_c("scope_reference_transfer",gamma,0.f,0.f,0.f);
        svp_reference_fullscreen(g_combine);
    };
    // SSFX blurs the tone-mapped image. The first pass converts at full
    // resolution, so the bilinear taps of the later passes read display values.
    draw(rt_scope_reference_scene_temp,rt_scope_reference_raw,s_scope_reference_fog_blur,true);
    for (unsigned i=0;i<2;++i)
    {
        const u32 divisor=i?2:4;
        svp_reference_target(rt_scope_reference_fog[i],i?"$user$svp_reference_fog2":"$user$svp_reference_fog4",
            std::max(1u,Width/divisor),std::max(1u,Height/divisor));
        draw(rt_scope_reference_fog[i],i?rt_scope_reference_fog[0]:rt_scope_reference_scene_temp,s_scope_reference_fog_blur);
    }
    t_scope_reference_fog_blur->surface_set(rt_scope_reference_fog[1]->pSurface);
    draw(rt_scope_reference_scene_temp,rt_scope_reference_raw,s_scope_reference_fog);
    svp_reference_unbind();
    HW.pContext->CopyResource(rt_scope_reference_raw->pSurface,rt_scope_reference_scene_temp->pSurface);
    SetActive(true);
}
