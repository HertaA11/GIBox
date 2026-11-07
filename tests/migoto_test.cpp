// SPDX-License-Identifier: GPL-3.0-only
// Host tests of the 3DMigoto logic core (app/src/main/cpp/migoto). The expected values follow the upstream code
// paths at XXMI a7847eae named in each case; they are simulated runs, not runs of 3DMigoto itself.
#include "migoto/adapters.hpp"
#include "migoto/rehearsal.hpp"
#include "model.hpp"
#include <cstdio>
#include <iostream>
#include <sstream>
using namespace migoto;

static int failures=0;
#define CHECK(c) do{if(!(c)){std::cerr<<__FILE__<<":"<<__LINE__<<": CHECK failed: "<<#c<<"\n";failures++;}}while(0)
#define CHECK_EQ(a,b) do{auto va=(a);auto vb=(b);if(!(va==vb)){std::ostringstream o;o<<va<<" != "<<vb;std::cerr<<__FILE__<<":"<<__LINE__<<": "<<#a<<" == "<<#b<<" failed: "<<o.str()<<"\n";failures++;}}while(0)

struct Plain:Context {int randoms=0;float keywordValue(const Keyword&)override{return 0;}float slotFilter(const std::string&)override{return 0;}
 float param(int,char)override{return 0;}float randomValue(float m)override{randoms++;return m/2;}};
static float eval(const std::string& t,Plain* ctx=nullptr){Var a{"$a",3,0};Resolver r=[&](const std::string& n)->Var*{return n=="$a"?&a:nullptr;};Plain p;return parseExpression(t,r,true)(ctx?*ctx:p);}
static bool syntaxError(const std::string& t){try{eval(t);}catch(const SyntaxError&){return true;}return false;}
static uint32_t bits(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}
static float var(Engine& e,const std::string& n){auto it=e.globals.find(n);if(it==e.globals.end()){std::cerr<<"no variable "<<n<<"\n";failures++;return -12345;}return it->second.value;}
static size_t count(const Engine& e,Diagnostic::Level l,const std::string& text){size_t n=0;for(auto& d:e.diag.list)n+=d.level==l&&(d.message.find(text)!=std::string::npos||d.where.find(text)!=std::string::npos);return n;}
static Engine loaded(const std::vector<IniFile>& files){Engine e;e.load(files);e.start();return e;}
static DrawInfo drawOf(std::map<std::string,Ref> b,std::vector<std::string> checks,std::map<std::string,float> call={}){DrawInfo d;d.bindings=std::move(b);d.checks=std::move(checks);d.call=std::move(call);return d;}

// CommandList.cpp DEFINE_OPERATOR table, the pass order in CommandListExpression::parse, ParseFloatToken.
static void expressions(){
 CHECK_EQ(eval("1+2*3"),7.0f);CHECK_EQ(eval("-2**2"),4.0f);CHECK_EQ(eval("2**-1"),0.5f);CHECK_EQ(eval("2**3**2"),512.0f);   // unary binds first; ** is right-assoc
 CHECK_EQ(eval("7//2"),3.0f);CHECK_EQ(eval("-7//2"),-4.0f);CHECK_EQ(eval("7%2.5"),2.0f);CHECK_EQ(eval("-7%3"),-1.0f);          // floor(l/r), fmod
 CHECK(std::isinf(eval("1/0")));CHECK_EQ(eval("(0/0) == (0/0)"),0.0f);CHECK_EQ(eval("(0/0) != (0/0)"),1.0f);
 CHECK_EQ(eval("-0.0 == 0"),1.0f);CHECK_EQ(eval("-0.0 === 0"),0.0f);CHECK_EQ(eval("-0.0 !== 0"),1.0f);                          // === compares bits
 CHECK_EQ(eval("0.1 + 0.2 == 0.3"),1.0f);           // true in 32-bit float, false in double
 CHECK_EQ(eval("16777217 == 16777216"),1.0f);       // the literal itself rounds to float
 CHECK_EQ(eval("0.5 * 3"),1.5f);CHECK_EQ(eval("$a % 0.5 == 0"),1.0f);CHECK_EQ(eval("$a / 2"),1.5f);   // not integer division
 CHECK_EQ(eval("~0"),-1.0f);CHECK_EQ(eval("5 & 3"),1.0f);CHECK_EQ(eval("5 | 3"),7.0f);CHECK_EQ(eval("5 ^ 3"),6.0f);CHECK_EQ(eval("2.7 & 3"),2.0f);
 CHECK_EQ(eval("1 << 31"),-2147483648.0f);CHECK_EQ(eval("-8 >> 1"),-4.0f);
 CHECK_EQ(eval("1 + 2 << 1"),6.0f);CHECK_EQ(eval("2 & 2 == 2"),0.0f);CHECK_EQ(eval("1 | 2 == 2"),1.0f);CHECK_EQ(eval("6 ^ 2 & 3"),4.0f);CHECK_EQ(eval("1 | 2 && 0"),0.0f);CHECK_EQ(eval("1 || 0 && 0"),1.0f);
 CHECK_EQ(eval("1 < 2 < 3"),1.0f);CHECK_EQ(eval("3 > 2 == 1"),1.0f);CHECK_EQ(eval("!1 + 1"),1.0f);CHECK_EQ(eval("!$a == 0"),1.0f);CHECK_EQ(eval("1 - -2"),3.0f);
 CHECK_EQ(eval("sign(-3)"),-1.0f);CHECK_EQ(eval("frac(-1.25)"),0.75f);CHECK_EQ(eval("saturate(2)"),1.0f);CHECK_EQ(eval("round(2.5)"),3.0f);
 CHECK_EQ(eval("trunc(-1.5)"),-1.0f);CHECK_EQ(eval("abs(-(2+3))"),5.0f);CHECK_EQ(eval("countbits(7)"),3.0f);CHECK_EQ(eval("sqrt(16) + rsqrt(4)"),4.5f);
 CHECK_EQ(eval("0x1f"),31.0f);CHECK_EQ(eval("0b11"),3.0f);CHECK_EQ(eval(".5"),0.5f);CHECK_EQ(eval("1e3"),1000.0f);CHECK_EQ(eval("inf > 1e30"),1.0f);
 CHECK_EQ(bits(eval("-0.0")),0x80000000u);
 // Both sides of && and || are evaluated: no short circuit.
 Plain p;eval("0 && random(1)",&p);CHECK_EQ(p.randoms,1);
 for(const char* bad:{"1 +","(1","1)","$unknown","foo","sin 1","","()","1 2","$pool[0]","resourcefoo->size"})CHECK(syntaxError(bad));}

