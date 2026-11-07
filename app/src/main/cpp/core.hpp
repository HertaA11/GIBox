#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ms {
using Bytes=std::vector<uint8_t>;
inline void require(bool b,const std::string& s){if(!b)throw std::runtime_error(s);}
inline uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline float f32(const uint8_t* p){uint32_t v=u32(p);float f;std::memcpy(&f,&v,4);return f;}
inline void put32(uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(8*i);}
inline Bytes read(const std::string& path,size_t max=256*1024*1024){
 std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"无法读取："+path);
 auto n=f.tellg();require(n>=0&&uint64_t(n)<=max,"文件过大："+path);Bytes b(size_t(n),0);f.seekg(0);
 if(n){f.read(reinterpret_cast<char*>(b.data()),n);}
 require(bool(f),"读取不完整："+path);return b;
}
inline std::string text(const std::string& p){auto b=read(p,1024*1024);return std::string(b.begin(),b.end());}
inline void write(const std::string& p,const void* b,size_t n){std::ofstream f(p,std::ios::binary|std::ios::trunc);require(bool(f),"无法写入："+p);if(n)f.write(static_cast<const char*>(b),n);require(bool(f),"写入失败："+p);}
inline void write(const std::string& p,const std::string& b){write(p,b.data(),b.size());}
inline bool exists(const std::string& p){return std::filesystem::exists(p);}
inline std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
inline std::vector<std::string> lines(std::string s){if(s.compare(0,3,"\xef\xbb\xbf")==0)s.erase(0,3);std::istringstream in(s);std::vector<std::string> v;while(std::getline(in,s)){s=trim(s);if(!s.empty())v.push_back(s);}return v;}
inline std::vector<std::string> words(const std::string& s){std::istringstream in(s);std::vector<std::string> v;std::string a;while(in>>a)v.push_back(a);return v;}
inline unsigned decimal(const std::string& s){require(!s.empty()&&s.find_first_not_of("0123456789")==s.npos,"整数无效："+s);auto n=std::stoull(s);require(n<=UINT32_MAX,"整数溢出");return unsigned(n);}
inline unsigned config(const std::string& s){auto n=decimal(trim(s));require(n>=10000000&&n<=10001999,"角色 ID 超出范围");return n;}
inline void filename(const std::string& s){require(!s.empty()&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==s.npos,"文件名无效："+s);}
inline std::string normalize(std::string s,bool bone=false){std::string o;for(unsigned char c:s){if(c>='A'&&c<='Z')c+=32;if(!bone||(c>='a'&&c<='z')||(c>='0'&&c<='9'))o+=char(c);}if(!bone){auto p=o.rfind("_lod");if(p!=o.npos&&p+4<o.size()&&o.find_first_not_of("0123456789",p+4)==o.npos)o.resize(p);}return o;}
struct Part {std::string name,group;unsigned material,bones;};
struct Material {std::string name;unsigned slot;};
struct Skin {Bytes data;std::set<unsigned> used;};
struct Submesh {unsigned start,count;};
struct Texture {unsigned w,h;Bytes pixels;};
// Halved with a 2x2 box filter (edge pixels averaged with what exists) until neither side exceeds maxSide: exactly what
// the converter and the texture-only import do in Java (ModelConverter.texture, TextureMod, 1024). Done here, in native
// memory, a 4096x8192 texture never reaches the App's Java heap as 128 MiB of RGBA.
inline Texture shrinkTexture(Texture t,unsigned maxSide){
 while(t.w>maxSide||t.h>maxSide){unsigned nw=t.w/2?t.w/2:1,nh=t.h/2?t.h/2:1;Bytes n(size_t(nw)*nh*4);
  for(unsigned j=0;j<nh;j++)for(unsigned i=0;i<nw;i++)for(unsigned c=0;c<4;c++){unsigned sum=0,count=0;
   for(unsigned dy=0;dy<2&&j*2+dy<t.h;dy++)for(unsigned dx=0;dx<2&&i*2+dx<t.w;dx++){sum+=t.pixels[(size_t(j*2+dy)*t.w+i*2+dx)*4+c];count++;}
   n[(size_t(j)*nw+i)*4+c]=uint8_t(sum/count);}
  t.pixels=std::move(n);t.w=nw;t.h=nh;}
 return t;}
