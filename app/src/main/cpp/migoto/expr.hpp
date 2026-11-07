// SPDX-License-Identifier: GPL-3.0-only
// GIBox 3DMigoto logic core: expressions.
// Ported from 3DMigoto (XXMI fork) DirectX11/CommandList.cpp at a7847eae: tokenise(), group_parenthesis(), the
// transform_operators_* passes and their order in CommandListExpression::parse(), the DEFINE_OPERATOR table,
// CommandListSyntaxTree::finalise(), ParseFloatToken(), valid_variable_name(), ParamOverrideTypeNames,
// operand_allowed_in_context(); util.cpp random(). See third_party/3dmigoto/README.md.
// Every value is a 32-bit float, both sides of every operator are evaluated (no short circuit), and the
// precedence is upstream's: unary/functions > ** (right) > * / // % > + - > << >> > < <= > >= > == != === !== >
// & > ^ > | > && > ||.
#pragma once
#include "ini.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace migoto {

struct SyntaxError:std::runtime_error {size_t pos;SyntaxError(const std::string& m,size_t p):std::runtime_error(m),pos(p){}};

enum VarFlags:unsigned {VarGlobal=1,VarPersist=2,VarLocked=4};
struct Var {std::string name;float value=0;unsigned flags=0;};

// Operands that read the PC host rather than a variable. Draw: the draw call (call_info), 0 when there is none, as
// upstream. Frame: the engine clock the adapter drives. Host: the window, cursor, render target and so on; an adapter
// that cannot provide one has it reported as a limitation.
enum class Scope {Draw,Frame,Host};
struct Keyword {const char* name;Scope scope;bool outsideLists;};
inline const Keyword* keyword(const std::string& n){
 static const Keyword table[]={{"rt_width",Scope::Host,false},{"rt_height",Scope::Host,false},{"res_width",Scope::Host,true},{"res_height",Scope::Host,true},
  {"window_width",Scope::Host,false},{"window_height",Scope::Host,false},{"vertex_count",Scope::Draw,false},{"index_count",Scope::Draw,false},
  {"instance_count",Scope::Draw,false},{"first_vertex",Scope::Draw,false},{"first_index",Scope::Draw,false},{"first_instance",Scope::Draw,false},
  {"thread_group_count_x",Scope::Draw,false},{"thread_group_count_y",Scope::Draw,false},{"thread_group_count_z",Scope::Draw,false},
  {"indirect_offset",Scope::Draw,false},{"draw_type",Scope::Draw,false},{"cursor_showing",Scope::Host,false},{"cursor_screen_x",Scope::Host,false},
  {"cursor_screen_y",Scope::Host,false},{"cursor_window_x",Scope::Host,false},{"cursor_window_y",Scope::Host,false},{"cursor_x",Scope::Host,false},
  {"cursor_y",Scope::Host,false},{"cursor_hotspot_x",Scope::Host,false},{"cursor_hotspot_y",Scope::Host,false},{"time",Scope::Frame,true},
  {"scissor_left",Scope::Host,false},{"scissor_top",Scope::Host,false},{"scissor_right",Scope::Host,false},{"scissor_bottom",Scope::Host,false},
  {"hunting",Scope::Host,true},{"frame_analysis",Scope::Host,false},{"effective_dpi",Scope::Host,true},{"sli",Scope::Host,true},
  {"stereo_active",Scope::Host,true},{"stereo_available",Scope::Host,true},{"frame_number",Scope::Frame,true},{"draw_number",Scope::Host,false},
  {"dispatch_number",Scope::Host,false},{"frame_time",Scope::Frame,false},{"fps",Scope::Frame,false}};
 for(auto& k:table)if(n==k.name)return &k;
 return nullptr;}

