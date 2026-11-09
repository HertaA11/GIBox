#pragma once
// Index buffers as the game uploads them through Vulkan, hashed the way 3DMigoto hashes a D3D11 buffer (0.7.56): a
// read-only diagnostic, off by default (control.json textureMods.ibHash, read once at start). It answers one question:
// are this build's index buffers byte for byte the PC ones, so that a GIMI hash such as "hash = 773fbe7b" can be
// recomputed on Android? Nothing here changes what the game draws.
//
// 3DMigoto (XXMI-Libs-Package a7847eae, DirectX11/HackerDevice.cpp CreateBuffer):
//   hash = data_hash = crc32c_hw(0, pInitialData->pSysMem, pDesc->ByteWidth);
//   hash = crc32c_hw(hash, pDesc, sizeof(D3D11_BUFFER_DESC));
// crc32c_hw (util.h) is crc32c_append (crc32c-hw-1.0.5): CRC-32C, the seed and the result inverted, so appending
// continues the same CRC. The PC desc is not stored anywhere on Android; the 0.7.57 device capture solved it from the
// rule hashes of two mods: ByteWidth = the data's size, Usage DEFAULT, BindFlags INDEX_BUFFER | SHADER_RESOURCE |
// UNORDERED_ACCESS (0x8A, Linnea: head/body/dress, bangs, eyes, book, crystal) or INDEX_BUFFER alone (0x2, the
// Traveler), CPU access, misc flags and stride 0. Both are hashed, IMMUTABLE too, for the data as uploaded, widened from
// 16 to 32 bit, or narrowed from 32 to 16 bit.
//
// Modes (1.0.3, control.json textureMods.hashMode, read at the game's start), after XXMI's normal play and its hunting /
// frame analysis: "match" hashes each upload once, as uploaded (every hash this device ever tied to a PC one was that
// form), and checks it against the mods' rules; "collect" also ties every shown mesh (characters, NPCs, monsters) to
// its upload for hash-map.json; "full" is the old diagnostic: every form, ib-hashes.txt, per-mesh candidate lines.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(__aarch64__)
#include <arm_acle.h>
#include <sys/auxv.h>
#endif

namespace ms::vkcap {
enum class Mode {Off=0,Match=1,Collect=2,Full=3};
// hashMode when given; else the switches of 1.0.2 and earlier: capture -> collect, ibHash -> match.
inline Mode modeFrom(const std::string& mode,bool ibHash,bool capture){
 if(mode=="off"){return Mode::Off;}if(mode=="match"){return Mode::Match;}if(mode=="collect"){return Mode::Collect;}if(mode=="full"){return Mode::Full;}
 return capture?Mode::Collect:ibHash?Mode::Match:Mode::Off;}
inline const char* modeName(Mode m){return m==Mode::Match?"匹配":m==Mode::Collect?"采集":m==Mode::Full?"完整诊断":"关闭";}
inline const std::array<uint32_t,256>& crcTable(){static const auto table=[]{std::array<uint32_t,256> t{};
  for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c&1)?(c>>1)^0x82F63B78u:c>>1;t[i]=c;}return t;}();return table;}
#if defined(__aarch64__)
__attribute__((target("crc"))) inline uint32_t crcArm(uint32_t c,const uint8_t* p,size_t n){
 for(;n&&(reinterpret_cast<uintptr_t>(p)&7);n--)c=__crc32cb(c,*p++);
 for(;n>=8;n-=8,p+=8){uint64_t v;std::memcpy(&v,p,8);c=__crc32cd(c,v);}
 for(;n;n--)c=__crc32cb(c,*p++);return c;}
inline bool crcHardware(){static const bool on=(getauxval(AT_HWCAP)&(1ul<<7))!=0;return on;}   // HWCAP_CRC32
#endif
// crc32c_append: crc32c(crc32c(0,a),b) is the CRC of a followed by b.
inline uint32_t crc32c(uint32_t seed,const void* data,size_t n){auto* p=static_cast<const uint8_t*>(data);uint32_t c=~seed;
#if defined(__aarch64__)
 if(crcHardware())return ~crcArm(c,p,n);
#endif
 auto& t=crcTable();for(size_t i=0;i<n;i++)c=t[(c^p[i])&0xff]^(c>>8);return ~c;}

