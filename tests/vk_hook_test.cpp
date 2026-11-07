// The one-instruction redirect of vk_hook.cpp, checked as bytes on the host (the code is ARM64 and is not run here):
// a [bti c] + b stub keeps its landing pad, its b goes to our page, our page goes on to the hook and back to the target;
// both entries are ready before either stub is written.
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
static int patches=0;
extern "C" int DobbyCodePatch(void* address,uint8_t* buffer,uint32_t size){std::memcpy(address,buffer,size);patches++;return 0;}
#include "../app/src/main/cpp/vk_hook.cpp"
#include <cassert>
#include <iostream>
static uintptr_t target(uint32_t w,uintptr_t at){int64_t imm=int64_t(w&0x03FFFFFFu);if(imm&0x02000000)imm-=0x04000000;return uintptr_t(int64_t(at)+imm*4);}
int main(){
 const size_t page=size_t(sysconf(_SC_PAGESIZE));auto* code=static_cast<uint32_t*>(mmap(nullptr,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(code!=MAP_FAILED);
 // bti c; b +0x1000 (the loader's internal function, reached only by this direct branch); a plain "b" stub next to it.
 code[0]=0xD503245Fu;code[1]=0x14000000u|(0x1000/4);uintptr_t stub=reinterpret_cast<uintptr_t>(code),internal=stub+4+0x1000;
 code[64]=0x14000000u|8;uintptr_t plain=reinterpret_cast<uintptr_t>(code+64);
 uint32_t* island=islandNear(stub);assert(island);std::string why;int hook=0,hook2=0;Redirect a,b;
 assert(prepare(stub,&hook,island,a,why)&&prepare(plain,&hook2,island+8,b,why)&&patches==0&&code[1]==(0x14000000u|(0x1000/4)));   // nothing in the stubs yet
 assert(patch(island,{&a,&b},why)&&patches==2&&a.original==island+4&&b.original==island+12);
 assert(code[0]==0xD503245Fu&&(code[1]&0xFC000000u)==0x14000000u&&target(code[1],stub+4)==reinterpret_cast<uintptr_t>(island));   // landing pad kept, b -> island
 uint64_t h;std::memcpy(&h,island+2,8);assert(island[0]==0x58000051u&&island[1]==0xD61F0220u&&h==reinterpret_cast<uint64_t>(&hook));   // ldr x17,#8; br x17; .quad hook
 assert((island[4]&0xFC000000u)==0x14000000u&&target(island[4],reinterpret_cast<uintptr_t>(island+4))==internal);   // the original: b internal
 assert(a.what.find("入口 d503245f 14000400")==0&&a.what.find("+4 处的 b")!=std::string::npos);
 assert(target(code[64],plain)==reinterpret_cast<uintptr_t>(island+8)&&target(island[12],reinterpret_cast<uintptr_t>(island+12))==plain+32&&b.what.find("+0 处的 b")!=std::string::npos);
 // Anything else is refused and left as it was.
 code[128]=0xA9BF7BFDu;code[129]=0x910003FDu;Redirect c;assert(!prepare(reinterpret_cast<uintptr_t>(code+128),&hook,island+16,c,why)&&why.find("不是 [bti] + b 形式的入口，开头为 a9bf7bfd 910003fd")==0&&code[128]==0xA9BF7BFDu);
 std::cout<<"Vulkan stub redirect: PASS (landing pad kept, direct b to our page and back, both ready before writing, other shapes refused)\n";}