// IniHandler.cpp namespaces: [Constants] names in included files become $\<namespace>\name, lookups try the
// namespaced name first; "namespace =" renames a file's namespace; sections with a prefix are namespaced, run finds
// the caller's own first; [Present] collects the lines of every file, each keeping its namespace.
static void namespaces(){
 Engine e=loaded({{"","[Constants]\nglobal $x = 1\nglobal $shared = 5\n[CommandListGlobal]\n$shared = $shared * 100\n"},
  {"Mods\\A\\a.ini","[Constants]\nglobal $x = 10\n[Present]\n$x = $x + 1\n$shared = $shared + 1\n[CommandListPick]\n$\\bee\\y = 7\n[TextureOverrideA]\nhash = 11111111\nrun = CommandListPick\n"},
  {"Mods\\B\\b.ini","namespace = Bee\n[Constants]\nglobal $y = 0\n[Present]\npost $y = $y + $x\n[CommandListPick]\n$y = 100\n[TextureOverrideB]\nhash = 22222222\nrun = CommandListPick\n[TextureOverrideC]\nhash = 33333333\nrun = CommandListGlobal\n"}});
 CHECK_EQ(var(e,"$x"),1.0f);CHECK_EQ(var(e,"$\\mods\\a\\a.ini\\x"),10.0f);CHECK_EQ(var(e,"$\\bee\\y"),0.0f);
 CHECK(e.lists.count("textureoverride\\mods\\a\\a.ini\\a"));CHECK(e.lists.count("commandlist\\bee\\pick"));
 e.frame(1.0/60);
 CHECK_EQ(var(e,"$\\mods\\a\\a.ini\\x"),11.0f);CHECK_EQ(var(e,"$x"),1.0f);CHECK_EQ(var(e,"$shared"),6.0f);
 CHECK_EQ(var(e,"$\\bee\\y"),1.0f);   // B has no $x: the global one
 e.draw(drawOf({{"ib",Ref::game(0x11111111)}},{"ib"}));CHECK_EQ(var(e,"$\\bee\\y"),7.0f);     // explicit namespace
 e.draw(drawOf({{"ib",Ref::game(0x22222222)}},{"ib"}));CHECK_EQ(var(e,"$\\bee\\y"),100.0f);   // B's own CommandListPick
 e.draw(drawOf({{"ib",Ref::game(0x33333333)}},{"ib"}));CHECK_EQ(var(e,"$shared"),600.0f);   // not in B: d3dx.ini's
 CHECK_EQ(e.diag.count(Diagnostic::Warning),size_t(0));}

// CommandList.cpp ParseIfCommand / ParseElseIfCommand / _ParseEndIfCommand, declare_local_variable.
static void conditions(){
 const char* nest="[Constants]\nglobal $a = 2\nglobal $b = 0\nglobal $out = 0\nglobal $count = 0\n[Present]\nrun = CommandListNest\nrun = CommandListCounter\n"
  "[CommandListNest]\nif $a == 1\n $out = 1\nelse if $a == 2\n if $b\n  $out = 21\n else\n  $out = 20\n endif\nelif $a == 3\n $out = 3\nelse\n $out = 9\nendif\n"
  "[CommandListCounter]\nlocal $n\n$n = $n + 1\n$count = $n\nif $a\n local $t = 1\nelse\n local $t = 2\nendif\n";
 Engine e=loaded({{"",nest}});
 auto at=[&](float a,float b){e.globals["$a"].value=a;e.globals["$b"].value=b;e.frame(0);return var(e,"$out");};
 CHECK_EQ(at(2,0),20.0f);CHECK_EQ(at(2,1),21.0f);CHECK_EQ(at(3,1),3.0f);CHECK_EQ(at(7,0),9.0f);CHECK_EQ(at(1,0),1.0f);
 CHECK_EQ(var(e,"$count"),5.0f);   // a local keeps its value between runs (upstream static_vars)
 CHECK_EQ(e.diag.count(Diagnostic::Warning),size_t(0));   // the same local name in sibling branches is allowed
 Engine bad=loaded({{"","[Constants]\nglobal $g = 0\n[CommandListX]\nlocal $p\nif 1\n local $p\nendif\nif 1\n local $q = 1\nendif\n$g = $q\nlocal $g\n"}});
 CHECK_EQ(count(bad,Diagnostic::Warning,"局部变量重复声明：\"$p\""),size_t(1));
 CHECK_EQ(count(bad,Diagnostic::Warning,"无法识别的变量：$q"),size_t(1));   // out of its scope after endif
 CHECK_EQ(count(bad,Diagnostic::Notice,"遮蔽了同名全局变量"),size_t(1));
 Engine unbalanced=loaded({{"","[CommandListX]\nif 1\nendif\nendif\nif 1\n"}});
 CHECK_EQ(count(unbalanced,Diagnostic::Warning,"缺少对应的 \"if\""),size_t(1));CHECK_EQ(count(unbalanced,Diagnostic::Warning,"作用域不平衡"),size_t(1));}