struct D3D11BufferDesc {uint32_t byteWidth=0,usage=0,bindFlags=0,cpuAccessFlags=0,miscFlags=0,structureByteStride=0;};
static_assert(sizeof(D3D11BufferDesc)==24,"D3D11_BUFFER_DESC is 24 bytes");
// variant = format * 4 + kind; format: 0 as uploaded, 1 widened 16->32 bit, 2 narrowed 32->16 bit; kind: 0 the data
// alone (3DMigoto's data_hash), 1 DEFAULT + INDEX_BUFFER, 2 DEFAULT + INDEX_BUFFER | SHADER_RESOURCE | UNORDERED_ACCESS,
// 3 IMMUTABLE + INDEX_BUFFER.
struct Hash {uint32_t value=0;uint8_t variant=0;};
inline std::string variantName(uint8_t v){static const char* formats[]={"as uploaded","16-bit widened to 32-bit","32-bit narrowed to 16-bit"};
 static const char* kinds[]={"data only","desc DEFAULT, Bind=IB","desc DEFAULT, Bind=IB|SRV|UAV","desc IMMUTABLE, Bind=IB"};
 return std::string(formats[(v>>2)%3])+", "+kinds[v&3];}
inline void hashFormat(const uint8_t* p,size_t n,uint8_t format,std::vector<Hash>& out){uint32_t data=crc32c(0,p,n);out.push_back({data,uint8_t(format*4)});
 const uint32_t kinds[3][2]={{0,0x2},{0,0x8A},{1,0x2}};   // Usage, BindFlags
 for(uint8_t k=0;k<3;k++){D3D11BufferDesc d;d.byteWidth=uint32_t(n);d.usage=kinds[k][0];d.bindFlags=kinds[k][1];out.push_back({crc32c(data,&d,sizeof d),uint8_t(format*4+k+1)});}}
inline std::vector<Hash> ibHashes(const uint8_t* p,size_t n){std::vector<Hash> out;hashFormat(p,n,0,out);
 if(n%2==0){std::vector<uint8_t> w(n*2);for(size_t i=0;i<n/2;i++){uint16_t v;std::memcpy(&v,p+2*i,2);uint32_t x=v;std::memcpy(&w[4*i],&x,4);}hashFormat(w.data(),w.size(),1,out);}
 if(n%4==0){std::vector<uint8_t> s(n/2);bool fits=true;for(size_t i=0;fits&&i<n/4;i++){uint32_t v;std::memcpy(&v,p+4*i,4);fits=v<=0xffff;uint16_t x=uint16_t(v);std::memcpy(&s[2*i],&x,2);}
  if(fits)hashFormat(s.data(),s.size(),2,out);}
 return out;}

// One index upload: where it came from, its hashes and enough of its content to compare by hand.
struct Blob {uint64_t seq=0,buffer=0,offset=0;uint32_t bytes=0;std::string via;std::vector<Hash> hashes;std::vector<uint16_t> head16;uint32_t max16=0,max32=0;};
struct Stats {uint64_t uploads=0,bytes=0,copies=0,mappedBinds=0,notMapped=0,outOfRange=0,tooLarge=0,dropped=0,resolved=0,wrapped=0;bool installed=false;std::string error;};
struct Copy {uint64_t srcOffset=0,dstOffset=0,size=0;};   // VkBufferCopy

