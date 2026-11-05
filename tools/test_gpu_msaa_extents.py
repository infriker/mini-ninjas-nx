#!/usr/bin/env python3
"""CPU image tests of actual MSAA copy/growth command flow; compile its GLSL with uam.

This checks pixel/row semantics and commands, not actual console execution or FPS.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'sdk/src/x360/deko/destinos.cpp').read_text()

def method(name):
    return re.search(r'^(?:bool|uint32_t) RenderDk::'+name+r'\(.*?^}', source, re.M|re.S)[0]

cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <rex/x360/deko/target_extent.h>
#include <rex/x360/deko/msaa_select.h>
using namespace rex::x360;
bool deko_altura_destino_ajustada=true, deko_edram_baldosas=false;
bool nfsmw_dk_compresion_profundidad=true, nfsmw_dk_compresion_color=true;
int deko_altura_minima_destino=1440;
#define REXCVAR_GET(x) (x)
constexpr unsigned kAltoMaximoDestino=2048;
enum { DkImageType_2D, DkImageType_2DMS };
enum { DkImageFlags_UsageRender=1, DkImageFlags_Usage2DEngine=2, DkImageFlags_HwCompression=4,
       DkBarrier_Fragments=1, DkInvalidateFlags_Image=2, DkInvalidateFlags_Zcull=4,
       DkFace_None=0, DkFace_FrontAndBack=3, DkCompareOp_Always=7, DkStageFlag_GraphicsMask=31,
       DkStage_Fragment=4, DkPrimitive_Triangles=3, DK_UNIFORM_BUF_ALIGNMENT=256,
       DkSwizzle_PositiveX=0,DkSwizzle_PositiveY=1,DkSwizzle_PositiveZ=2,DkSwizzle_PositiveW=3 };
struct Pixel { float value=0; unsigned stencil=0; bool operator==(const Pixel&) const = default; };
struct Image {
  unsigned w=0,h=0,samples=1; std::vector<Pixel> data;
  Pixel& at(unsigned x,unsigned y,unsigned s=0) { assert(x<w && y<h && s<samples); return data.at((y*w+x)*samples+s); }
};
struct DkImage { std::shared_ptr<Image> ptr; };
struct DkImageView { DkImage image; };
struct DkViewport { float x,y,w,h,z0,z1; };
struct DkScissor { unsigned x,y,w,h; };
struct DkViewportSwizzle { unsigned x,y,z,w; };
struct DkRasterizerState { unsigned cullMode; };
struct DkColorState {};
struct DkColorWriteState { unsigned masks; };
struct DkDepthStencilState { bool depthTestEnable,depthWriteEnable; unsigned depthCompareOp; bool stencilTestEnable; };
struct DkShader { unsigned id; };
enum { Color, Depth, ColorMS, DepthMS, DepthStencil };
struct Gpu {
  DkImage target; DkScissor rect{}; DkDepthStencilState ds{}; unsigned shader=0,draws=0,clears=0;
  unsigned stencil_mask=0; const int32_t* uniform=nullptr;
  std::vector<DkImage> descriptors{DkImage{}}; unsigned bound[2]{};
};
using DkCmdBuf=Gpu*;
struct Nucleo { Gpu gpu; DkCmdBuf comandos() { return &gpu; } };
struct ImagenDk {
  DkImage imagen; unsigned memoria=0,tipo=DkImageType_2D,formato=0,ancho=0,alto=0;
  unsigned alto_usado=0,alto_borrado=0,alto_logico=0,ms_filas=0;
  uint64_t serie=0;
  bool profundidad=false,viva=false,borrado_pendiente=false,contenido_invalido=false,banda_sucia=false;
  bool ms_nueva=false,ms_ligada=false,s_nueva=true;
  float borrado_z=1,borrado_color[4]{}; uint8_t borrado_estencil=0;
};
namespace deko { struct Subida { uint8_t* cpu; uintptr_t gpu; explicit operator bool() const { return cpu; } }; }
DkImageView VistaCompleta(DkImage i) { return {i}; }
void dkImageViewDefaults(DkImageView* v,const DkImage* i) { v->image=*i; }
void dkCmdBufBindRenderTargets(Gpu* g,const void*,unsigned,const DkImageView* v) { g->target=v->image; }
void dkCmdBufBindRenderTarget(Gpu* g,const DkImageView* v,const void*) { g->target=v->image; }
void dkCmdBufSetViewports(Gpu*,unsigned,const DkViewport*,unsigned) {}
void dkCmdBufSetViewportSwizzles(Gpu*,unsigned,const DkViewportSwizzle*,unsigned) {}
void dkCmdBufSetScissors(Gpu* g,unsigned,const DkScissor* s,unsigned) { g->rect=*s; }
void dkCmdBufBarrier(Gpu*,unsigned,unsigned) {}
void dkRasterizerStateDefaults(DkRasterizerState* s) { *s={}; }
void dkColorStateDefaults(DkColorState*) {}
void dkColorWriteStateDefaults(DkColorWriteState* s) { s->masks=15; }
void dkDepthStencilStateDefaults(DkDepthStencilState* s) { *s={}; }
void dkCmdBufBindRasterizerState(Gpu*,const DkRasterizerState*) {}
void dkCmdBufBindColorState(Gpu*,const DkColorState*) {}
void dkCmdBufBindColorWriteState(Gpu*,const DkColorWriteState*) {}
void dkCmdBufBindDepthStencilState(Gpu* g,const DkDepthStencilState* s) { g->ds=*s; }
void dkCmdBufSetStencil(Gpu* g,unsigned,unsigned mask,unsigned,unsigned) { g->stencil_mask=mask; }
void dkCmdBufBindVtxAttribState(Gpu*,const void*,unsigned) {}
void dkCmdBufBindVtxBufferState(Gpu*,const void*,unsigned) {}
void dkCmdBufBindShaders(Gpu* g,unsigned,const DkShader** s,unsigned) { g->shader=s[1]->id; }
void dkCmdBufBindUniformBuffer(Gpu* g,unsigned,unsigned,uintptr_t p,unsigned) { g->uniform=reinterpret_cast<const int32_t*>(p); }
unsigned dkMakeTextureHandle(unsigned slot,unsigned) { return slot; }
void dkCmdBufBindTexture(Gpu* g,unsigned,unsigned binding,unsigned descriptor) { g->bound[binding]=descriptor; }
void dkCmdBufDraw(Gpu* g,unsigned,unsigned,unsigned,unsigned,unsigned) {
  const auto& u=g->uniform; auto& src=*g->descriptors.at(g->bound[0]).ptr; auto& dst=*g->target.ptr;
  for(unsigned y=g->rect.y;y<g->rect.y+g->rect.h;++y) for(unsigned x=g->rect.x;x<g->rect.x+g->rect.w;++x) {
    Pixel& out=dst.at(x,y); Pixel in;
    if(g->shader==Depth || g->shader==DepthMS || g->shader==DepthStencil) {
      const unsigned scale=g->shader==DepthMS ? 1 : std::max(u[2],1);
      in=src.at((int(x)+u[0])*scale,(int(y)+u[1])*scale,0);
      out.value=in.value;
      if(g->ds.stencilTestEnable) {
        assert(g->shader==DepthStencil && g->stencil_mask==255);
        out.stencil=g->descriptors.at(g->bound[1]).ptr->at(int(x)+u[0],int(y)+u[1]).stencil;
      }
    } else if(u[2]>1) {
      const unsigned px=x-(u[3]&65535),py=y-(u[3]>>16);
      out.value=0;
      for(unsigned s=0;s<4;++s) out.value+=g->shader==ColorMS ? src.at(px+u[0],py+u[1],s).value*0.25f
          : src.at(px*2+u[0]+(s&1),py*2+u[1]+(s>>1)).value*0.25f;
    } else out.value=src.at(int(x)+u[0],int(y)+u[1]).value;
  }
  ++g->draws;
}
struct RenderDk {
  Nucleo nucleo_; uint64_t next=1,generacion_texturas_=0;
  std::unordered_map<uint64_t,ImagenDk> ms_de_;
  std::unordered_map<uint64_t,unsigned> borrado_previo_;
  bool allocation_fails=false,uniform_fails=false,snapshot=false; unsigned materialized=0,unpacks=0,retired=0;
  unsigned ends=0;
  std::array<uint8_t,256> uniform{};
  bool vs_salida_valido_=true,fs_copia_color_valido_=true,fs_copia_profundidad_valido_=true;
  bool fs_copia_color_ms_valido_=true,fs_copia_profundidad_ms_valido_=true,fs_copia_zs_valido_=true;
  DkShader vs_salida_{99},fs_copia_color_{Color},fs_copia_profundidad_{Depth},fs_copia_color_ms_{ColorMS};
  DkShader fs_copia_profundidad_ms_{DepthMS},fs_copia_zs_{DepthStencil};
  bool transferencia_sin_barrera_=false,copia3d_pendiente_=false,rop_sin_barrera_=false;
  bool invalidar_zcull_=false,trabajo_3d_sin_barrera_=false;
  unsigned ms_resueltas_directas_=0,barreras_fragmentos_=0,copias_3d_=0;
  enum { kGpuCopias,kCopiaValor=0 };
  bool AlturaAjustada() const;
  uint32_t AlturaDestino(uint32_t,uint32_t,bool) const;
  bool AsegurarAlturaDestino(ImagenDk&,uint32_t);
  bool Copiar3D(const ImagenDk&,uint32_t,uint32_t,uint32_t,uint32_t,ImagenDk&,uint32_t,uint32_t,
                int32_t=0,float=1,uint32_t=1,bool=false);
  bool CrearImagen(ImagenDk& im,unsigned type,unsigned format,unsigned w,unsigned h,unsigned,unsigned,unsigned,const char*) {
    if(allocation_fails) return false;
    im.tipo=type; im.formato=format; im.profundidad=format==1; im.ancho=w; im.alto=h; im.viva=true; im.serie=++next;
    im.imagen.ptr=std::make_shared<Image>(); auto& p=*im.imagen.ptr; p.w=w;p.h=h;p.samples=type==DkImageType_2DMS?4:1;
    p.data.resize(w*h*p.samples); return true;
  }
  ImagenDk image(unsigned w,unsigned h,bool depth=false,unsigned type=DkImageType_2D) {
    ImagenDk im; assert(CrearImagen(im,type,depth?1:0,w,h,1,1,0,"")); im.alto_borrado=h;return im;
  }
  void DestruirImagen(ImagenDk& im) { ms_de_.erase(im.serie); im=ImagenDk{}; ++retired; }
  void AntesDeCambiarDestino(ImagenDk*) { if(snapshot) {snapshot=false;++materialized;} }
  void AsegurarTrabajo() {} void TerminarPase() { ++ends; } void AntesDe3D() {} void OlvidarEstado() {}
  void MarcarGpu(unsigned) {} void BarreraTransferencia() {}
  void BorrarFilas(ImagenDk& im,unsigned,unsigned first,unsigned last) {
    for(unsigned y=first;y<last;++y) for(unsigned x=0;x<im.ancho;++x)
      im.imagen.ptr->at(x,y)={im.profundidad?im.borrado_z:im.borrado_color[0],im.profundidad?im.borrado_estencil:0u};
    im.alto_borrado=last;im.banda_sucia=last<im.alto; ++nucleo_.gpu.clears;
  }
  void HacerBorradoPendiente(ImagenDk& im) {
    if(!im.borrado_pendiente)return;
    im.borrado_pendiente=false;im.ms_nueva=false;BorrarFilas(im,0,0,im.alto);
  }
  void CompletarBanda(ImagenDk& im,unsigned rows,bool,unsigned) {
    if(im.banda_sucia&&!im.borrado_pendiente&&rows>im.alto_borrado) { LeerMuestras(im); BorrarFilas(im,0,im.alto_borrado,std::min(rows,im.alto)); }
  }
  void RestaurarContenido(ImagenDk& im,bool) {
    if(!im.contenido_invalido)return;
    im.contenido_invalido=false;
    for(auto& p:im.imagen.ptr->data)p={0.625f,197};
  }
  void LeerMuestras(ImagenDk& im) {
    if(!im.ms_nueva)return;
    auto& ms=*ms_de_.at(im.serie).imagen.ptr;
    for(unsigned y=0;y<im.ms_filas;++y)for(unsigned x=0;x<im.ancho;++x)
      im.imagen.ptr->at(x,y)=ms.at(x/2,y/2,(x&1)|((y&1)<<1));
    im.ms_nueva=false;im.ms_filas=0;++unpacks;
  }
  void EscribirMuestras(ImagenDk& im) { LeerMuestras(im); im.s_nueva=true; }
  deko::Subida Reservar(unsigned,unsigned) { return uniform_fails ? deko::Subida{} : deko::Subida{uniform.data(),reinterpret_cast<uintptr_t>(uniform.data())}; }
  unsigned VistaDe(const ImagenDk& im,unsigned,unsigned) { auto& d=nucleo_.gpu.descriptors;d.push_back(im.imagen);return d.size()-1; }
  unsigned VistaEstencil(const ImagenDk& im) { return VistaDe(im,0,0); }
  void newer(ImagenDk& im) {
    auto ms=image(im.ancho/2,im.alto/2,im.profundidad,DkImageType_2DMS);
    unsigned n=0; for(auto& p:ms.imagen.ptr->data) { p={float(++n)/4096.0f,(n*73)&255}; }
    ms_de_[im.serie]=ms;im.ms_nueva=true;im.s_nueva=false;im.ms_filas=im.alto;
  }
};
'''
cpp += '\n'.join(method(n) for n in ('AlturaAjustada','AlturaDestino','AsegurarAlturaDestino','Copiar3D'))
cpp += r'''
int main() {
  for(unsigned h=0;h<=32768;++h) {
    const auto result=target_extent::Height(h);
    assert(result>=32 && result<=2048 && result%32==0 && result>=std::min(h,2048u));
  }
  assert(target_extent::DrawHeight(2<<16,0x80000000,720<<16,0)==1440);
  assert(target_extent::DrawHeight(0,0,90<<16,0)==90);
  assert(target_extent::DrawHeight(0,0,90<<16,0x7FF6<<16)==80);
  assert(target_extent::DrawHeight(2<<16,0,90<<16,0x7FF6<<16)==160);
  assert(target_extent::DrawHeight(0,0,5<<16,0x7FF6<<16)==0);
  uint64_t keys[5]{};
  for(unsigned threshold:{160,320,640}) for(unsigned pitch:{80,160,256,320,512,640,2560}) {
    keys[4]=(uint64_t(1)<<62)|(2u<<20)|pitch;
    assert(msaa::SelectNative4x(keys,threshold)==(pitch>=threshold));
    keys[4]=(uint64_t(1)<<62)|pitch;assert(!msaa::SelectNative4x(keys,threshold));
  }
  assert(target_extent::Direct4x(true,2,3,4,2,3,16,14));
  assert(!target_extent::Direct4x(true,2,3,4,2,3,16,13));
  assert(!target_extent::Direct4x(false,2,3,4,2,3,16,14));
  assert(!target_extent::Direct4x(true,2,UINT32_MAX,0,1,1,16,32));
  for(bool depth:{false,true}) for(unsigned state=0;state<4;++state) {
    RenderDk r; auto im=r.image(8,32,depth);im.alto_logico=1440;
    im.borrado_z=0.75f;im.borrado_estencil=129;im.borrado_color[0]=0.25f;
    unsigned n=0;for(auto& p:im.imagen.ptr->data)p={float(++n)/1024,(n*31)&255};
    if(state==1) im.borrado_pendiente=true;
    if(state==2) r.newer(im);
    if(state==3) im.contenido_invalido=true;
    auto expected=im.imagen.ptr->data;
    if(state==1) std::fill(expected.begin(),expected.end(),Pixel{depth?0.75f:0.25f,depth?129u:0u});
    if(state==2) {
      const auto ms=r.ms_de_.at(im.serie).imagen.ptr;
      for(unsigned y=0;y<32;++y)for(unsigned x=0;x<8;++x) expected[y*8+x]=ms->at(x/2,y/2,(x&1)|((y&1)<<1));
    }
    if(state==3) std::fill(expected.begin(),expected.end(),Pixel{0.625f,197});
    r.snapshot=true;const auto old=im.serie;
    assert(r.AsegurarAlturaDestino(im,65));assert(im.alto==96&&im.serie!=old&&im.alto_logico==1440);
    assert(r.materialized==1&&r.retired==1&&!im.ms_nueva&&im.s_nueva);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<8;++x) {
      auto p=im.imagen.ptr->at(x,y),e=expected[y*8+x];
      assert(p.value==e.value);if(depth)assert(p.stencil==e.stencil);
    }
    for(unsigned y=32;y<96;++y)for(unsigned x=0;x<8;++x) {
      auto p=im.imagen.ptr->at(x,y);assert(p.value==(depth?0.75f:0.25f));if(depth)assert(p.stencil==129);
    }
    const auto draws=r.nucleo_.gpu.draws;assert(r.AsegurarAlturaDestino(im,65));assert(draws==r.nucleo_.gpu.draws);
  }
  for(bool depth:{false,true}) {
    RenderDk r;auto im=r.image(8,32,depth);r.snapshot=true;r.allocation_fails=true;
    const auto data=im.imagen.ptr;assert(!r.AsegurarAlturaDestino(im,96));
    assert(im.imagen.ptr==data&&im.alto==32&&r.snapshot&&r.materialized==0);
    r.allocation_fails=false;r.uniform_fails=true;assert(!r.AsegurarAlturaDestino(im,96));
    assert(im.imagen.ptr==data&&im.alto==32);
  }
  // Actual copy selection/UBO/commands: compare direct MS reads against a full
  // CPU unpack followed by the legacy shader for many cropped rectangles.
  for(bool depth:{false,true}) for(unsigned x=0;x<3;++x)for(unsigned y=0;y<7;++y) {
    RenderDk fast,legacy;auto a=fast.image(16,32,depth),b=legacy.image(16,32,depth);
    fast.newer(a);legacy.newer(b);auto out=fast.image(8,16,depth),ref=legacy.image(8,16,depth);
    for(auto& p:out.imagen.ptr->data)p={-1,83};for(auto& p:ref.imagen.ptr->data)p={-1,83};
    legacy.fs_copia_profundidad_ms_valido_=false;legacy.fs_copia_color_ms_valido_=false;
    const unsigned sx=depth?x:2*x,sy=depth?y:2*y;
    assert(fast.Copiar3D(a,sx,sy,3,4,out,2,3,0,1,2));
    assert(legacy.Copiar3D(b,sx,sy,3,4,ref,2,3,0,1,2));
    assert(out.imagen.ptr->data==ref.imagen.ptr->data);
    assert(fast.unpacks==0&&legacy.unpacks==1&&a.ms_nueva);
    if(depth)assert(!fast.nucleo_.gpu.ds.stencilTestEnable&&fast.invalidar_zcull_);
  }
  RenderDk fallback;auto a=fallback.image(16,32,true),out=fallback.image(8,16,true);
  fallback.newer(a);a.ms_filas=10;
  assert(fallback.Copiar3D(a,0,4,3,4,out,0,0,0,1,2));assert(fallback.unpacks==1);
  assert(fallback.ms_resueltas_directas_==0);
  fallback.fs_copia_zs_valido_=false;
  assert(fallback.AlturaDestino(2560,1440,true)==2048);
  std::cout<<"MSAA/extent tests passed: cropped resolves, partial sample rows, stencil preservation, growth, allocation failures and selection thresholds\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)
    (path/'test.cpp').write_text(cpp)
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',
                    '-I',str(root/'sdk/include'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=30)
    shader=re.search(r'constexpr const char\* kFsCopiaProfundidad = R"\((.*?)\)";',source,re.S)[1]
    for define in ('','ORIGEN_MS','COPIAR_ESTENCIL'):
        text=shader.replace('\n','\n#define '+define+' 1\n',1) if define else shader
        glsl=path/(define or 'legacy')
        glsl.write_text(text)
        subprocess.run([os.environ.get('UAM','/opt/devkitpro/tools/bin/uam'),'-s','frag','-o',str(glsl)+'.dksh',str(glsl)],check=True)
        assert Path(str(glsl)+'.dksh').stat().st_size>256
    print('All depth-copy GLSL variants compiled to Maxwell DKSH')
