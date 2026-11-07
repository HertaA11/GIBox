// SPDX-License-Identifier: GPL-3.0-only
// GIBox 3DMigoto logic core: reading INI files the way 3DMigoto does.
// Ported from 3DMigoto (XXMI fork) DirectX11/IniHandler.cpp at a7847eae (ParseIniStream, ParseIniSectionLine,
// ParseIniKeyValLine, ParseIniPreamble, get_namespaced_section_name, the section lists); see
// third_party/3dmigoto/README.md. No DirectX, Windows, Unity, JNI or hook code: text in, sections out.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace migoto {

inline std::string lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
inline std::string trim(const std::string& s,const char* ws=" \t"){auto a=s.find_first_not_of(ws);if(a==std::string::npos)return "";return s.substr(a,s.find_last_not_of(ws)-a+1);}
inline bool startsWith(const std::string& s,const std::string& p){return s.compare(0,p.size(),p)==0;}
inline bool istarts(const std::string& s,const std::string& p){return s.size()>=p.size()&&lower(s.substr(0,p.size()))==lower(p);}

// What the core could not do, or did differently from 3DMigoto on PC. Limitation marks a context Android does not
// have (a D3D draw value, a shader, a cursor): those are reported, never stood in for silently.
struct Diagnostic {enum Level {Notice,Warning,Error,Limitation} level;std::string where,message;};
struct Diagnostics {
 std::vector<Diagnostic> list;
 void add(Diagnostic::Level l,const std::string& where,const std::string& m){for(auto& d:list)if(d.level==l&&d.where==where&&d.message==m)return;list.push_back({l,where,m});}
 size_t count(Diagnostic::Level l)const{size_t n=0;for(auto& d:list)n+=d.level==l;return n;}
};

// One line of a section: key and value with the whitespace around the first '=' stripped, the whole line, and the
// namespace it came from (global sections such as [Present] collect lines from many files).
struct Line {std::string key,val,raw,ns,file;int number=0;bool hasEquals=false;};
struct Section {std::string name,ns,path;std::vector<Line> lines;};

struct SectionKind {const char* name;bool prefix;};
// IniHandler.cpp CommandListSections and RegularSections, in the same order (the first matching prefix names it).
inline const std::vector<SectionKind>& commandListSections(){static const std::vector<SectionKind> v={{"TextureOverride",true},{"CommandList",true},{"Constants",false},{"Present",false},
 {"ShaderOverride",true},{"CustomShader",true},{"ShaderRegex",true},{"BuiltInCommandList",true},{"BuiltInCustomShader",true},{"ClearRenderTargetView",false},
 {"ClearDepthStencilView",false},{"ClearUnorderedAccessViewUint",false},{"ClearUnorderedAccessViewFloat",false}};return v;}
inline const std::vector<SectionKind>& regularSections(){static const std::vector<SectionKind> v={{"Pool",true},{"Resource",true},{"Key",true},{"Include",true},{"Preset",true},
 {"Hunting",false},{"Logging",false},{"System",false},{"Input",false},{"Device",false},{"Rendering",false},{"Loader",false},{"Profile",false},{"Stereo",false},{"ConvergenceMap",false}};return v;}
inline bool inList(const std::string& s,const std::vector<SectionKind>& list){for(auto& k:list)if(k.prefix?istarts(s,k.name):lower(s)==lower(k.name))return true;return false;}
inline bool isCommandListSection(const std::string& s){return inList(s,commandListSections());}
inline const char* sectionPrefix(const std::string& s){for(auto* list:{&commandListSections(),&regularSections()})for(auto& k:*list)if(k.prefix&&istarts(s,k.name))return k.name;return nullptr;}
// "TextureOverrideFoo" in namespace "Mods\X\a.ini" is "TextureOverride\Mods\X\a.ini\Foo"; sections without a prefix
// ([Present], [Constants]) are global and are never namespaced.
inline bool namespacedSection(const std::string& section,const std::string& ns,std::string& out){const char* p=sectionPrefix(section);if(!p)return false;
 size_t n=std::string(p).size();out=std::string(p)+"\\"+ns+"\\"+section.substr(n);return true;}