// Rows in the other order. model.bin keeps textures with rows bottom to top, for the converter's meshes (whose UVs it
// flips, 1 - v); a texture put on one of the game's own meshes (its UVs untouched) needs the PC rows back.
inline void flipRows(Texture& t){size_t row=size_t(t.w)*4;if(!row||t.pixels.size()!=row*t.h)return;for(unsigned i=0,j=t.h?t.h-1:0;i<j;i++,j--)std::swap_ranges(t.pixels.begin()+long(i*row),t.pixels.begin()+long((i+1)*row),t.pixels.begin()+long(j*row));}
// ---------------------------------------------------------------- bones
// Source bones (the model's skeleton) to the bones of the renderer it goes on. Each bone the weights use is matched by
//  1. its exact name, when that name is on one target bone;
//  2. its name without case, spaces and punctuation, when that is on one target bone;
//  3. among several target bones of that name: the one whose ancestors carry the source bone's ancestors' names the
//     furthest up (needs both parent lists); a tie stays ambiguous and is refused.
// A matched bone's bindpose must agree with the source's (each element within 0.1 %, relative). A bone the target lacks
// hands its weight to its nearest ancestor that matches — only a helper bone (hair, cloth, accessories), never a body
// bone — and is listed in `moved` for the log. Anything else is refused with the bone's name.
struct BoneMap {std::vector<int> target;std::vector<std::pair<unsigned,int>> moved;};
inline bool bodyBone(const std::string& name){auto k=normalize(name,true);
 for(const char* w:{"pelvis","spine","neck","head","clavicle","shoulder","upperarm","forearm","arm","elbow","hand","finger","thigh","calf","knee","leg","foot","toe","jaw","eye","hip","chest"})if(k.find(w)!=k.npos)return true;
 return false;}
inline std::vector<std::string> lineage(size_t i,const std::vector<std::string>& names,const std::vector<int>& parents){std::vector<std::string> out;if(parents.size()!=names.size())return out;
 for(int k=parents[i];k>=0&&out.size()<64;k=parents[size_t(k)]){out.push_back(normalize(names[size_t(k)],true));}return out;}
inline bool bindposeAgrees(const Bytes& sb,size_t i,const Bytes& tb,size_t t){for(int j=0;j<16;j++){float a=f32(sb.data()+64*i+4*j),b=f32(tb.data()+64*t+4*j);
  if(!(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=.001f*(1+std::max(std::abs(a),std::abs(b)))))return false;}return true;}
inline BoneMap mapBones(const std::vector<std::string>& src,const std::vector<std::string>& dst,const Bytes& sb,const Bytes& tb,const std::set<unsigned>& used,
                        const std::vector<int>& srcParents={},const std::vector<int>& dstParents={}){
 require(!src.empty()&&src.size()<=512&&!dst.empty()&&dst.size()<=512&&sb.size()==64*src.size()&&tb.size()==64*dst.size(),"骨骼数量或绑定姿势长度不符");
 std::map<std::string,std::vector<size_t>> exact,loose;for(size_t t=0;t<dst.size();t++){exact[dst[t]].push_back(t);loose[normalize(dst[t],true)].push_back(t);}
 // >=0 target bone; -1 no bone of that name; -2 several, not told apart by their ancestors.
 auto find=[&](size_t i)->int{auto e=exact.find(src[i]);if(e!=exact.end()&&e->second.size()==1)return int(e->second[0]);
  auto k=normalize(src[i],true);if(k.empty())return -1;std::vector<size_t> c;if(e!=exact.end())c=e->second;else{auto l=loose.find(k);if(l!=loose.end())c=l->second;}
  if(c.empty()){return -1;}if(c.size()==1){return int(c[0]);}
  auto mine=lineage(i,src,srcParents);if(mine.empty()||dstParents.size()!=dst.size())return -2;
  int best=-1;size_t score=0;bool tie=false;
  for(size_t t:c){auto theirs=lineage(t,dst,dstParents);size_t n=0;while(n<mine.size()&&n<theirs.size()&&mine[n]==theirs[n])n++;if(n>score){score=n;best=int(t);tie=false;}else if(n==score)tie=true;}
  return best<0||tie?-2:best;};
 BoneMap m;m.target.assign(src.size(),-1);
 for(unsigned i:used){require(i<src.size(),"源骨骼编号无效");int t=find(i);
  if(t>=0){require(bindposeAgrees(sb,i,tb,size_t(t)),"绑定姿势不符："+src[i]);m.target[i]=t;continue;}
  require(t==-1,"骨骼名称有歧义："+src[i]);
  require(!bodyBone(src[i])&&srcParents.size()==src.size(),"找不到骨骼："+src[i]);
  int a=-1;for(int k=srcParents[i];k>=0;k=srcParents[size_t(k)]){int x=find(size_t(k));if(x>=0&&bindposeAgrees(sb,size_t(k),tb,size_t(x))){a=x;break;}if(x==-2)break;}
  require(a>=0,"找不到骨骼："+src[i]+"（其上级骨骼也都不匹配）");m.target[i]=a;m.moved.push_back({i,a});}
 return m;}
