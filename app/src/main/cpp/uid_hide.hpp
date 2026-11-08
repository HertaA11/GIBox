#pragma once
// Hiding the account UID on screen (settings → 通用, control.json "hideUid"; 0.7.104). From the user's
// GIBox_UID_Hide 7.2 source (CN + global): Unity UI.Text whose string is a "UID" label followed by a colon and at least
// four digits, or a bare "UID:" prefix, is switched off (Behaviour.set_enabled(false)) when the game sets it and draws
// nothing (VertexHelper.Clear) when it is laid out. The account, the network and the UID itself are not touched, no
// mask or replacement text is drawn, and no string content is logged. The hooks are in bootstrap.cpp.
#include <atomic>
#include <cstdint>

namespace ms::uid {
// Live switch: the hooks are installed when "hideUid" is on as the game starts; turning it off stops hiding new text.
inline std::atomic<bool> on{false};
// "UID" not preceded by a letter or digit (not GUID), optional spaces, ':' or '：', optional spaces, then 4+ digits or
// the end of the string.
inline bool looksLikeUid(const char16_t* s,int32_t length){
 if(!s||length<4||length>160)return false;
 auto up=[](char16_t x)->char16_t{return x>=u'a'&&x<=u'z'?char16_t(x-(u'a'-u'A')):x;};
 auto space=[](char16_t x){return x==u' '||x==u'\t'||x==u' ';};
 for(int32_t i=0;i+2<length;i++){
  if(up(s[i])!=u'U'||up(s[i+1])!=u'I'||up(s[i+2])!=u'D')continue;
  if(i>0){char16_t b=up(s[i-1]);if((b>=u'A'&&b<=u'Z')||(b>=u'0'&&b<=u'9'))continue;}
  int32_t p=i+3;while(p<length&&space(s[p]))p++;
  if(p>=length||(s[p]!=u':'&&s[p]!=u'：'))continue;
  p++;while(p<length&&space(s[p]))p++;
  int digits=0;while(p<length&&s[p]>=u'0'&&s[p]<=u'9'){digits++;p++;}
  if(digits>=4||(digits==0&&p==length))return true;}
 return false;}
// A managed System.String: length at +0x10, UTF-16 characters from +0x14.
inline bool managedUid(uintptr_t str){return str&&looksLikeUid(reinterpret_cast<const char16_t*>(str+0x14),*reinterpret_cast<const int32_t*>(str+0x10));}
}