// AddCommandToList: an assignment goes to pre unless it says post; run and if go to both lists; "post run" runs the
// target's pre and post together; handling = skip is pre only; abort stops the rest of the run.
static void prePost(){
 Engine e=loaded({{"","[Constants]\nglobal $trace = 0\nglobal $z = 0\n"
  "[CommandListInner]\n$trace = $trace * 10 + 1\npost $trace = $trace * 10 + 2\n[CommandListInner2]\n$trace = $trace * 10 + 5\npost $trace = $trace * 10 + 6\n"
  "[TextureOverrideT]\nhash = 33333333\nrun = CommandListInner\npost run = CommandListInner2\n$trace = $trace * 10 + 3\npost $trace = $trace * 10 + 4\nhandling = skip\n"
  "[CommandListAbort]\npre handling = abort\n[TextureOverrideAbort]\nhash = 44444444\nrun = CommandListAbort\n$z = 1\npost $z = $z + 10\n"
  "[CommandListLoop]\nrun = CommandListLoop\n[TextureOverrideLoop]\nhash = 55555555\nrun = CommandListLoop\n"}});
 std::vector<Event> trace;e.trace=&trace;
 auto r=e.draw(drawOf({{"ib",Ref::game(0x33333333)}},{"ib"}));
 CHECK_EQ(var(e,"$trace"),132564.0f);CHECK(r.skip);
 size_t skips=0;for(auto& ev:trace)skips+=ev.kind==Event::Skip;CHECK_EQ(skips,size_t(1));   // pre only
 std::vector<std::string> runs;for(auto& ev:trace)if(ev.kind==Event::Run)runs.push_back(ev.detail);
 CHECK(runs==std::vector<std::string>({"CommandListInner (pre)","CommandListInner (post)","CommandListInner2 (pre+post)"}));
 e.draw(drawOf({{"ib",Ref::game(0x44444444)}},{"ib"}));CHECK_EQ(var(e,"$z"),10.0f);   // pre aborted before $z = 1; the post run is a new state
 e.draw(drawOf({{"ib",Ref::game(0x55555555)}},{"ib"}));CHECK(count(e,Diagnostic::Warning,"递归超过上限")>=1);}

// HackerDXGI.cpp RunFrameActions: [Present] pre, input, transitions, frame_number++, the game's Present, [Present]
// post. A key's run list runs when the key is dispatched, before its values are applied; its post part after them.
static void timing(){
 Engine e=loaded({{"","[Constants]\nglobal $f = -1\nglobal $g = -1\nglobal $k = 0\nglobal $seen = -1\nglobal $seenpost = -1\nglobal $atdraw = -1\n"
  "[Present]\n$f = frame_number\npost $g = frame_number\n[KeyK]\nkey = VK_A\ntype = activate\n$k = 1\nrun = CommandListKeyRun\n"
  "[CommandListKeyRun]\n$seen = $k\npost $seenpost = $k\n[TextureOverrideD]\nhash = 66666666\n$atdraw = $k\n"}});
 auto drawK=[&]{e.draw(drawOf({{"ib",Ref::game(0x66666666)}},{"ib"}));return var(e,"$atdraw");};
 e.frame(0);CHECK_EQ(var(e,"$f"),0.0f);CHECK_EQ(var(e,"$g"),1.0f);
 e.keyDown("KeyK");CHECK_EQ(drawK(),0.0f);   // queued input waits for the next Present
 e.frame(1.0/60);CHECK_EQ(var(e,"$f"),1.0f);CHECK_EQ(var(e,"$seen"),0.0f);CHECK_EQ(var(e,"$seenpost"),1.0f);CHECK_EQ(var(e,"$k"),1.0f);CHECK_EQ(var(e,"$g"),2.0f);
 CHECK_EQ(drawK(),1.0f);}

// Override.cpp: cycle (wrap, smart resync, back), toggle, hold with the shared save, condition, transitions.
static void keys(){
 Engine e=loaded({{"","[Constants]\nglobal $c = 0\nglobal $t = 0\nglobal $h = 3\nglobal $enabled = 0\nglobal $cond = 0\nglobal $tr = 0\nglobal $nw = 0\n"
  "[KeyCycle]\nkey = 1\nback = 2\ntype = cycle\n$c = 0,1,2\n[KeyNoWrap]\nkey = 3\ntype = cycle\nwrap = false\n$nw = 0,1\n[KeyToggle]\nkey = 4\ntype = toggle\n$t = 1\n"
  "[KeyHold1]\nkey = 5\ntype = hold\n$h = 5\n[KeyHold2]\nkey = 6\ntype = hold\n$h = 7\n[KeyCond]\nkey = 7\ncondition = $enabled == 1\n$cond = 1\n"
  "[KeyTrans]\nkey = 8\n$tr = 10\ntransition = 1000\n"}});
 double t=0;auto press=[&](const char* k){e.keyDown(k);e.frame(t+=0.01);};auto release=[&](const char* k){e.keyUp(k);e.frame(t+=0.01);};
 press("KeyCycle");CHECK_EQ(var(e,"$c"),1.0f);press("KeyCycle");CHECK_EQ(var(e,"$c"),2.0f);press("KeyCycle");CHECK_EQ(var(e,"$c"),0.0f);
 e.keyBack("KeyCycle");e.frame(t+=0.01);CHECK_EQ(var(e,"$c"),2.0f);
 e.globals["$c"].value=0;press("KeyCycle");CHECK_EQ(var(e,"$c"),1.0f);   // smart: resynced to the preset matching $c = 0 first
 // The App's direct choice (no upstream key does this): that preset activated as stepping to it would, then steps go on from it.
 auto state=[&](const char* name){for(auto& s:e.keyStates())if(lower(s.name)==lower(name))return s;return Engine::KeyState{};};
 CHECK_EQ(state("KeyCycle").current,1);
 e.keySelect("KeyCycle",2);CHECK_EQ(var(e,"$c"),1.0f);e.frame(t+=0.01);CHECK_EQ(var(e,"$c"),2.0f);CHECK_EQ(state("KeyCycle").current,2);
 press("KeyCycle");CHECK_EQ(var(e,"$c"),0.0f);
 e.keySelect("KeyCycle",3);e.keySelect("KeyCycle",-1);e.frame(t+=0.01);CHECK_EQ(var(e,"$c"),0.0f);   // out of range: nothing
 e.globals["$c"].value=2;CHECK_EQ(state("KeyCycle").current,2);e.globals["$c"].value=7;CHECK_EQ(state("KeyCycle").current,-1);   // read back as the smart resync would
 e.globals["$c"].value=0;
 press("KeyNoWrap");press("KeyNoWrap");CHECK_EQ(var(e,"$nw"),1.0f);press("KeyNoWrap");CHECK_EQ(var(e,"$nw"),1.0f);
 press("KeyToggle");CHECK_EQ(var(e,"$t"),1.0f);CHECK(state("KeyToggle").active&&state("KeyToggle").matches);press("KeyToggle");CHECK_EQ(var(e,"$t"),0.0f);CHECK(!state("KeyToggle").active);
 e.keySelect("KeyToggle",0);e.frame(t+=0.01);CHECK_EQ(var(e,"$t"),1.0f);press("KeyToggle");CHECK_EQ(var(e,"$t"),0.0f);   // select on another type: a press
 press("KeyHold1");CHECK_EQ(var(e,"$h"),5.0f);press("KeyHold2");CHECK_EQ(var(e,"$h"),7.0f);
 release("KeyHold1");CHECK_EQ(var(e,"$h"),3.0f);   // Hold1's own save is 3; the shared save still has a holder
 release("KeyHold2");CHECK_EQ(var(e,"$h"),3.0f);   // Hold2 saved 5, but the last release takes the shared save   // the last release restores the shared save
 press("KeyCond");CHECK_EQ(var(e,"$cond"),0.0f);CHECK(!state("KeyCond").matches);e.globals["$enabled"].value=1;press("KeyCond");CHECK_EQ(var(e,"$cond"),1.0f);CHECK(state("KeyCond").matches);
 e.keyDown("KeyTrans");e.frame(t=10.0);CHECK_EQ(var(e,"$tr"),0.0f);e.frame(10.25);CHECK_EQ(var(e,"$tr"),2.5f);e.frame(10.5);CHECK_EQ(var(e,"$tr"),5.0f);e.frame(11.0);CHECK_EQ(var(e,"$tr"),10.0f);}

