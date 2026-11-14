#include "loading_images.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace ms;
namespace {
struct Color {float r,g,b,a;};struct Rect {float x,y,w,h;};struct Pivot {float x,y;};
struct Object {Ptr p=0,sprite=0,go=0;Color color{.3f,.4f,.5f,1};int type=1;bool aspect=false,active=true;};
std::map<Ptr,Object> objects;std::vector<void*> allocations;std::map<unsigned,Ptr> handles;unsigned handle=0,destroyed=0,created=0;Ptr descriptor=0;Rect viewport{0,0,4,1},lastCrop{};int wrap=-1,filter=-1;
Ptr object(){auto* bytes=std::calloc(1,512);assert(bytes);allocations.push_back(bytes);Ptr p=reinterpret_cast<Ptr>(bytes);store<Ptr>(p,p+0x100);store<Ptr>(p+0x10,p+0x180);objects[p].p=p;return p;}
Ptr go(Ptr p,Ptr){return objects.at(p).go;}Ptr sprite(Ptr p,Ptr){return objects.at(p).sprite;}void setSprite(Ptr p,Ptr v,Ptr){objects.at(p).sprite=v;}
int type(Ptr p,Ptr){return objects.at(p).type;}void setType(Ptr p,int v,Ptr){objects.at(p).type=v;}
uint32_t aspect(Ptr p,Ptr){return objects.at(p).aspect;}void setAspect(Ptr p,int v,Ptr){objects.at(p).aspect=v!=0;}
Color color(Ptr p,Ptr){return objects.at(p).color;}void setColor(Ptr p,Color c,Ptr){objects.at(p).color=c;}
uint32_t imageActive(Ptr p,Ptr){return objects.at(p).active&&objects.at(objects.at(p).go).active;}
uint32_t active(Ptr p,Ptr){return objects.at(p).active;}void setActive(Ptr p,int v,Ptr){objects.at(p).active=v!=0;}
uint32_t pin(Ptr p,int kind,Ptr){assert(p&&kind==2);handles[++handle]=p;return handle;}void unpin(uint32_t h){assert(handles.erase(h)==1);}
Ptr textureType(){if(!descriptor)descriptor=object();return descriptor;}Ptr newTexture(Ptr){Ptr p=object();store<Ptr>(p+0x10,0);return p;}
void init(Ptr p,int w,int h,int format,int mip,int linear,Ptr){assert(w==2&&h==2&&format==4&&mip==0&&linear==0);store<Ptr>(p+0x10,p+0x180);created++;}
void pixels(Ptr,Ptr bytes,int n,Ptr){assert(bytes&&n==16);}void action(Ptr,Ptr){}
Ptr rectTransform(Ptr p,Ptr){return p;}Rect bounds(Ptr,Ptr){return viewport;}
void setWrap(Ptr,int value,Ptr){wrap=value;}void setFilter(Ptr,int value,Ptr){filter=value;}
Ptr createSprite(Ptr texture,Rect r,Pivot p,Ptr){assert(texture&&r.w>0&&r.h>0&&p.x==.5f&&p.y==.5f);lastCrop=r;return object();}
void destroy(Ptr p,Ptr){destroyed++;store<Ptr>(p+0x10,0);}
Ptr resolve(abi::Offset o){switch(o){
#define F(a,b) case abi::a:return reinterpret_cast<Ptr>(&b)
 F(graphicRect,rectTransform);F(rectBounds,bounds);F(textureWrap,setWrap);F(textureFilter,setFilter);
 F(gameObject,go);F(imageSprite,sprite);F(imageSetSprite,setSprite);F(imageType,type);F(imageSetType,setType);F(imageAspect,aspect);F(imageSetAspect,setAspect);F(graphicColor,color);F(graphicSetColor,setColor);
 F(imageActive,imageActive);F(goActive,active);F(goActiveSelf,active);F(goSetActive,setActive);F(pin,pin);F(unpin,unpin);F(textureType,textureType);F(objectNew,newTexture);F(textureInit,init);F(texturePixels,pixels);F(textureApply,action);F(spriteCreate,createSprite);F(destroy,destroy);
#undef F
 default:assert(false);return 0;}}
}
int main(){std::string pattern=std::string(std::getenv("TMPDIR")?std::getenv("TMPDIR"):"/tmp")+"/gibox-loading-XXXXXX";assert(mkdtemp(&pattern[0]));auto root=pattern+"/models";std::filesystem::create_directories(root+"/Screens/images");
 std::string manifest=R"J({"format":"gibox-loading-images","version":1,"hideUi":true,"images":[{"file":"images/0.rgba","width":2,"height":2},{"file":"images/1.rgba","width":2,"height":2}]})J";
 write(root+"/Screens/loading.json",manifest);Bytes data(16,127);write(root+"/Screens/images/0.rgba",data.data(),data.size());write(root+"/Screens/images/1.rgba",data.data(),data.size());LoadingReader reader;
 auto prepared=reader.update(root,{"Screens"},true,{},0);assert(prepared&&prepared->error.empty()&&prepared->hide&&prepared->image.pixels==data);assert(!reader.update(root,{"Screens"},true,{},0));
 auto next=reader.update(root,{"Screens"},true,{},1);assert(next&&next->cycle==1);auto policy=reader.update(root,{"Screens"},true,{{"Screens",{{"$skip_ui",0}}}},1);assert(policy&&!policy->hide&&policy->index==next->index);auto off=reader.update(root,{"Screens"},false,{},1);assert(off&&off->name.empty());
 Engine e(0);e.resolver=resolve;LoadingScreens loading(e);Ptr canvas=object(),image=object(),original=object(),canvasGo=object(),imageGo=object(),tip=object(),tipGo=object();objects[canvas].go=canvasGo;objects[image].go=imageGo;objects[image].sprite=original;objects[tip].go=tipGo;store<Ptr>(canvas+0x18,tip);
 loading.observe(canvas,image);loading.receive(prepared);loading.tick(true,{"Screens"});assert(loading.active()&&objects[image].sprite!=original&&!objects[tipGo].active&&objects[image].type==0&&objects[image].aspect);
 assert(wrap==1&&filter==1&&lastCrop.w==2&&lastCrop.h==2);
 unsigned n=created;for(int i=0;i<20;i++)loading.tick(true,{"Screens"});assert(created==n);
 loading.event(image,false);assert(objects[image].sprite==original&&objects[tipGo].active&&objects[image].type==1&&!objects[image].aspect&&loading.cycle==1&&destroyed==2);
 loading.receive(next);loading.event(image,true);loading.tick(true,{"Screens"});assert(loading.active());loading.tick(false,{"Screens"});assert(objects[image].sprite==original&&objects[tipGo].active);
 // UI policy can change while visible; a disabled mod never modifies an unrelated selected entry.
 next->key+="-keep-ui";next->hide=false;loading.receive(next);loading.tick(true,{"Screens"});assert(loading.active()&&objects[tipGo].active);loading.tick(true,{"Other"});assert(objects[image].sprite==original);
 loading.tick(true,{"Screens"},"cover");assert(lastCrop.w==2&&lastCrop.h==.5f&&lastCrop.y==.75f&&!objects[image].aspect);
 n=created;loading.tick(true,{"Screens"},"cover");assert(created==n);
 viewport={0,0,1,4};loading.tick(true,{"Screens"},"cover");assert(lastCrop.w==.5f&&lastCrop.h==2&&lastCrop.x==.75f);
 loading.tick(true,{"Screens"},"stretch");assert(lastCrop.w==2&&lastCrop.h==2&&!objects[image].aspect);
 loading.tick(true,{"Screens"},"original");assert(objects[image].aspect&&lastCrop.w==2&&lastCrop.h==2);
 loading.shutdown();assert(handles.empty()&&objects[image].color.r==.3f);
 write(root+"/Screens/loading.json",std::string(manifest).replace(manifest.find("images/0.rgba"),13,"../bad/0.rgba"));LoadingReader bad;auto invalid=bad.update(root,{"Screens"},true,{},0); // check both rows, regardless of the random choice
 if(invalid->error.empty()){std::filesystem::remove(root+"/Screens/images/1.rgba");invalid=bad.update(root,{"Screens"},true,{},1);}assert(!invalid->error.empty());
 std::filesystem::remove_all(pattern);for(auto p:allocations)std::free(p);std::cout<<"Loading images: PASS (one image per cycle, strict paths, pooled UI activation, aspect/color/UI restoration, bounded resources)\n";
}
