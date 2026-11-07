#include "core.hpp"
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
namespace ms {
static unsigned component(unsigned pixel,unsigned mask,unsigned fallback){if(!mask)return fallback;unsigned shift=0;while(!(mask&1)){mask>>=1;shift++;}require((mask&(mask+1))==0,"DDS 颜色掩码不连续");return ((uint64_t((pixel>>shift)&mask)*255)+mask/2)/mask;}
Texture decodeDds(const Bytes& b){
 require(b.size()>=128&&u32(b.data())==0x20534444&&u32(b.data()+4)==124&&u32(b.data()+76)==32,"DDS 文件头无效");
 unsigned h=u32(b.data()+12),w=u32(b.data()+16),pf=u32(b.data()+80),four=u32(b.data()+84),bits=u32(b.data()+88),off=128,fmt=0;
 require(w&&h&&w<=8192&&h<=8192&&u32(b.data()+24)<=1&&(u32(b.data()+112)&0xfe00)==0,"不支持的 DDS 尺寸、立方体贴图或体积贴图");
 bool bgra=false; // decode the first 2D image, first mip only
 if(pf&4){switch(four){case 0x31545844:fmt=1;break;case 0x33545844:fmt=2;break;case 0x35545844:fmt=3;break;case 0x31495441:case 0x55344342:fmt=4;break;case 0x32495441:case 0x55354342:fmt=5;break;
 case 0x30315844:{require(b.size()>=148,"DDS DX10 文件头不完整");off=148;unsigned dx=u32(b.data()+128);require(u32(b.data()+132)==3&&!(u32(b.data()+136)&4)&&u32(b.data()+140)==1,"DDS must be a single 2D texture");if(dx==71||dx==72)fmt=1;else if(dx==74||dx==75)fmt=2;else if(dx==77||dx==78)fmt=3;else if(dx==80)fmt=4;else if(dx==83)fmt=5;else if(dx==95||dx==96)fmt=dx==95?6:8;else if(dx==98||dx==99)fmt=7;else if(dx==28||dx==29){bits=32;}else if(dx==87||dx==91){bits=32;bgra=true;}else require(false,"不支持的 DDS DXGI 格式 "+std::to_string(dx));break;}default:require(false,"不支持的 DDS FourCC");}}
 Texture t{w,h,Bytes(4ull*w*h)};
 if(!fmt){require(bits==24||bits==32,"DDS RGB bits");unsigned stride=w*(bits/8);if(u32(b.data()+8)&8){unsigned pitch=u32(b.data()+20);require(pitch>=stride,"DDS row pitch");stride=pitch;}require(uint64_t(off)+uint64_t(stride)*h<=b.size(),"DDS RGB 数据不完整");unsigned masks[4]={u32(b.data()+92),u32(b.data()+96),u32(b.data()+100),u32(b.data()+104)};
 if(off==148){masks[0]=bgra?0xff0000:0xff;masks[1]=0xff00;masks[2]=bgra?0xff:0xff0000;masks[3]=0xff000000;}
 require(masks[0]&&masks[1]&&masks[2],"DDS RGB masks");for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){auto p=b.data()+off+uint64_t(stride)*y+(bits/8)*x;unsigned v=unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16);if(bits==32)v|=unsigned(p[3])<<24;auto o=t.pixels.data()+4ull*(y*w+x);for(int c=0;c<4;c++)o[c]=component(v,masks[c],c==3?255:0);}return t;}
 unsigned block=(fmt==1||fmt==4)?8:16,bx=(w+3)/4,by=(h+3)/4;require(uint64_t(off)+uint64_t(bx)*by*block<=b.size(),"DDS 块数据不完整");
 for(unsigned y=0;y<by;y++)for(unsigned x=0;x<bx;x++){auto p=b.data()+off+(uint64_t(y)*bx+x)*block;alignas(8) uint8_t rgba[64]={};uint8_t rg[32]={};float rgb[48]={};
 switch(fmt){case 1:bcdec_bc1(p,rgba,16);break;case 2:bcdec_bc2(p,rgba,16);break;case 3:bcdec_bc3(p,rgba,16);break;case 4:bcdec_bc4(p,rg,4);for(int k=0;k<16;k++){rgba[k*4]=rg[k];rgba[k*4+3]=255;}break;case 5:bcdec_bc5(p,rg,8);for(int k=0;k<16;k++){rgba[k*4]=rg[k*2];rgba[k*4+1]=rg[k*2+1];rgba[k*4+3]=255;}break;case 6:case 8:bcdec_bc6h_float(p,rgb,12,fmt==8);for(int k=0;k<16;k++){for(int c=0;c<3;c++)rgba[k*4+c]=uint8_t(std::round(std::clamp(rgb[k*3+c],0.f,1.f)*255));rgba[k*4+3]=255;}break;case 7:bcdec_bc7(p,rgba,16);break;}
 for(unsigned j=0;j<4&&y*4+j<h;j++)for(unsigned i=0;i<4&&x*4+i<w;i++)std::memcpy(t.pixels.data()+4ull*((y*4+j)*w+x*4+i),rgba+4*(j*4+i),4);}
 return t;
}
}