// TextureOverride: hash plus draw context (match_first_index), sorted by match_priority then section name;
// filter_index and the texture filter values (CommandList.cpp process_texture_filter); checktextureoverride inside
// a mod's own list.
static void matching(){
 Engine e=loaded({{"","[Constants]\nglobal $order = 0\nglobal $inner = 0\nglobal $f0 = 0\nglobal $f1 = 0\nglobal $f2 = 0\nglobal $f3 = 0\n"
  "[TextureOverrideB_Body]\nhash = aaaa0001\nmatch_first_index = 100\n$order = $order * 10 + 2\n[TextureOverrideA_All]\nhash = aaaa0001\n$order = $order * 10 + 1\n"
  "[TextureOverrideC_Head]\nhash = aaaa0001\nmatch_first_index = 0\n$order = $order * 10 + 3\n[TextureOverrideZ]\nhash = aaaa0001\nmatch_priority = -1\n$order = $order * 10 + 9\n"
  "[TextureOverrideTex]\nhash = bbbb0001\nfilter_index = 7\n[TextureOverrideTex2]\nhash = bbbb0002\n"
  "[TextureOverrideFilter]\nhash = cccc0001\n$f0 = ps-t0\n$f1 = ps-t1 === -0.0\n$f2 = ps-t2\n$f3 = ps-t3\ncheckTextureOverride = ps-t4\n[TextureOverrideInner]\nhash = dddd0001\n$inner = 1\n"}});
 e.draw(drawOf({{"ib",Ref::game(0xaaaa0001)}},{"ib"},{{"first_index",100}}));CHECK_EQ(var(e,"$order"),912.0f);
 e.globals["$order"].value=0;e.draw(drawOf({{"ib",Ref::game(0xaaaa0001)}},{"ib"},{{"first_index",0}}));CHECK_EQ(var(e,"$order"),913.0f);
 e.globals["$order"].value=0;e.draw(drawOf({{"ib",Ref::game(0xaaaa0001)}},{"ib"}));CHECK_EQ(var(e,"$order"),91.0f);   // no first_index known: draw matches fail, reported
 CHECK(count(e,Diagnostic::Limitation,"match_first_index")>=1);
 e.draw(drawOf({{"ib",Ref::game(0xcccc0001)},{"ps-t0",Ref::game(0xbbbb0001)},{"ps-t2",Ref::game(0x12345678)},{"ps-t3",Ref::game(0xbbbb0002)},{"ps-t4",Ref::game(0xdddd0001)}},{"ib"}));
 CHECK_EQ(var(e,"$f0"),7.0f);CHECK_EQ(var(e,"$f1"),1.0f);CHECK_EQ(var(e,"$f2"),0.0f);CHECK_EQ(var(e,"$f3"),1.0f);CHECK_EQ(var(e,"$inner"),1.0f);}

// What a draw selects, and the game-side session asking for an update only when that changes.
// AfterDraw runs the post lists of what each checked slot holds after the pre lists: a slot rebound to a mod
// resource matches nothing (the core's model of upstream, where mod resources carry no tracked hash; unverified).
static void postRematch(){
 Engine e=loaded({{"","[Constants]\nglobal $post = 0\nglobal $post2 = 0\n[ResourceA]\nfilename = a.ib\n"
  "[TextureOverrideRebind]\nhash = f0000001\nib = ResourceA\npost $post = 1\n[TextureOverrideKeep]\nhash = f0000002\npost $post2 = 1\n"}});
 e.draw(drawOf({{"ib",Ref::game(0xf0000001)}},{"ib"}));CHECK_EQ(var(e,"$post"),0.0f);CHECK(count(e,Diagnostic::Notice,"没有可追踪的 hash")>=1);
 e.draw(drawOf({{"ib",Ref::game(0xf0000002)}},{"ib"}));CHECK_EQ(var(e,"$post2"),1.0f);}

