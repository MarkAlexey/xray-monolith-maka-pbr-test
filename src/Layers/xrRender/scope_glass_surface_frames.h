#pragma once
#include "svp_lens_detect_math.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// Pure geometry qualification, shared with the offline actual-OGF regression.
struct scope_surface_vertex { svp_v3 p, n, t; unsigned bone; };
struct scope_surface_frame { svp_v3 center, axis, right, up; float radius, halfDepth; unsigned bone; };
namespace scope_surface_detail {
inline svp_v3 add(svp_v3 a,svp_v3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline svp_v3 sub(svp_v3 a,svp_v3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline svp_v3 mul(svp_v3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(svp_v3 a,svp_v3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline svp_v3 cross(svp_v3 a,svp_v3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(svp_v3 a){return std::sqrt(dot(a,a));}
inline svp_v3 unit(svp_v3 a){float n=length(a);return n>1e-8f?mul(a,1/n):svp_v3{0,0,0};}
inline unsigned root(std::vector<unsigned>& p,unsigned x){while(p[x]!=x){p[x]=p[p[x]];x=p[x];}return x;}
}
inline std::vector<scope_surface_frame> scope_surface_fit(
 const std::vector<scope_surface_vertex>& v,const std::vector<unsigned>& indices)
{
 using namespace scope_surface_detail;
 std::vector<scope_surface_frame> out;
 if(v.size()<12 || v.size()>65535 || indices.empty() || indices.size()%3) return out;
 for(const auto& x:v) if(x.bone!=v[0].bone || !std::isfinite(x.p.x)||!std::isfinite(x.p.y)||!std::isfinite(x.p.z)||!std::isfinite(x.n.x)||!std::isfinite(x.n.y)||!std::isfinite(x.n.z)||!std::isfinite(x.t.x)||!std::isfinite(x.t.y)||!std::isfinite(x.t.z))return out;
 std::vector<unsigned> parent(v.size());std::iota(parent.begin(),parent.end(),0);
 std::vector<bool> used(v.size(),false);
 for(unsigned i:indices)if(i>=v.size())return out;
 for(size_t i=0;i<indices.size();i+=3){unsigned a=indices[i];for(unsigned k=0;k<3;++k){unsigned b=indices[i+k];parent[root(parent,b)]=root(parent,a);used[b]=true;}}
 std::vector<std::vector<unsigned>> groups(v.size());
 for(unsigned i=0;i<v.size();++i)if(used[i])groups[root(parent,i)].push_back(i);
 groups.erase(std::remove_if(groups.begin(),groups.end(),[](const std::vector<unsigned>& g){return g.empty();}),groups.end());
 if(groups.size()>32)return out;
 auto fit=[&](const std::vector<unsigned>& g){std::vector<svp_v3> p;for(unsigned i:g)p.push_back(v[i].p);SDiscFit f=svp_fit_disc(p.data(),int(p.size()));svp_v3 mean={0,0,0};for(unsigned i:g)mean=add(mean,v[i].n);if(dot(f.normal,mean)<0)f.normal=mul(f.normal,-1);return f;};
 // Merge split/mirrored halves only when they occupy the same shallow plane
 // and face the same way. Front and rear remain separate optical surfaces.
 for(size_t a=0;a<groups.size();++a)for(size_t b=a+1;b<groups.size();){
  auto x=fit(groups[a]),y=fit(groups[b]);auto delta=sub(x.center,y.center);
  if(x.valid&&y.valid&&dot(x.normal,y.normal)>.92f&&std::abs(dot(delta,x.normal))<.3f*std::max(x.radius,y.radius)&&length(delta)<1.8f*(x.radius+y.radius)){
   groups[a].insert(groups[a].end(),groups[b].begin(),groups[b].end());groups.erase(groups.begin()+b);
  }else ++b;
 }
 if(groups.size()>4)return out;
 for(const auto& g:groups){
  auto f=fit(g);if(!f.valid || g.size()<12)return {};
  svp_v3 t={0,0,0};for(unsigned i:g)t=add(t,v[i].t);
  t=unit(sub(t,mul(f.normal,dot(t,f.normal))));
  if(length(t)<.5f){svp_v3 seed=std::abs(f.normal.x)<.8f?svp_v3{1,0,0}:svp_v3{0,1,0};t=unit(sub(seed,mul(f.normal,dot(seed,f.normal))));}
  svp_v3 up=unit(cross(f.normal,t));float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
  for(unsigned i:g){auto d=sub(v[i].p,f.center);float x=dot(d,t),y=dot(d,up),z=dot(d,f.normal);minx=std::min(minx,x);maxx=std::max(maxx,x);miny=std::min(miny,y);maxy=std::max(maxy,y);minz=std::min(minz,z);maxz=std::max(maxz,z);}
  auto center=add(f.center,add(mul(t,(minx+maxx)*.5f),add(mul(up,(miny+maxy)*.5f),mul(f.normal,(minz+maxz)*.5f))));
  float rx=(maxx-minx)*.5f,ry=(maxy-miny)*.5f,radius=0;
  if(rx<.001f||ry<.001f||std::min(rx,ry)/std::max(rx,ry)<.85f)return {};
  bool sectors[16]={};float facing=0;
  for(unsigned i:g){auto d=sub(v[i].p,center);float x=dot(d,t),y=dot(d,up),r=std::sqrt(x*x+y*y);radius=std::max(radius,r);facing+=dot(unit(v[i].n),f.normal);if(r>.75f*std::max(rx,ry)){int sector=int((std::atan2(y,x)+3.141592654f)*(16.f/6.283185308f));sectors[std::min(15,std::max(0,sector))]=true;}}
  if(radius>.08f||(maxz-minz)>.4f*radius||facing/g.size()<.45f)return {};
  int coverage=0;for(bool present:sectors)coverage+=present?1:0;if(coverage<12)return {};
  out.push_back({center,f.normal,t,up,radius,(maxz-minz)*.5f,v[0].bone});
 }
 return out;
}