inline std::string namespacedVar(const std::string& lowName,const std::string& ns){return "$\\"+lower(ns)+"\\"+lowName.substr(1);}

struct IniFile {std::string path,text;};   // path: relative to the 3DMigoto folder, e.g. "Mods\\Traveler\\Anim.ini"; "" for d3dx.ini

// All sections of all files. Lookup is case-insensitive (3DMigoto lowercases section ids for lookups).
struct IniSet {
 std::map<std::string,Section> sections;   // key: lower(name)
 std::vector<std::string> order;            // keys, in first-seen order
 Diagnostics* diag=nullptr;
 void warn(const std::string& where,const std::string& m){if(diag)diag->add(Diagnostic::Warning,where,m);}
 const Section* find(const std::string& name)const{auto it=sections.find(lower(name));return it==sections.end()?nullptr:&it->second;}
 // condition: the include condition evaluator (a static expression in the file's namespace); null accepts every file.
 void add(const IniFile& f,const std::function<bool(const std::string&,const std::string&)>& condition=nullptr){
  std::vector<std::string> text;for(size_t pos=0;;){size_t end=f.text.find('\n',pos);if(end==std::string::npos){text.push_back(f.text.substr(pos));break;}text.push_back(f.text.substr(pos,end-pos));pos=end+1;}
  if(!text.empty()&&startsWith(text[0],"\xEF\xBB\xBF"))text[0]=text[0].substr(3);
  std::string ns=f.path;const std::string& path=f.path;bool preamble=true,noEquals=false;Section* current=nullptr;
  for(size_t n=0;n<text.size();n++){std::string line=trim(text[n]," \t\r");
   if(line.empty()||line[0]==';')continue;   // a ';' later in a line is not a comment
   if(line[0]=='['){preamble=false;size_t last=line.find(']');if(last==std::string::npos)last=line.size();std::string name=trim(line.substr(1,last-1)),full=name;
    // Included files may add lines to a global section; a namespaced section, or any section of d3dx.ini, is
    // taken only the first time (GetPrivateProfileString behaviour).
    bool namespaced=!ns.empty()&&namespacedSection(name,ns,full),appendable=!ns.empty()&&!namespaced;std::string key=lower(full);auto it=sections.find(key);
    if(it!=sections.end()&&!appendable){warn(path,"段重复：["+full+"]");current=nullptr;continue;}
    if(it==sections.end()){Section s;s.name=full;if(namespaced){s.ns=ns;s.path=path;}it=sections.emplace(key,s).first;order.push_back(key);}
    current=&it->second;
    if(!isCommandListSection(name)&&!inList(name,regularSections()))warn(path,"未知的段类型：["+name+"]");
    noEquals=isCommandListSection(name)||istarts(name,"ShaderRegex")||lower(name)=="profile";continue;}
   size_t eq=line.find('=');
   if(preamble){
    if(eq!=std::string::npos){std::string k=lower(trim(line.substr(0,eq))),v=trim(line.substr(eq+1));
     if(k=="condition"){if(condition&&!condition(v,ns)){warn(path,"condition = false, skipping \""+ns+"\"");return;}continue;}
     if(k=="namespace"){ns=v;continue;}}
    warn(path,"条目不在任何段内："+line);continue;}
   if(!current)continue;   // the lines of a duplicate section that was not taken
   Line l;l.raw=line;l.ns=ns;l.file=path;l.number=int(n+1);
   if(eq!=std::string::npos){l.hasEquals=true;l.key=trim(line.substr(0,eq));l.val=trim(line.substr(eq+1));
    if(l.val.empty()){warn(path,"条目没有值：\""+line+"\"（["+current->name+"]）");continue;}}
   else if(!noEquals){warn(path,"格式错误的行：\""+line+"\"（["+current->name+"]）");continue;}
   current->lines.push_back(l);}
 }
 // Sections whose name starts with a prefix, sorted the way the std::map in 3DMigoto iterates them.
 std::vector<const Section*> withPrefix(const std::string& prefix)const{std::vector<const Section*> out;for(auto& kv:sections)if(istarts(kv.second.name,prefix))out.push_back(&kv.second);return out;}
};

}  // namespace migoto