static void selection(){
 Engine e=loaded({{"Mods\\S\\s.ini","[Constants]\nglobal $sel = 0\n[ResourceA]\nfilename = a.buf\n[ResourceB]\nfilename = b.buf\n[ResourceSave]\n"
  "[TextureOverrideSel]\nhash = eeee0001\nif $sel == 0\n vb0 = ResourceA\nelse\n vb0 = ResourceB\nendif\nResourceSave = ref ps-t0\nps-t1 = ResourceSave\nhandling = skip\ndrawindexed = auto\n"}});
 DrawInfo d=drawOf({{"ib",Ref::game(0xeeee0001)},{"ps-t0",Ref::game(0x12345678)}},{"ib"});
 auto r=e.draw(d);CHECK(r.skip);CHECK(r.matched==std::vector<std::string>({"TextureOverride\\Mods\\S\\s.ini\\Sel"}));CHECK_EQ(r.draws.size(),size_t(1));CHECK(r.draws[0].automatic);
 CHECK_EQ(describe(e,r.draws[0].bindings["vb0"]),std::string("ResourceA"));CHECK_EQ(describe(e,r.bindings["ps-t1"]),std::string("game:12345678"));
 Session s(e);
 CHECK_EQ(s.tick({{"sel",d}},0.0).size(),size_t(1));CHECK_EQ(s.tick({{"sel",d}},0.1).size(),size_t(0));
 e.globals["$\\mods\\s\\s.ini\\sel"].value=1;auto changed=s.tick({{"sel",d}},0.2);CHECK_EQ(changed.size(),size_t(1));
 CHECK(s.last["sel"].bindings["vb0"]=="ResourceB");
 ImportScan scan(e);CHECK_EQ(scan.resources.size(),size_t(3));CHECK(!scan.finiteStates);   // ResourceSave = ref ps-t0: what it holds is the game's
 bool game=false;for(auto& r:scan.reasons)game|=r.find("使用了游戏资源")!=std::string::npos;CHECK(game);}

// An outfit set a key cycles: three styles with consecutive file names and a drawindexed each. It is finite (every
// write is a key value or a constant), so the importer may prepare all three; nothing about it is an animation.
static void outfits(){
 std::ostringstream o;o<<"[Constants]\nglobal persist $dress = 0\n[KeyDress]\nkey = VK_RIGHT\ntype = cycle\n$dress = 0,1,2\n[TextureOverrideBody]\nhash = 0b0d0001\nhandling = skip\n";
 for(int i=0;i<3;i++)o<<(i?"else if":"if")<<" $dress == "<<i<<"\n ib = ResourceBodyIB."<<i<<"\n drawindexed = 3000,"<<i*3000<<",0\n";
 o<<"endif\n";for(int i=0;i<3;i++)o<<"[ResourceBodyIB."<<i<<"]\nfilename = Body"<<i<<".ib\n";
 Engine e=loaded({{"Mods\\Dress\\dress.ini",o.str()}});ImportScan scan(e);
 CHECK(scan.finiteStates);CHECK_EQ(scan.states,uint64_t(3));CHECK(scan.vars["$\\mods\\dress\\dress.ini\\dress"].finite);CHECK_EQ(scan.resources.size(),size_t(3));
 Session s(e);DrawInfo d=drawOf({{"ib",Ref::game(0x0b0d0001)}},{"ib"});
 CHECK_EQ(s.tick({{"body",d}},0).size(),size_t(1));for(int i=1;i<10;i++)CHECK_EQ(s.tick({{"body",d}},i/60.0).size(),size_t(0));   // no key: no update
 e.keyDown("KeyDress");s.tick({{"body",d}},0.2);CHECK_EQ(s.tick({{"body",d}},0.3).size(),size_t(1));CHECK(s.last["body"].draws[0].find("ResourceBodyIB.1")!=std::string::npos);
 CHECK_EQ(e.persistent().at("$\\mods\\dress\\dress.ini\\dress"),1.0f);CHECK(e.persistDirty);
 Engine again;again.load({{"Mods\\Dress\\dress.ini",o.str()}});again.start(e.persistent());CHECK_EQ(var(again,"$\\mods\\dress\\dress.ini\\dress"),1.0f);}