enum class Op {Not,BitNot,Plus,Neg,CountBits,Sin,Cos,Tan,Asin,Acos,Atan,Abs,Sign,Ceil,Floor,Trunc,Round,Frac,Sqrt,Rsqrt,Exp,Exp2,Log,Log2,Saturate,Random,
 Pow,Mul,Div,FloorDiv,Mod,Add,Sub,Shl,Shr,Lt,Le,Gt,Ge,Eq,Ne,Same,NotSame,BitAnd,BitXor,BitOr,And,Or};
struct OpName {const char* text;Op op;};
inline const std::vector<OpName>& unaryOps(){static const std::vector<OpName> v={{"!",Op::Not},{"~",Op::BitNot},{"-",Op::Neg},{"+",Op::Plus},{"countbits",Op::CountBits},
 {"sin",Op::Sin},{"cos",Op::Cos},{"tan",Op::Tan},{"asin",Op::Asin},{"acos",Op::Acos},{"atan",Op::Atan},{"abs",Op::Abs},{"sign",Op::Sign},{"ceil",Op::Ceil},
 {"floor",Op::Floor},{"trunc",Op::Trunc},{"round",Op::Round},{"frac",Op::Frac},{"sqrt",Op::Sqrt},{"rsqrt",Op::Rsqrt},{"exp",Op::Exp},{"exp2",Op::Exp2},
 {"log",Op::Log},{"log2",Op::Log2},{"saturate",Op::Saturate},{"random",Op::Random}};return v;}
// The binary passes, highest precedence first; the bool is right-associativity.
inline const std::vector<std::pair<std::vector<OpName>,bool>>& binaryPasses(){static const std::vector<std::pair<std::vector<OpName>,bool>> v={
 {{{"**",Op::Pow}},true},{{{"*",Op::Mul},{"/",Op::Div},{"//",Op::FloorDiv},{"%",Op::Mod}},false},{{{"+",Op::Add},{"-",Op::Sub}},false},
 {{{"<<",Op::Shl},{">>",Op::Shr}},false},{{{"<",Op::Lt},{"<=",Op::Le},{">",Op::Gt},{">=",Op::Ge}},false},
 {{{"==",Op::Eq},{"!=",Op::Ne},{"===",Op::Same},{"!==",Op::NotSame}},false},{{{"&",Op::BitAnd}},false},{{{"^",Op::BitXor}},false},{{{"|",Op::BitOr}},false},
 {{{"&&",Op::And}},false},{{{"||",Op::Or}},false}};return v;}
// Matched in this order: three characters, then two, then one (upstream operator_tokens).
inline const std::vector<const char*>& operatorTokens(){static const std::vector<const char*> v={"===","!==","<<",">>","==","!=","//","<=",">=","&&","||","**",
 "(",")","!","~","&","|","^","*","/","%","+","-","<",">"};return v;}

struct Node {
 enum Kind {Value,Variable,Param,Key,Slot,Unary,Binary} kind=Value;
 float value=0;Var* var=nullptr;int index=0;char comp='x';const Keyword* key=nullptr;std::string slot;Op op=Op::Add;std::unique_ptr<Node> lhs,rhs;size_t pos=0;
};

// What an expression reads outside its variables. The engine implements it per run (Present, a draw, a key).
struct Context {
 virtual ~Context()=default;
 virtual float keywordValue(const Keyword& k)=0;
 virtual float slotFilter(const std::string& slot)=0;   // a texture filter operand such as ps-t0 (upstream process_texture_filter)
 virtual float param(int index,char comp)=0;
 virtual float randomValue(float max)=0;
};

// MSVC x64 converts with cvttss2si: NaN and out-of-range values become INT_MIN. Shift counts use the low five bits,
// as the x86 shift instructions do.
inline int32_t toInt32(float f){if(std::isnan(f)||f>=2147483648.0f||f<-2147483648.0f)return INT32_MIN;return int32_t(f);}
inline uint32_t toUint32(float f){if(std::isnan(f)||f>=9.2233720368547758e18f||f<-9.2233720368547758e18f)return 0;return uint32_t(int64_t(f));}
inline uint32_t bitsOf(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}