// The buffers, memory bindings and host mappings needed to find index data in CPU memory: a staging buffer copied
// into an index buffer (read when the copy is recorded), or an index buffer in mapped memory (read at its first bind).
class Tracker {
 struct Buffer {uint64_t size=0,memory=0,memoryOffset=0;uint32_t usage=0;bool bound=false,captured=false;};
 struct Mapping {const uint8_t* base=nullptr;uint64_t offset=0,size=0;};
 std::mutex m;std::unordered_map<uint64_t,Buffer> buffers;std::unordered_map<uint64_t,Mapping> maps;std::vector<Blob> out;Stats st;uint64_t seq=0;
 static constexpr uint32_t indexUsage=0x40,transferSrc=0x1;static constexpr size_t maxBlob=64u<<20,maxQueue=65536;
 // Host address of [memory offset, +size) or null when that range is not mapped.
 const uint8_t* host(const Buffer& b,uint64_t at,uint64_t size){auto x=maps.find(b.memory);if(x==maps.end()||!x->second.base)return nullptr;uint64_t from=b.memoryOffset+at;
  if(from<x->second.offset||(x->second.size!=~0ull&&from+size>x->second.offset+x->second.size)){return nullptr;}return x->second.base+(from-x->second.offset);}
 // Under the lock: an unmap waits until the bytes are read.
 void add(const uint8_t* p,uint64_t n,uint64_t buffer,uint64_t offset,const char* via){st.uploads++;st.bytes+=n;if(out.size()>=maxQueue){st.dropped++;return;}
  Blob b;b.seq=++seq;b.buffer=buffer;b.offset=offset;b.bytes=uint32_t(n);b.via=via;if(allForms)b.hashes=ibHashes(p,size_t(n));else hashFormat(p,size_t(n),0,b.hashes);
  for(size_t i=0;i<n/2;i++){uint16_t v;std::memcpy(&v,p+2*i,2);if(i<8)b.head16.push_back(v);if(v>b.max16)b.max16=v;}
  if(n%4==0)for(size_t i=0;i<n/4;i++){uint32_t v;std::memcpy(&v,p+4*i,4);if(v>b.max32)b.max32=v;}
  out.push_back(std::move(b));}
public:
 std::atomic<bool> enabled{false};
 std::atomic<bool> allForms{true};   // false (match / collect): the data as uploaded only, one pass over it
 void created(uint64_t buffer,uint64_t size,uint32_t usage){if(!(usage&(indexUsage|transferSrc)))return;std::lock_guard<std::mutex> l(m);Buffer b;b.size=size;b.usage=usage;buffers[buffer]=b;}
 void destroyed(uint64_t buffer){std::lock_guard<std::mutex> l(m);buffers.erase(buffer);}
 void bound(uint64_t buffer,uint64_t memory,uint64_t offset){std::lock_guard<std::mutex> l(m);auto b=buffers.find(buffer);if(b==buffers.end())return;b->second.memory=memory;b->second.memoryOffset=offset;b->second.bound=true;}
 void mapped(uint64_t memory,uint64_t offset,uint64_t size,const void* p){std::lock_guard<std::mutex> l(m);maps[memory]={static_cast<const uint8_t*>(p),offset,size};}
 void unmapped(uint64_t memory){std::lock_guard<std::mutex> l(m);maps.erase(memory);}
 // A copy into an index buffer: each region's source bytes, as written into the staging memory before the copy.
 void copied(uint64_t src,uint64_t dst,const Copy* regions,uint32_t count){
  std::lock_guard<std::mutex> l(m);auto d=buffers.find(dst);if(d==buffers.end()||!(d->second.usage&indexUsage))return;d->second.captured=true;auto s=buffers.find(src);
   for(uint32_t i=0;i<count;i++){const Copy& r=regions[i];st.copies++;
    if(s==buffers.end()||!s->second.bound){st.notMapped++;continue;}if(r.size>maxBlob){st.tooLarge++;continue;}
    if(r.srcOffset+r.size>s->second.size||r.dstOffset+r.size>d->second.size){st.outOfRange++;continue;}
    const uint8_t* p=host(s->second,r.srcOffset,r.size);if(!p){st.notMapped++;continue;}add(p,r.size,dst,r.dstOffset,"staging copy");}}
 // An index buffer bound for drawing that no copy filled: read from its mapped memory once, offset to its end.
 void indexBound(uint64_t buffer,uint64_t offset){
  std::lock_guard<std::mutex> l(m);auto b=buffers.find(buffer);if(b==buffers.end()||b->second.captured||!b->second.bound||!(b->second.usage&indexUsage)||offset>=b->second.size)return;b->second.captured=true;
  uint64_t n=b->second.size-offset;if(n>maxBlob){st.tooLarge++;return;}const uint8_t* p=host(b->second,offset,n);if(!p){st.notMapped++;return;}st.mappedBinds++;
  add(p,n,buffer,offset,"mapped memory at its first bind (offset to the buffer's end)");}
 void resolvedName(bool wrapped){std::lock_guard<std::mutex> l(m);st.resolved++;if(wrapped)st.wrapped++;}
 void installed(bool ok,const std::string& error){std::lock_guard<std::mutex> l(m);st.installed=ok;st.error=error;enabled=ok;}
 std::vector<Blob> take(){std::lock_guard<std::mutex> l(m);std::vector<Blob> r;r.swap(out);return r;}
 Stats stats(){std::lock_guard<std::mutex> l(m);return st;}
};
inline Tracker& tracker(){static Tracker t;return t;}
}
// vk_hook.cpp (Android only): hooks the Vulkan loader's lookups; detail says what was hooked or why not.
namespace ms::vkhook {bool install(std::string& detail);}