// ImportScan: which variables what is drawn depends on, and whether their values are a fixed set. Through if
// conditions, draw arguments, assignments from other variables and the ifs around them, locals; a counter, the
// clock, random() and a texture filter are not finite.
static ImportScan scanOf(const std::string& ini){Engine e=loaded({{"Mods\\S\\s.ini",ini}});return ImportScan(e);}
static bool reason(const ImportScan& s,const std::string& text){for(auto& r:s.reasons)if(r.find(text)!=std::string::npos)return true;return false;}
static void finiteness(){
 const std::string head="[Constants]\nglobal $a = 0\nglobal $b = 0\nglobal $n = 3\nglobal $t = 0\n[ResourceA]\nfilename = a.ib\n[ResourceB]\nfilename = b.ib\n";
 std::string k="\\mods\\s\\s.ini\\";
 // A draw count read straight from a variable that a key sets: finite, its values are the key's and the initial one.
 {auto s=scanOf(head+"[KeyN]\nkey = 1\ntype = cycle\n$n = 3,6\n[TextureOverrideP]\nhash = 1\nhandling = skip\nib = ResourceA\ndrawindexed = $n,0,0\n");
  CHECK(s.finiteStates);CHECK(s.vars["$"+k+"n"].readByDraws);CHECK(s.vars["$"+k+"n"].values==std::set<float>({3,6}));CHECK_EQ(s.states,uint64_t(2));}
 // $b decides the draw; Present copies $a + 1 into $b under if $t: $a and $t become draw variables too.
 {auto s=scanOf(head+"[KeyA]\nkey = 1\ntype = cycle\n$a = 0,1,2\n[KeyT]\nkey = 2\ntype = toggle\n$t = 1\n[Present]\nif $t\n $b = $a + 1\nendif\n"
   "[TextureOverrideP]\nhash = 1\nif $b == 2\n ib = ResourceB\nendif\n");
  CHECK(s.finiteStates);CHECK(s.vars["$"+k+"a"].readByDraws);CHECK(s.vars["$"+k+"t"].readByDraws);CHECK(s.vars["$"+k+"b"].values==std::set<float>({0,1,2,3}));CHECK_EQ(s.states,uint64_t(3*2*4));}
 // A local that carries a value into the draw decision.
 {auto s=scanOf(head+"[KeyA]\nkey = 1\ntype = cycle\n$a = 0,1\n[CommandListPick]\nlocal $pick\n$pick = $a * 2\nif $pick == 2\n ib = ResourceB\nendif\n[TextureOverrideP]\nhash = 1\nrun = CommandListPick\n");
  auto& pick=s.vars["CommandList\\Mods\\S\\s.ini\\Pick local $pick"];CHECK(s.finiteStates);CHECK(pick.readByDraws);CHECK(pick.values==std::set<float>({0,2}));}
 // A counter, even one that wraps: not provable here, so not finite.
 {auto s=scanOf(head+"[Present]\nif $a < 20\n $a = $a + 1\nelse\n $a = 0\nendif\n[TextureOverrideP]\nhash = 1\nif $a == 3\n ib = ResourceB\nendif\n");
  CHECK(!s.finiteStates);CHECK(!s.vars["$"+k+"a"].finite);CHECK(reason(s,"可能是计数器"));}
 // The clock, random() and a texture filter make the decision depend on what is outside the mod.
 {auto s=scanOf(head+"[Present]\n$b = time\n[TextureOverrideP]\nhash = 1\nif $b > 5\n ib = ResourceB\nendif\n");CHECK(!s.finiteStates);CHECK(reason(s,"读取了 time"));}
 {auto s=scanOf(head+"[Present]\n$b = random(3)\n[TextureOverrideP]\nhash = 1\nif $b > 1\n ib = ResourceB\nendif\n");CHECK(!s.finiteStates);CHECK(reason(s,"random()"));}
 {auto s=scanOf(head+"[TextureOverrideP]\nhash = 1\nif ps-t0 == 1\n ib = ResourceB\nendif\n");CHECK(!s.finiteStates);CHECK(reason(s,"贴图过滤 ps-t0"));}
 // A transition passes through values in between.
 {auto s=scanOf(head+"[KeyA]\nkey = 1\n$a = 1\ntransition = 100\n[TextureOverrideP]\nhash = 1\nif $a == 1\n ib = ResourceB\nendif\n");CHECK(!s.finiteStates);CHECK(reason(s,"transition"));}
 // A variable only Present uses (not what is drawn) does not count.
 {auto s=scanOf(head+"[Present]\n$t = $t + 1\n[KeyA]\nkey = 1\ntype = cycle\n$a = 0,1\n[TextureOverrideP]\nhash = 1\nif $a == 1\n ib = ResourceB\nendif\n");CHECK(s.finiteStates);CHECK(!s.vars["$"+k+"t"].readByDraws);CHECK_EQ(s.states,uint64_t(2));}
 // A long else-if chain (a merged animation's frame lists): every branch is reached, once; walking every path is
 // exponential in the chain's length and never finishes.
 {std::ostringstream o;o<<head<<"[KeyA]\nkey = 1\ntype = cycle\n$a = ";for(int i=0;i<64;i++)o<<(i?",":"")<<i;o<<"\n[CommandListPick]\n";
  for(int i=0;i<64;i++)o<<(i?"else if":"if")<<" $a == "<<i<<"\n ib = ResourceF"<<i<<"\n";
  o<<"endif\n[TextureOverrideP]\nhash = 1\nrun = CommandListPick\n[TextureOverrideQ]\nhash = 2\nrun = CommandListPick\n";for(int i=0;i<64;i++)o<<"[ResourceF"<<i<<"]\nfilename = f"<<i<<".ib\n";
  auto s=scanOf(o.str());CHECK(s.finiteStates);CHECK_EQ(s.states,uint64_t(64));CHECK_EQ(s.resources.size(),size_t(64));CHECK_EQ(s.bindings.size(),size_t(64));}
 // Deeper than the scan follows: refused with a reason, never cut short as if the rest did not exist.
 {std::ostringstream o;o<<head<<"[TextureOverrideP]\nhash = 1\n";for(int i=0;i<1100;i++)o<<(i?"else if":"if")<<" $a == "<<i<<"\n ib = ResourceA\n";o<<"endif\n";
  auto s=scanOf(o.str());CHECK(!s.finiteStates);CHECK(reason(s,"嵌套过深"));}
 // A list reached under different conditions keeps both: the write in CommandListSet depends on $c and on $d.
 {std::string h=head;h.replace(h.find("[ResourceA]"),0,"global $c = 0\nglobal $d = 0\n");auto s=scanOf(h+"[KeyC]\nkey = 1\ntype = toggle\n$c = 1\n[KeyD]\nkey = 2\ntype = toggle\n$d = 1\n"
   "[Present]\nif $d\n run = CommandListSet\nendif\nif $c\n run = CommandListSet\nendif\n[CommandListSet]\n$b = 1\n[TextureOverrideP]\nhash = 1\nif $b == 1\n ib = ResourceB\nendif\n");
  CHECK(s.finiteStates);CHECK(s.vars["$"+k+"c"].readByDraws);CHECK(s.vars["$"+k+"d"].readByDraws);CHECK(s.vars["$"+k+"b"].values==std::set<float>({0,1}));}
 // What is drawn repeats for 100 frames, then a counter opens a new branch: the repetition is no proof of a cycle,
 // and the scan does not take the counter for finite.
 {std::string ini=head+"[Present]\n$t = $t + 1\n[TextureOverrideP]\nhash = 1\nif $t > 100\n ib = ResourceB\nelse\n ib = ResourceA\nendif\n";
  Engine e=loaded({{"Mods\\S\\s.ini",ini}});Session run(e);std::vector<Session::Pass> passes{{"p",drawOf({{"ib",Ref::game(1)}},{"ib"})}};
  std::map<std::string,Session::Selection> sel;std::set<std::string> early;
  for(int f=0;f<100;f++){run.tick(passes,f/60.0,&sel);early.insert(sel["p"].bindings["ib"]);}
  CHECK_EQ(early.size(),size_t(1));
  for(int f=100;f<103;f++)run.tick(passes,f/60.0,&sel);
  CHECK(sel["p"].bindings["ib"]!=*early.begin());
  ImportScan scan(e);CHECK(!scan.finiteStates);CHECK(!scan.vars["$"+k+"t"].finite);CHECK(reason(scan,"可能是计数器"));}}