inline float evaluate(const Node& n,Context& c){
 switch(n.kind){
  case Node::Value:return n.value;
  case Node::Variable:return n.var->value;
  case Node::Param:return c.param(n.index,n.comp);
  case Node::Key:return c.keywordValue(*n.key);
  case Node::Slot:return c.slotFilter(n.slot);
  case Node::Unary:{float r=evaluate(*n.rhs,c);switch(n.op){
   case Op::Not:return r==0;case Op::BitNot:return float(~toInt32(r));case Op::Plus:return +r;case Op::Neg:return -r;
   case Op::CountBits:{uint32_t u=toUint32(r);int k=0;while(u){k+=u&1;u>>=1;}return float(k);}
   case Op::Sin:return std::sin(r);case Op::Cos:return std::cos(r);case Op::Tan:return std::tan(r);case Op::Asin:return std::asin(r);case Op::Acos:return std::acos(r);
   case Op::Atan:return std::atan(r);case Op::Abs:return std::fabs(r);case Op::Sign:return float((r>0)-(r<0));case Op::Ceil:return std::ceil(r);
   case Op::Floor:return std::floor(r);case Op::Trunc:return std::trunc(r);case Op::Round:return std::round(r);case Op::Frac:return r-std::floor(r);
   case Op::Sqrt:return std::sqrt(r);case Op::Rsqrt:return float(1.0/std::sqrt(r));case Op::Exp:return std::exp(r);case Op::Exp2:return std::exp2(r);
   case Op::Log:return std::log(r);case Op::Log2:return std::log2(r);case Op::Saturate:return float(std::max(0.0,std::min(double(r),1.0)));
   case Op::Random:return c.randomValue(r);default:throw std::logic_error("未知的一元运算符");}}
  case Node::Binary:{float l=evaluate(*n.lhs,c),r=evaluate(*n.rhs,c);switch(n.op){
   case Op::Pow:return std::pow(l,r);case Op::Mul:return l*r;case Op::Div:return l/r;case Op::FloorDiv:return std::floor(l/r);case Op::Mod:return std::fmod(l,r);
   case Op::Add:return l+r;case Op::Sub:return l-r;
   case Op::Shl:return float(int32_t(uint32_t(toInt32(l))<<(toInt32(r)&31)));case Op::Shr:return float(toInt32(l)>>(toInt32(r)&31));
   case Op::Lt:return l<r;case Op::Le:return l<=r;case Op::Gt:return l>r;case Op::Ge:return l>=r;case Op::Eq:return l==r;case Op::Ne:return l!=r;
   case Op::Same:return bitsOf(l)==bitsOf(r);case Op::NotSame:return bitsOf(l)!=bitsOf(r);
   case Op::BitAnd:return float(toInt32(l)&toInt32(r));case Op::BitXor:return float(toInt32(l)^toInt32(r));case Op::BitOr:return float(toInt32(l)|toInt32(r));
   case Op::And:return l!=0&&r!=0;case Op::Or:return l!=0||r!=0;default:throw std::logic_error("未知的二元运算符");}}}
 return 0;}

// Upstream ParseFloatToken: "0b" binary, otherwise wcstof (decimal, 0x hex, inf, nan); overflow becomes infinity.
inline bool parseFloatToken(const std::string& s,float& out,size_t& len){
 if(s.size()>=3&&s[0]=='0'&&s[1]=='b'){uint64_t v=0;size_t i=2;for(;i<s.size()&&(s[i]=='0'||s[i]=='1');i++)v=v*2+uint64_t(s[i]-'0');if(i==2)return false;out=float(v);len=i;return true;}
 errno=0;char* end=nullptr;out=std::strtof(s.c_str(),&end);if(end==s.c_str())return false;len=size_t(end-s.c_str());
 if(errno==ERANGE&&std::isinf(out))out=std::signbit(out)?-std::numeric_limits<float>::infinity():std::numeric_limits<float>::infinity();
 return true;}