// Source bone indices to target ones, then per vertex: the weights going to one target bone added together, largest
// first, summing to 1; unused slots weigh 0 on bone 0.
inline void remap(Skin& s,const std::vector<int>& map){
 for(size_t i=0;i+32<=s.data.size();i+=32){uint8_t* v=s.data.data()+i;std::pair<float,unsigned> slot[4];int n=0;
  for(int j=0;j<4;j++){float w=f32(v+4*j);if(!(w>0))continue;unsigned b=u32(v+16+4*j);require(b<map.size()&&map[b]>=0,"未映射的骨骼");unsigned t=unsigned(map[b]);
   int k=0;while(k<n&&slot[k].second!=t)k++;if(k<n){slot[k].first+=w;}else{slot[n++]={w,t};}}
  std::sort(slot,slot+n,[](const std::pair<float,unsigned>& a,const std::pair<float,unsigned>& b){return a.first>b.first||(a.first==b.first&&a.second<b.second);});
  float sum=0;for(int k=0;k<n;k++)sum+=slot[k].first;
  for(int j=0;j<4;j++){float w=j<n&&sum>0?slot[j].first/sum:0.f;std::memcpy(v+4*j,&w,4);put32(v+16+4*j,j<n?slot[j].second:0);}}}
Texture decodeDds(const Bytes& b);

// Identity of an original Mesh asset, readable without CPU vertex data:
// asset name, vertex count (shared+0x74), submesh count (shared+0xc0), bindpose count (shared+0xe0).
// The renderer name is informational. Same rule as PC importers matching buffers by hash.
struct Fingerprint {std::string renderer,mesh;unsigned vertices=0,submeshes=0,bones=0;};
inline std::string meshName(std::string s){std::string o;for(unsigned char c:s)o+=char(c>='A'&&c<='Z'?c+32:c);for(const char* junk:{"(clone)"," instance"}){size_t p;while((p=o.find(junk))!=o.npos)o.erase(p,std::strlen(junk));}return normalize(trim(o));}
// A lower level of detail by name (Body_LOD1, Body_LOD3 …). The plain name and _LOD0 are the full mesh.
inline bool lowerLod(const std::string& s){std::string o;for(unsigned char c:s)o+=char(c>='A'&&c<='Z'?c+32:c);for(const char* junk:{"(clone)"," instance"}){size_t p;while((p=o.find(junk))!=o.npos)o.erase(p,std::strlen(junk));}o=trim(o);
 auto p=o.rfind("_lod");if(p==o.npos||p+4>=o.size()||o.find_first_not_of("0123456789",p+4)!=o.npos)return false;return o.find_first_not_of('0',p+4)!=o.npos;}
inline bool matches(const Fingerprint& t,const Fingerprint& f){return t.vertices==f.vertices&&t.submeshes==f.submeshes&&t.bones==f.bones&&(t.mesh.empty()?normalize(t.renderer)==normalize(f.renderer):meshName(t.mesh)==meshName(f.mesh));}

// Switchable variables of a model (GIBox model format, "variants"): each with its values, and the value it starts with.
// combos: only for version 1 files, whose combinations are the only valid assignments.
using Assignment=std::map<std::string,int>;
struct Variants {std::vector<std::pair<std::string,std::vector<int>>> vars;Assignment defaults;std::vector<Assignment> combos;};
// When a draw is drawn: the variables it depends on and the value tuples it is drawn for. No variables: always.
struct When {std::vector<std::string> vars;std::set<std::vector<int>> allow;};
inline bool drawn(const When& w,const Assignment& a){if(w.vars.empty())return true;std::vector<int> t;for(auto& n:w.vars){auto it=a.find(n);if(it==a.end())return false;t.push_back(it->second);}return w.allow.count(t)>0;}
inline int integer(const std::string& s){require(!s.empty()&&s.size()<11&&s.find_first_not_of("-0123456789")==s.npos&&s.find('-',1)==s.npos&&s!="-","整数无效："+s);return std::stoi(s);}
// The values in effect: the chosen ones where the variable has that value, else where it starts. A version 1 model
// knows only its listed combinations: any other assignment is its default one.
inline Assignment pick(const Variants& v,const std::map<std::string,int>& chosen){Assignment a=v.defaults;
 for(auto& x:v.vars){auto c=chosen.find(x.first);if(c!=chosen.end()&&std::find(x.second.begin(),x.second.end(),c->second)!=x.second.end())a[x.first]=c->second;}
 if(!v.combos.empty()&&std::find(v.combos.begin(),v.combos.end(),a)==v.combos.end()){a=v.defaults;}return a;}
}