// The Present frame loop of a merged GIMI animation (as in the uploaded Traveler MOD, three frames here), run by
// the core and by the old path (model.hpp FrameLoop with the values the old converter extracts), side by side.
static std::string animationIni(const char* speed,int frames){std::ostringstream o;
 o<<"[Constants]\nglobal $speed = "<<speed<<"\nglobal $frameStart = 0\nglobal $frameEnd = "<<frames-1<<"\nglobal $swapvar = 0\nglobal $auxTime = 0\n"
  "[Present]\nif $auxTime % $speed == 0\n    if $swapvar < $frameEnd\n        $swapvar = $swapvar + 1\n    else\n        $swapvar = $frameStart\n    endif\nendif\npost $auxTime=$auxTime+1\n"
  "[TextureOverridePosition]\nhash = 8239be13\nrun = CommandListPosition\n[TextureOverrideIB]\nhash = e7612ed8\nhandling = skip\ndrawindexed = auto\n"
  "[TextureOverrideHead]\nhash = e7612ed8\nmatch_first_index = 0\nrun = CommandListHead\n[CommandListPosition]\n";
 for(int i=0;i<frames;i++)o<<(i?"else if":"if")<<" $swapvar == "<<i<<"\n\tvb0 = ResourcePosition."<<i<<"\n";
 o<<"endif\n[CommandListHead]\n";
 for(int i=0;i<frames;i++)o<<(i?"else if":"if")<<" $swapvar == "<<i<<"\n\tib = ResourceHeadIB."<<i<<"\n\tps-t0 = ResourceHeadDiffuse."<<i<<"\n";
 o<<"endif\n";
 for(int i=0;i<frames;i++)o<<"[ResourcePosition."<<i<<"]\ntype = Buffer\nstride = 40\nfilename = .\\f"<<i<<"\\Position.buf\n[ResourceHeadIB."<<i<<"]\ntype = Buffer\nformat = DXGI_FORMAT_R32_UINT\nfilename = .\\f"<<i<<"\\Head.ib\n[ResourceHeadDiffuse."<<i<<"]\nfilename = .\\f"<<i<<"\\HeadDiffuse.dds\n";
 return o.str();}
static void animation(){
 for(const char* speed:{"0.5","1","2","3","0.3"}){const int frames=3;Engine e=loaded({{"Mods\\Anim\\Anim.ini",animationIni(speed,frames)}});Session s(e);
  ms::AnimationSpec spec;spec.variable="$swapvar";spec.first=0;spec.last=frames-1;spec.step=1;spec.initial=0;spec.counter=0;spec.period=std::strtof(speed,nullptr);spec.counterStep=1;
  for(int i=0;i<frames;i++){ms::AnimationFrame f;f.value=i;spec.frames.push_back(f);}
  ms::FrameLoop old(spec);old.tick(0);
  DrawInfo skin=drawOf({{"vb0",Ref::game(0x8239be13)}},{"vb0"}),head=drawOf({{"ib",Ref::game(0xe7612ed8)}},{"ib"},{{"first_index",0}});
  int mismatches=0;
  for(uint32_t tick=1;tick<=200;tick++){std::map<std::string,Session::Selection> sel;s.tick({{"skin",skin},{"head",head}},tick/60.0,&sel);
   int frame=old.spec.frames[old.current].value;std::string n=std::to_string(frame);
   bool same=sel["skin"].bindings["vb0"]=="ResourcePosition."+n&&sel["head"].draws.size()==1&&sel["head"].draws[0].find("ib=ResourceHeadIB."+n)!=std::string::npos
    &&sel["head"].draws[0].find("ps-t0=ResourceHeadDiffuse."+n)!=std::string::npos;
   if(!same&&mismatches++<3)std::cerr<<"speed "<<speed<<" tick "<<tick<<": old frame "<<frame<<", core "<<sel["skin"].bindings["vb0"]<<"\n";
   old.tick(tick);}
  CHECK_EQ(mismatches,0);
  ImportScan scan(e);CHECK(!scan.finiteStates);CHECK(!scan.vars["$\\mods\\anim\\anim.ini\\swapvar"].finite);CHECK_EQ(scan.resources.size(),size_t(3*frames));}}