inline bool identifierChar(char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';}
// Upstream FindVariableTokenEnd: "$name", or "$\parent\child\...\name" where the parent segment may hold anything
// up to the next backslash and later segments hold identifier characters and periods.
inline size_t variableTokenEnd(const std::string& s){size_t i=1;
 if(i<s.size()&&s[i]=='\\'){size_t close=s.find('\\',2);if(close==std::string::npos)return s.size();for(i=close+1;i<s.size()&&(identifierChar(s[i])||s[i]=='.'||s[i]=='\\');i++){}return i;}
 while(i<s.size()&&identifierChar(s[i]))i++;
 return i;}
inline bool validVariableName(const std::string& n){if(n.size()<2||n[0]!='$')return false;if(!((n[1]>='a'&&n[1]<='z')||n[1]=='_'))return false;
 for(size_t i=2;i<n.size();i++)if(!identifierChar(n[i]))return false;
 return true;}
// "x", "y3", "w127": an ini param (upstream ParseIniParamName).
inline bool iniParam(const std::string& s,int& index,char& comp){if(s.empty()||(s[0]!='x'&&s[0]!='y'&&s[0]!='z'&&s[0]!='w'))return false;comp=s[0];if(s.size()==1){index=0;return true;}
 long v=0;for(size_t i=1;i<s.size();i++){if(s[i]<'0'||s[i]>'9')return false;v=v*10+(s[i]-'0');if(v>INT_MAX)return false;}index=int(v);return true;}
// Slots a texture filter or checktextureoverride can name: ib, vb0..vb31, <stage>-t/cb/u<n>, o0..o7, od.
inline bool slotName(const std::string& s){
 auto digits=[&](size_t from){if(from>=s.size())return false;for(size_t i=from;i<s.size();i++)if(s[i]<'0'||s[i]>'9')return false;return true;};
 if(s=="ib"||s=="od"||s=="oD"||(startsWith(s,"vb")&&digits(2))||(s.size()>=2&&s[0]=='o'&&digits(1)))return true;
 static const char* stages[]={"vs-","hs-","ds-","gs-","ps-","cs-"};
 for(auto* st:stages)if(startsWith(s,st)){std::string rest=s.substr(3);if(startsWith(rest,"cb"))return rest.size()>2&&digits(5);if(rest[0]=='t'||rest[0]=='u')return digits(4);}
 return false;}

// The variable lookup of the place an expression is in: locals of the enclosing scopes, then the namespaced global,
// then the global (upstream parse_variable). Null: unknown.
using Resolver=std::function<Var*(const std::string&)>;

struct Expression {
 std::shared_ptr<Node> root;std::string text;
 float operator()(Context& c)const{return evaluate(*root,c);}
};

// text: lowercase, as ParseCommandList hands it over. inList: false for [Key] / [Preset] / include conditions, which
// accept fewer operand kinds.
inline Expression parseExpression(const std::string& text,const Resolver& resolve,bool inList){
 // A token, an operand, a parenthesised group, or an operator built by a pass. Operators keep their operands as
 // items, so a group under a unary operator still gets the later passes (upstream transform_operators_recursive).
 struct Item {enum Type {Token,Leaf,Group,Oper} type=Token;std::string token;size_t pos=0;std::unique_ptr<Node> leaf;std::vector<Item> group;Op op=Op::Add;
  std::unique_ptr<Item> l,r;bool operand()const{return type!=Token;}};
 std::vector<Item> items;size_t pos=0;
 auto allowed=[&](bool ok,size_t p,const std::string& what){if(!inList&&!ok)throw SyntaxError(what+" 不能在命令列表之外使用",p);};
 while(true){
  pos=text.find_first_not_of(" \t",pos);if(pos==std::string::npos)break;std::string rest=text.substr(pos);bool matched=false;
  for(auto* t:operatorTokens()){size_t n=std::strlen(t);if(rest.compare(0,n,t)==0){Item it;it.token=t;it.pos=pos;items.push_back(std::move(it));pos+=n;matched=true;break;}}
  if(matched)continue;
  for(auto& f:unaryOps()){size_t n=std::strlen(f.text);if(n>1&&rest.size()>n&&rest.compare(0,n,f.text)==0&&rest[n]=='('){Item it;it.token=f.text;it.pos=pos;items.push_back(std::move(it));pos+=n;matched=true;break;}}
  if(matched)continue;
  auto node=std::make_unique<Node>();node->pos=pos;size_t len=0;
  if(std::isdigit((unsigned char)rest[0])||rest[0]=='.'){
   if(!parseFloatToken(rest,node->value,len))throw SyntaxError("无法识别的浮点数："+rest,pos);
  }else if(rest[0]=='$'){
   if(startsWith(rest,"$pool"))throw SyntaxError("不支持资源池变量："+rest,pos);
   len=variableTokenEnd(rest);Var* v=resolve(rest.substr(0,len));if(!v)throw SyntaxError("无法识别的变量："+rest.substr(0,len),pos);
   node->kind=Node::Variable;node->var=v;
  }else{
   while(len<rest.size()&&identifierChar(rest[len]))len++;
   std::string word=rest.substr(0,len);size_t slotLen=len;while(slotLen<rest.size()&&(identifierChar(rest[slotLen])||rest[slotLen]=='-'))slotLen++;
   if(!word.empty()&&slotName(word)){node->kind=Node::Slot;node->slot=word;allowed(false,pos,"贴图过滤");}
   else if(!word.empty()&&iniParam(word,node->index,node->comp))node->kind=Node::Param;
   else if(const Keyword* k=word.empty()?nullptr:keyword(word)){node->kind=Node::Key;node->key=k;allowed(k->outsideLists,pos,word);}
   else if(!word.empty()&&parseFloatToken(rest,node->value,len)){}   // inf, nan
   else if(slotName(rest.substr(0,slotLen))){node->kind=Node::Slot;node->slot=rest.substr(0,slotLen);len=slotLen;allowed(false,pos,"贴图过滤");}
   else throw SyntaxError("不支持的操作数（资源目标、资源池、着色器和裁剪操作数未移植）："+rest,pos);
  }
  Item it;it.type=Item::Leaf;it.pos=pos;it.leaf=std::move(node);items.push_back(std::move(it));pos+=len;}
 // Parentheses: each ")" closes the nearest "(" before it.
 for(size_t i=0;i<items.size();i++){if(items[i].type!=Item::Token||items[i].token!=")")continue;size_t open=i;bool found=false;
  while(open>0){open--;if(items[open].type==Item::Token&&items[open].token=="("){found=true;break;}}
  if(!found)throw SyntaxError("多余的 )",items[i].pos);
  Item g;g.type=Item::Group;g.pos=items[open].pos;for(size_t k=open+1;k<i;k++)g.group.push_back(std::move(items[k]));
  items.erase(items.begin()+long(open),items.begin()+long(i)+1);items.insert(items.begin()+long(open),std::move(g));i=open;}
 for(auto& it:items)if(it.type==Item::Token&&it.token=="(")throw SyntaxError("未闭合的 (",it.pos);
 auto oper=[](Item& at,Op op,Item* l,Item* r){Item n;n.type=Item::Oper;n.op=op;n.pos=at.pos;if(l)n.l=std::make_unique<Item>(std::move(*l));n.r=std::make_unique<Item>(std::move(*r));return n;};
 std::function<void(std::vector<Item>&,int)> pass;
 std::function<void(Item&,int)> into=[&](Item& it,int which){if(it.type==Item::Group)pass(it.group,which);else if(it.type==Item::Oper){if(it.l)into(*it.l,which);into(*it.r,which);}};
 pass=[&](std::vector<Item>& list,int which){
  for(auto& it:list)into(it,which);
  if(list.empty())throw SyntaxError("括号内的表达式不能为空",0);
  if(which<0){   // unary operators and functions, right to left, where the right neighbour is an operand and the left one is not
   for(long i=long(list.size())-2;i>=0;i--){Item& t=list[size_t(i)];if(t.operand()||!list[size_t(i)+1].operand()||(i>0&&list[size_t(i)-1].operand()))continue;
    const OpName* f=nullptr;for(auto& u:unaryOps())if(t.token==u.text){f=&u;break;}if(!f)continue;
    Item n=oper(t,f->op,nullptr,&list[size_t(i)+1]);list.erase(list.begin()+i,list.begin()+i+2);list.insert(list.begin()+i,std::move(n));}
   return;}
  auto& ops=binaryPasses()[size_t(which)].first;bool right=binaryPasses()[size_t(which)].second;
  auto find=[&](const Item& t)->const OpName*{if(t.operand())return nullptr;for(auto& o:ops)if(t.token==o.text)return &o;return nullptr;};
  if(right){for(long i=long(list.size())-2;i>=1;i--){const OpName* o=find(list[size_t(i)]);if(!o||!list[size_t(i)-1].operand()||!list[size_t(i)+1].operand())continue;
    Item n=oper(list[size_t(i)],o->op,&list[size_t(i)-1],&list[size_t(i)+1]);list.erase(list.begin()+i-1,list.begin()+i+2);list.insert(list.begin()+i-1,std::move(n));i--;}}
  else{for(size_t i=1;i+1<list.size();i++){const OpName* o=find(list[i]);if(!o||!list[i-1].operand()||!list[i+1].operand())continue;
    Item n=oper(list[i],o->op,&list[i-1],&list[i+1]);list.erase(list.begin()+long(i)-1,list.begin()+long(i)+2);list.insert(list.begin()+long(i)-1,std::move(n));i--;}}};
 pass(items,-1);for(size_t p=0;p<binaryPasses().size();p++)pass(items,int(p));
 // Finalise: every list must have become one operand (upstream CommandListSyntaxTree::finalise).
 std::function<std::unique_ptr<Node>(std::vector<Item>&)> single;
 std::function<std::unique_ptr<Node>(Item&)> finish=[&](Item& it)->std::unique_ptr<Node>{
  if(it.type==Item::Leaf)return std::move(it.leaf);
  if(it.type==Item::Group)return single(it.group);
  if(it.type==Item::Token)throw SyntaxError("无法求值",it.pos);
  auto n=std::make_unique<Node>();n->kind=it.l?Node::Binary:Node::Unary;n->op=it.op;n->pos=it.pos;if(it.l)n->lhs=finish(*it.l);n->rhs=finish(*it.r);return n;};
 single=[&](std::vector<Item>& list)->std::unique_ptr<Node>{if(list.empty())throw SyntaxError("表达式为空",0);if(list.size()>1)throw SyntaxError("意外的内容",list[1].pos);return finish(list[0]);};
 Expression e;e.text=text;e.root=single(items);return e;}

// Upstream static_evaluate(evaluate_variables=true): values, variables, ini params and the frame clock; anything
// that needs a draw or the host fails.
struct StaticContext:Context {
 std::function<float(int,char)> params;float time=0,frameTime=0,frameNumber=0;bool failed=false;
 float keywordValue(const Keyword& k)override{std::string n=k.name;if(n=="time")return time;if(n=="frame_time")return frameTime;if(n=="frame_number")return frameNumber;failed=true;return 0;}
 float slotFilter(const std::string&)override{failed=true;return 0;}
 float param(int i,char c)override{return params?params(i,c):0;}
 float randomValue(float)override{failed=true;return 0;}
};
inline bool staticEvaluate(const Expression& e,StaticContext& c,float& out){c.failed=false;out=e(c);return !c.failed;}

}  // namespace migoto