// The rehearsal of a Present program (converter side): frames, the order without input, keys, and what it refuses.
static Rehearsal rehearse(const std::string& ini,Rehearsal::Limits lim={}){Engine e=loaded({{"Mods\\R\\r.ini",ini}});return Rehearsal(e,"Mods\\R\\r.ini",lim);}
static bool says(const Rehearsal& r,const std::string& part){if(r.reason.find(part)!=std::string::npos)return true;std::cerr<<"rehearsal: expected \""<<part<<"\", got \""<<r.reason<<"\"\n";return false;}
static void rehearsal(){
 const std::string draws="[TextureOverrideP]\nhash = 1\nif $f == 0\n ib = ResourceA\nelse if $f == 1\n ib = ResourceB\nelse\n ib = ResourceC\nendif\n[ResourceA]\nfilename = a.ib\n[ResourceB]\nfilename = b.ib\n[ResourceC]\nfilename = c.ib\n";
 // The merged GIMI loop, as the old FrameLoop plays it, Present by Present; the clock counter is kept as its phase.
 for(const char* speed:{"0.5","1","2","3"}){std::string ini=animationIni(speed,3);auto r=rehearse(ini);CHECK(r.ok);if(!r.ok){std::cerr<<r.reason<<"\n";continue;}
  CHECK_EQ(r.frames.size(),size_t(3));CHECK(r.phases.size()==1&&r.phases[0].var=="$\\mods\\r\\r.ini\\auxtime");
  ms::AnimationSpec spec;spec.variable="$swapvar";spec.first=0;spec.last=2;spec.step=1;spec.initial=0;spec.counter=0;spec.period=std::strtof(speed,nullptr);spec.counterStep=1;
  for(int i=0;i<3;i++){ms::AnimationFrame f;f.value=i;spec.frames.push_back(f);}ms::FrameLoop old(spec);old.tick(0);
  for(uint32_t t=0;t<60;t++){size_t at=t<r.sequence.size()?t:r.loop+(t-r.loop)%(r.sequence.size()-r.loop);CHECK_EQ(r.frames[r.sequence[at]].at("$swapvar"),old.spec.frames[old.current].value);old.tick(t+1);}}
 // A period that is not a 1/1024 step: the counter is not a phase here, and its full value never repeats.
 {auto r=rehearse(animationIni("0.3",3),{4096,4096,64,3000,64ull<<20});CHECK(!r.ok&&says(r,"在 4096 次 Present 内没有重复")&&says(r,"auxtime"));}
 // Back and forth (not the pattern the converter matches): 0 1 2 1, then the start state again.
 {auto r=rehearse("[Constants]\nglobal $f = 0\nglobal $dir = 1\n[Present]\n$f = $f + $dir\nif $f >= 2\n $dir = -1\nendif\nif $f <= 0\n $dir = 1\nendif\n"+draws);CHECK(r.ok);
  std::vector<int> shown;for(auto i:r.sequence)shown.push_back(r.frames[i].at("$f"));CHECK(shown==std::vector<int>({0,1,2,1}));CHECK_EQ(r.loop,size_t(0));CHECK(r.keys.empty()&&r.keyFrames==0);}
 // What is drawn repeats for 100 Presents, then a counter opens another branch: refused, not cut short.
 {auto r=rehearse("[Constants]\nglobal $t = 0\nglobal $f = 0\n[Present]\n$t = $t + 1\nif $t > 100\n $f = 1\nendif\n"+draws,{1000,4096,64,3000,64ull<<20});
  CHECK(!r.ok&&says(r,"在 1000 次 Present 内没有重复")&&says(r,"$\\mods\\r\\r.ini\\t"));}
 // A key the Present program does not touch: its frames come from key presses only.
 {auto r=rehearse("[Constants]\nglobal $f = 0\nglobal $blink = 0\n[KeyF]\nkey = 1\ntype = cycle\n$f = 0,1,2\n[Present]\nif $blink == 0\n $blink = 1\nelse\n $blink = 0\nendif\n"
   "[TextureOverrideQ]\nhash = 2\nif $blink == 1\n ps-t0 = ResourceA\nendif\n"+draws);CHECK(r.ok);
  CHECK_EQ(r.frames.size(),size_t(6));CHECK_EQ(r.sequence.size(),size_t(2));CHECK_EQ(r.loop,size_t(0));CHECK_EQ(r.keyFrames,size_t(4));CHECK(r.keys==std::vector<std::string>({"Key\\Mods\\R\\r.ini\\F"}));CHECK(!r.notes.empty());}
 // Refused: the clock, a condition on the clock, draws that change a draw variable (a global, a local), too many frames.
 {auto r=rehearse("[Constants]\nglobal $f = 0\n[Present]\n$f = time % 3\n"+draws);CHECK(!r.ok&&says(r,"依赖 MOD 自身变量以外的数据")&&says(r,"time"));}
 {auto r=rehearse("[Constants]\nglobal $f = 0\n[Present]\nif time > 5\n $f = 1\nendif\n"+draws);CHECK(!r.ok&&says(r,"所在条件依赖 MOD 自身变量以外的数据"));}
 {auto r=rehearse("[Constants]\nglobal $f = 0\n[Present]\n$f = 1\n[TextureOverrideZ]\nhash = 3\n$f = 2\n"+draws);CHECK(!r.ok&&says(r,"绘制过程中修改了 $\\mods\\r\\r.ini\\f"));}
 {auto r=rehearse("[Constants]\nglobal $g = 0\n[Present]\n$g = 1 - $g\n[CommandListPick]\nlocal $f\n$f = $g\nif $f == 1\n ib = ResourceA\nendif\n[TextureOverrideP]\nhash = 1\nrun = CommandListPick\n[ResourceA]\nfilename = a.ib\n");
  CHECK(!r.ok&&says(r,"绘制过程中修改了 CommandList\\Mods\\R\\r.ini\\Pick local $f"));}   // a local of a list the draws run
 {auto r=rehearse("[Constants]\nglobal $f = 0\n[Present]\nif $f < 99\n $f = $f + 1\nelse\n $f = 0\nendif\n"+draws);CHECK(!r.ok&&says(r,"不同的帧超过 64 个"));}}

int main(){
 expressions();namespaces();conditions();prePost();timing();keys();matching();postRematch();selection();outfits();finiteness();animation();rehearsal();
 if(failures){std::cerr<<failures<<" failure(s)\n";return 1;}
 std::cout<<"3DMigoto logic core: PASS (float expressions and precedence, namespaces, nested if / else if / locals, run and pre/post placement, abort, "
  "Present / input / transition order, keys: cycle, toggle, hold, condition, transition; TextureOverride draw matching and order, texture filters, "
  "resource selection and change detection, finite outfit cycle and persist, finite-state analysis (draw arguments, assignment and control dependencies, locals, counters, clock, random, texture filters, transitions), Present frame loop vs the old FrameLoop, Present rehearsal (phases, back and forth, key frames, counters and outside input refused); simulated, not a game run)\n";}
