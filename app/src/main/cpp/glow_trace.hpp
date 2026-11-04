#pragma once
// Switch-in glow trace (debug log level + control.json experiments.glowTrace).
//
// Records who writes what to the materials and renderers GIBox works with, instead of guessing which parameter glows:
// every engine entry that can change a material, a MaterialPropertyBlock or a renderer's own properties is hooked, and
// a call is kept only when its target is a tracked object (a model's renderer, the game's original materials, our
// clones, anything the game derived from them, the blocks and proxies used on those renderers). The Runtime registers
// the objects, opens time windows (switch-in, attach, user mark) and reads the parameters back on both sides.
//
// All RVAs below come from dump.cs of build 2b4dd731 (game 7.1), carried to 6d85e314 (7.2) by name and signature (offsets.hpp; the Engine refuses other builds). Two things the dump shows decide
// how they are hooked:
//  * many icall entries are a single "B native" (4 bytes, next method right behind): the branch is followed and the
//    native function is hooked; a short entry that is not a branch is skipped and reported, never patched;
//  * public overloads have the same size as their Impl (an inlined copy), so both are hooked: only the outermost hooked
//    call on a thread records (writeDepth), the inner ones pass through.
// The hooks are installed for the trace only (Runtime::glowTick: debug log level and experiments.glowTrace both on), at
// most once per process: a game started without the trace hooks nothing. Switching the trace off stops recording
// (armed); the entries stay hooked until the game process ends, passing every call straight to the engine.
//
// GGCDDCOCKLG is the game's own Material subclass (renderer at +0x18, an int at +0x20 that is logged, not interpreted,
// two materials at +0x28/+0x30, a private method returning a Material that is followed as the one it writes to). Its overrides are hooked as "intent" with their own depth, so
// the engine write they end in is recorded too.
#include "runtime.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <functional>
#include <tuple>
#include <atomic>
#include <mutex>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace ms::glow {
enum class Scope:uint8_t{Material,Block,Renderer,Group,GameObject,Created,Names,Proxy};
enum class Shape:uint8_t{IdFloat,NameFloat,IdInt,NameInt,IdRef4,IdVec4,NameVec4,Vec4,IdPtr,NamePtr,Ptr1,IdRef2,IdVec2,NameVec2,Vec2,IdArray,Int1,Lerp,Name,IntInt,None,Ptr2,PtrInt,
 Create,Clone,Mats,Mat,MatAt,GetMat,GetMats,BlockInt,Block,SlotFloat,SlotRef,SlotPtr,SlotRemove,CustomFloat,CustomRef,CustomPtr,CustomRemove,CustomArray,GroupFloat,GroupRef,GroupInt,GroupPtr,
 IdRemove,NameToId,ProxyCtor,ProxyTarget};
#define GLOW_OPS(X) X(SetFloat)X(SetInt)X(SetColor)X(SetVector)X(SetTexture)X(SetTextureOffset)X(SetTextureScale)X(SetMainColor)X(SetMainTexture)X(SetMainTextureOffset)X(SetMainTextureScale)\
 X(SetFloatArray)X(SetVectorArray)X(SetColorArray)X(SetShader)X(SetRenderQueue)X(CopyPropertiesFrom)X(CopyAsCloneFrom)X(Lerp)X(EnableKeyword)X(DisableKeyword)X(EnableKeywordId)X(DisableKeywordId)\
 X(SetKeywords)X(ModifyKeywordSet)X(SetOverrideTag)X(SetShaderPass)X(SetOwner)X(CreatedFrom)X(ClonedFrom)X(SetMaterials)X(SetMaterial)X(SetMaterialAt)X(GotMaterial)X(GotMaterials)X(SetEnabled)\
 X(SetPropertyBlock)X(GotPropertyBlock)X(SetSlotPropertyBlock)X(GotSlotPropertyBlock)X(SlotCustomFloat)X(SlotCustomVector)X(SlotCustomColor)X(SlotCustomTexture)X(SlotCustomRemoveFloat)\
 X(SlotCustomRemoveVector)X(SlotCustomRemoveColor)X(CustomFloat)X(CustomVector)X(CustomColor)X(CustomTexture)X(CustomRemoveFloat)X(CustomRemoveVector)X(CustomRemoveColor)X(CustomClear)\
 X(CustomVectorArray)X(GroupCustomFloat)X(GroupCustomVector)X(GroupEnableProperty)X(GroupEnablePropertyArray)X(AllowSlotProperties)X(AllowInstancedMaterial)X(SetMaterialLayer)X(BlockClear)\
 X(BlockRemoveFloat)X(BlockRemoveColor)X(SetActive)X(PropertyToId)X(KeywordToId)X(ProxyCreated)X(ProxyTarget)X(ProxyRelease)X(Marker)
enum class Op:uint8_t{
#define GLOW_ENUM(n) n,
 GLOW_OPS(GLOW_ENUM)
#undef GLOW_ENUM
};
inline const char* opName(Op o){switch(o){
#define GLOW_NAME(n) case Op::n:return #n;
 GLOW_OPS(GLOW_NAME)
#undef GLOW_NAME
 }return "?";}
struct Hook {uintptr_t rva,span;Shape shape;Op op;Scope scope;const char* name;};
inline constexpr Hook kHooks[]={
 {0x80e6848,0x20,Shape::IdFloat,Op::SetFloat,Scope::Material,"Material.SetFloatImpl(int,float)"},
 {0x80e7ac8,0x20,Shape::IdFloat,Op::SetFloat,Scope::Material,"Material.SetFloat(int,float)"},
 {0x80e79e8,0xe0,Shape::NameFloat,Op::SetFloat,Scope::Material,"Material.SetFloat(string,float)"},
 {0x80e6868,0x24,Shape::IdInt,Op::SetInt,Scope::Material,"Material.SetIntImpl(int,int)"},
 {0x80e7b24,0x24,Shape::IdInt,Op::SetInt,Scope::Material,"Material.SetInt(int,int)"},
 {0x80e7ae8,0x3c,Shape::NameInt,Op::SetInt,Scope::Material,"Material.SetInt(string,int)"},
 {0x80e68c8,0x20,Shape::IdRef4,Op::SetColor,Scope::Material,"Material.INTERNAL_CALL_SetColorImpl(Material,int,Color*)"},
 {0x80e688c,0x3c,Shape::IdVec4,Op::SetColor,Scope::Material,"Material.SetColorImpl(int,Color)"},
 {0x80e7c48,0x3c,Shape::IdVec4,Op::SetColor,Scope::Material,"Material.SetColor(int,Color)"},
 {0x80e7b48,0x100,Shape::NameVec4,Op::SetColor,Scope::Material,"Material.SetColor(string,Color)"},
 {0x80e679c,0x18,Shape::Vec4,Op::SetMainColor,Scope::Material,"Material.set_color(Color)"},
 {0x80e6928,0x3c,Shape::IdRef4,Op::SetVector,Scope::Material,"Material.INTERNAL_CALL_SetVectorImpl(Material,int,Vector4*)"},
 {0x80e68e8,0x40,Shape::IdVec4,Op::SetVector,Scope::Material,"Material.SetVectorImpl(int,Vector4)"},
 {0x80e7ce0,0x40,Shape::IdVec4,Op::SetVector,Scope::Material,"Material.SetVector(int,Vector4)"},
 {0x80e7c84,0x5c,Shape::NameVec4,Op::SetVector,Scope::Material,"Material.SetVector(string,Vector4)"},
 {0x80e69a4,0x30,Shape::IdPtr,Op::SetTexture,Scope::Material,"Material.SetTextureImpl(int,Texture)"},
 {0x80e7e94,0x30,Shape::IdPtr,Op::SetTexture,Scope::Material,"Material.SetTexture(int,Texture)"},
 {0x80e7dbc,0xd8,Shape::NamePtr,Op::SetTexture,Scope::Material,"Material.SetTexture(string,Texture)"},
 {0x80e67cc,0x1c,Shape::Ptr1,Op::SetMainTexture,Scope::Material,"Material.set_mainTexture(Texture)"},
 {0x80e7178,0x6c,Shape::IdRef2,Op::SetTextureOffset,Scope::Material,"Material.INTERNAL_CALL_SetTextureOffsetImpl(Material,int,Vector2*)"},
 {0x80e7fec,0x78,Shape::IdVec2,Op::SetTextureOffset,Scope::Material,"Material.SetTextureOffset(int,Vector2)"},
 {0x80e7f04,0xe8,Shape::NameVec2,Op::SetTextureOffset,Scope::Material,"Material.SetTextureOffset(string,Vector2)"},
 {0x80e6800,0x18,Shape::Vec2,Op::SetMainTextureOffset,Scope::Material,"Material.set_mainTextureOffset(Vector2)"},
 {0x80e725c,0x6c,Shape::IdRef2,Op::SetTextureScale,Scope::Material,"Material.INTERNAL_CALL_SetTextureScaleImpl(Material,int,Vector2*)"},
 {0x80e814c,0x78,Shape::IdVec2,Op::SetTextureScale,Scope::Material,"Material.SetTextureScale(int,Vector2)"},
 {0x80e8064,0xe8,Shape::NameVec2,Op::SetTextureScale,Scope::Material,"Material.SetTextureScale(string,Vector2)"},
 {0x80e6830,0x18,Shape::Vec2,Op::SetMainTextureScale,Scope::Material,"Material.set_mainTextureScale(Vector2)"},
 {0x80e69d8,0x70,Shape::IdArray,Op::SetFloatArray,Scope::Material,"Material.SetFloatArrayImpl(int,float[],int)"},
 {0x80e6a48,0x70,Shape::IdArray,Op::SetVectorArray,Scope::Material,"Material.SetVectorArrayImpl(int,Vector4[],int)"},
 {0x80e6b28,0x70,Shape::IdArray,Op::SetColorArray,Scope::Material,"Material.SetColorArrayImpl(int,Color[],int)"},
 {0x80e675c,0x28,Shape::Ptr1,Op::SetShader,Scope::Material,"Material.SetShaderImpl(Shader)"},
 {0x80e6734,0x28,Shape::Ptr1,Op::SetShader,Scope::Material,"Material.set_shader(Shader)"},
 {0x80e7560,0x40,Shape::Int1,Op::SetRenderQueue,Scope::Material,"Material.SetCustomRenderQueueImpl(int)"},
 {0x80e7520,0x40,Shape::Int1,Op::SetRenderQueue,Scope::Material,"Material.set_renderQueue(int)"},
 {0x80e7634,0x94,Shape::Ptr1,Op::CopyPropertiesFrom,Scope::Material,"Material.CopyPropertiesFromMaterialImpl(Material)"},
 {0x80e75a0,0x94,Shape::Ptr1,Op::CopyPropertiesFrom,Scope::Material,"Material.CopyPropertiesFromMaterial(Material)"},
 {0x80e775c,0x94,Shape::Ptr1,Op::CopyAsCloneFrom,Scope::Material,"Material.CopyFromMaterialAsCloneImpl(Material)"},
 {0x80e76c8,0x94,Shape::Ptr1,Op::CopyAsCloneFrom,Scope::Material,"Material.CopyFromMaterialAsClone(Material)"},
 {0x80e73ec,0x4,Shape::Lerp,Op::Lerp,Scope::Material,"Material.LerpImpl(Material,Material,float)"},
 {0x80e73e8,0x4,Shape::Lerp,Op::Lerp,Scope::Material,"Material.Lerp(Material,Material,float)"},
 {0x80e77f4,0x4,Shape::Name,Op::EnableKeyword,Scope::Material,"Material.EnableKeywordImpl(string)"},
 {0x80e77f0,0x4,Shape::Name,Op::EnableKeyword,Scope::Material,"Material.EnableKeyword(string)"},
 {0x80e77fc,0x4,Shape::Name,Op::DisableKeyword,Scope::Material,"Material.DisableKeywordImpl(string)"},
 {0x80e77f8,0x4,Shape::Name,Op::DisableKeyword,Scope::Material,"Material.DisableKeyword(string)"},
 {0x80e781c,0x4,Shape::IntInt,Op::EnableKeywordId,Scope::Material,"Material.EnableKeywordIDImpl(int,bool)"},
 {0x80e7818,0x4,Shape::IntInt,Op::EnableKeywordId,Scope::Material,"Material.EnableKeywordID(int,bool)"},
 {0x80e7834,0x4,Shape::IntInt,Op::DisableKeywordId,Scope::Material,"Material.DisableKeywordIDImpl(int,bool)"},
 {0x80e7830,0x4,Shape::IntInt,Op::DisableKeywordId,Scope::Material,"Material.DisableKeywordID(int,bool)"},
 {0x80e792c,0x4,Shape::Ptr1,Op::SetKeywords,Scope::Material,"Material.SetShaderKeywordsImpl(string[])"},
 {0x80e7928,0x4,Shape::Ptr1,Op::SetKeywords,Scope::Material,"Material.set_shaderKeywords(string[])"},
 {0x80e785c,0x1c,Shape::None,Op::ModifyKeywordSet,Scope::Material,"Material.ModifyShaderKeywordSetForcelyImpl()"},
 {0x80e7840,0x1c,Shape::None,Op::ModifyKeywordSet,Scope::Material,"Material.ModifyShaderKeywordSetForcely()"},
 {0x80e73d4,0x4,Shape::Ptr2,Op::SetOverrideTag,Scope::Material,"Material.SetOverrideTagImpl(string,string)"},
 {0x80e73dc,0x4,Shape::PtrInt,Op::SetShaderPass,Scope::Material,"Material.SetShaderPassEnabledImpl(string,bool)"},
 {0x80e78ac,0x34,Shape::Ptr1,Op::SetOwner,Scope::Material,"Material.SetOwnerImpl(Object)"},
 {0x80e7878,0x34,Shape::Ptr1,Op::SetOwner,Scope::Material,"Material.SetOwner(Object)"},
 {0x80e78e0,0x34,Shape::Ptr1,Op::SetOwner,Scope::Material,"Material.SetOwnerNonVirtual(Object)"},
 {0x80e66f8,0x34,Shape::Create,Op::CreatedFrom,Scope::Created,"Material.Internal_CreateWithMaterial(Material,Material)"},
 {0x80e66c4,0x34,Shape::Create,Op::CreatedFrom,Scope::Created,"Material..ctor(Material)"},
 {0x8ffe308,0x4,Shape::Clone,Op::ClonedFrom,Scope::Created,"Object.Internal_CloneSingle(Object)"},
 {0x8f03260,0x4,Shape::Mats,Op::SetMaterials,Scope::Renderer,"Renderer.SetMaterialArrayImpl(Material[])"},
 {0x8f038cc,0x38,Shape::Mats,Op::SetMaterials,Scope::Renderer,"Renderer.SetMaterialArray(Material[])"},
 {0x8f0399c,0x38,Shape::Mats,Op::SetMaterials,Scope::Renderer,"Renderer.set_sharedMaterials(Material[])"},
 {0x8f03908,0x38,Shape::Mats,Op::SetMaterials,Scope::Renderer,"Renderer.set_materials(Material[])"},
 {0x8f03180,0x78,Shape::Mat,Op::SetMaterial,Scope::Renderer,"Renderer.SetMaterial(Material)"},
 {0x8f03710,0x78,Shape::Mat,Op::SetMaterial,Scope::Renderer,"Renderer.set_material(Material)"},
 {0x8f0378c,0x78,Shape::Mat,Op::SetMaterial,Scope::Renderer,"Renderer.set_sharedMaterial(Material)"},
 {0x8f031fc,0x4,Shape::MatAt,Op::SetMaterialAt,Scope::Renderer,"Renderer.SetSharedMaterialAt(Material,int)"},
 {0x8f0314c,0x30,Shape::GetMat,Op::GotMaterial,Scope::Renderer,"Renderer.GetMaterial()"},
 {0x8f036e0,0x30,Shape::GetMat,Op::GotMaterial,Scope::Renderer,"Renderer.get_material()"},
 {0x8f03200,0x4,Shape::GetMats,Op::GotMaterials,Scope::Renderer,"Renderer.GetMaterialArray()"},
 {0x8f03904,0x4,Shape::GetMats,Op::GotMaterials,Scope::Renderer,"Renderer.get_materials()"},
 {0x8f03334,0x4,Shape::Int1,Op::SetEnabled,Scope::Renderer,"Renderer.set_enabled(bool)"},
 {0x8f02898,0x4,Shape::BlockInt,Op::SetPropertyBlock,Scope::Renderer,"Renderer.SetPropertyBlock(MaterialPropertyBlock,bool)"},
 {0x8f028a4,0x54,Shape::Block,Op::GotPropertyBlock,Scope::Renderer,"Renderer.GetPropertyBlock(MaterialPropertyBlock)"},
 {0x8f02904,0x4,Shape::BlockInt,Op::SetSlotPropertyBlock,Scope::Renderer,"Renderer.SetPerMaterialPropertyBlock(MaterialPropertyBlock,int)"},
 {0x8f02900,0x4,Shape::BlockInt,Op::GotSlotPropertyBlock,Scope::Renderer,"Renderer.GetPerMaterialPropertyBlock(MaterialPropertyBlock,int)"},
 {0x8f02a38,0x4,Shape::SlotFloat,Op::SlotCustomFloat,Scope::Renderer,"Renderer.SetPerMaterialCustomPropertyFloat(int,int,float,bool)"},
 {0x8f02a6c,0x4,Shape::SlotRef,Op::SlotCustomVector,Scope::Renderer,"Renderer.INTERNAL_CALL_SetPerMaterialCustomPropertyVector(Renderer,int,int,Vector4*,bool)"},
 {0x8f02ac0,0x4,Shape::SlotRef,Op::SlotCustomColor,Scope::Renderer,"Renderer.INTERNAL_CALL_SetPerMaterialCustomPropertyColor(Renderer,int,int,Color*,bool)"},
 {0x8f02aec,0x4,Shape::SlotPtr,Op::SlotCustomTexture,Scope::Renderer,"Renderer.SetPerMaterialCustomPropertyTexture(int,int,Texture,bool)"},
 {0x8f02af8,0x4,Shape::SlotRemove,Op::SlotCustomRemoveFloat,Scope::Renderer,"Renderer.RemovePerMaterialCustomPropertyFloat(int,int,bool)"},
 {0x8f02afc,0x4,Shape::SlotRemove,Op::SlotCustomRemoveVector,Scope::Renderer,"Renderer.RemovePerMaterialCustomPropertyVector(int,int,bool)"},
 {0x8f02b00,0x4,Shape::SlotRemove,Op::SlotCustomRemoveColor,Scope::Renderer,"Renderer.RemovePerMaterialCustomPropertyColor(int,int,bool)"},
 {0x8f02b04,0x4,Shape::CustomFloat,Op::CustomFloat,Scope::Renderer,"Renderer.SetCustomPropertyFloat(int,float,bool)"},
 {0x8f02bd0,0x4,Shape::CustomRef,Op::CustomVector,Scope::Renderer,"Renderer.INTERNAL_CALL_SetCustomPropertyVector(Renderer,int,Vector4*,bool)"},
 {0x8f02d0c,0x4,Shape::CustomRef,Op::CustomColor,Scope::Renderer,"Renderer.INTERNAL_CALL_SetCustomPropertyColor(Renderer,int,Color*,bool)"},
 {0x8f02d50,0x4,Shape::CustomPtr,Op::CustomTexture,Scope::Renderer,"Renderer.SetCustomPropertyTexture(int,Texture,bool)"},
 {0x8f02b14,0x4,Shape::CustomRemove,Op::CustomRemoveFloat,Scope::Renderer,"Renderer.RemoveCustomPropertyFloat(int,bool)"},
 {0x8f02bfc,0x4,Shape::CustomRemove,Op::CustomRemoveVector,Scope::Renderer,"Renderer.RemoveCustomPropertyVector(int,bool)"},
 {0x8f02d38,0x4,Shape::CustomRemove,Op::CustomRemoveColor,Scope::Renderer,"Renderer.RemoveCustomPropertyColor(int,bool)"},
 {0x8f02b8c,0x4,Shape::None,Op::CustomClear,Scope::Renderer,"Renderer.ClearCustomProperty()"},
 {0x8f02d44,0x4,Shape::CustomArray,Op::CustomVectorArray,Scope::Renderer,"Renderer.SetCustomPropertyVectorArray(int,Vector4[],int,bool)"},
 {0x8f02b20,0x6c,Shape::GroupFloat,Op::GroupCustomFloat,Scope::Group,"Renderer.SetRenderersCustomPropertyFloat(Renderer[],int,float)"},
 {0x8f02c7c,0x68,Shape::GroupRef,Op::GroupCustomVector,Scope::Group,"Renderer.INTERNAL_CALL_SetRenderersCustomPropertyVector(Renderer[],int,Vector4*)"},
 {0x8f02d60,0x4,Shape::GroupInt,Op::GroupEnableProperty,Scope::Group,"Renderer.SetRenderersEnableProperty(Renderer[],bool)"},
 {0x8f02d64,0x90,Shape::GroupPtr,Op::GroupEnablePropertyArray,Scope::Group,"Renderer.SetRenderersEnablePropertyArray(Renderer[],bool[])"},
 {0x8f028fc,0x4,Shape::Int1,Op::AllowSlotProperties,Scope::Renderer,"Renderer.SetAllowPerMaterialProp(bool)"},
 {0x8f03a48,0x4,Shape::Int1,Op::AllowInstancedMaterial,Scope::Renderer,"Renderer.set_allowCreateInstantiatedMaterial(bool)"},
 {0x8f02e0c,0x4,Shape::Int1,Op::SetMaterialLayer,Scope::Renderer,"Renderer.set_matLayer(MaterialLayer)"},
 {0xb7b25cc,0x28,Shape::IdFloat,Op::SetFloat,Scope::Block,"MaterialPropertyBlock.SetFloatImpl(int,float)"},
 {0xb7b2c2c,0x28,Shape::IdFloat,Op::SetFloat,Scope::Block,"MaterialPropertyBlock.SetFloat(int,float)"},
 {0xb7b2bdc,0x50,Shape::NameFloat,Op::SetFloat,Scope::Block,"MaterialPropertyBlock.SetFloat(string,float)"},
 {0xb7b2638,0x28,Shape::IdRef4,Op::SetVector,Scope::Block,"MaterialPropertyBlock.INTERNAL_CALL_SetVectorImpl(MaterialPropertyBlock,int,Vector4*)"},
 {0xb7b25f4,0x44,Shape::IdVec4,Op::SetVector,Scope::Block,"MaterialPropertyBlock.SetVectorImpl(int,Vector4)"},
 {0xb7b2ccc,0x44,Shape::IdVec4,Op::SetVector,Scope::Block,"MaterialPropertyBlock.SetVector(int,Vector4)"},
 {0xb7b2c54,0x78,Shape::NameVec4,Op::SetVector,Scope::Block,"MaterialPropertyBlock.SetVector(string,Vector4)"},
 {0xb7b2844,0x28,Shape::IdRef4,Op::SetColor,Scope::Block,"MaterialPropertyBlock.INTERNAL_CALL_SetColorImpl(MaterialPropertyBlock,int,Color*)"},
 {0xb7b2800,0x44,Shape::IdVec4,Op::SetColor,Scope::Block,"MaterialPropertyBlock.SetColorImpl(int,Color)"},
 {0xb7b2d88,0x44,Shape::IdVec4,Op::SetColor,Scope::Block,"MaterialPropertyBlock.SetColor(int,Color)"},
 {0xb7b2d10,0x78,Shape::NameVec4,Op::SetColor,Scope::Block,"MaterialPropertyBlock.SetColor(string,Color)"},
 {0xb7b2748,0x2c,Shape::IdPtr,Op::SetTexture,Scope::Block,"MaterialPropertyBlock.SetTextureImpl(int,Texture)"},
 {0xb7b3004,0x60,Shape::IdPtr,Op::SetTexture,Scope::Block,"MaterialPropertyBlock.SetTexture(int,Texture)"},
 {0xb7b2f84,0x80,Shape::NamePtr,Op::SetTexture,Scope::Block,"MaterialPropertyBlock.SetTexture(string,Texture)"},
 {0xb7b2574,0x28,Shape::None,Op::BlockClear,Scope::Block,"MaterialPropertyBlock.Clear()"},
 {0xb7b25c4,0x4,Shape::IdRemove,Op::BlockRemoveFloat,Scope::Block,"MaterialPropertyBlock.RemoveFloatImpl(int)"},
 {0xb7b25c8,0x4,Shape::IdRemove,Op::BlockRemoveColor,Scope::Block,"MaterialPropertyBlock.RemoveColorImpl(int)"},
 {0x95779f8,0x4,Shape::Int1,Op::SetActive,Scope::GameObject,"GameObject.SetActive(bool)"},
 {0x9dd6d14,0xb0,Shape::NameToId,Op::PropertyToId,Scope::Names,"Shader.PropertyToID(string)"},
 {0x9dd6dc4,0xb8,Shape::NameToId,Op::KeywordToId,Scope::Names,"Shader.ShaderKeywordToID(string)"},
 {0xec6bd04,0x6c,Shape::ProxyCtor,Op::ProxyCreated,Scope::Proxy,"GGCDDCOCKLG..ctor(Material,Renderer,int)"},
 {0xec699ac,0x134,Shape::ProxyTarget,Op::ProxyTarget,Scope::Proxy,"GGCDDCOCKLG.JCOHIBBMOPA(bool)"},
 {0xec6d144,0x68,Shape::None,Op::ProxyRelease,Scope::Proxy,"GGCDDCOCKLG.GLDBIOGNELM()"},
 {0xec69b00,0x8c,Shape::IdFloat,Op::SetFloat,Scope::Proxy,"GGCDDCOCKLG.SetFloat(int,float)"},
 {0xec6a760,0x8c,Shape::NameFloat,Op::SetFloat,Scope::Proxy,"GGCDDCOCKLG.SetFloat(string,float)"},
 {0xec6bd70,0x8c,Shape::IdInt,Op::SetInt,Scope::Proxy,"GGCDDCOCKLG.SetInt(int,int)"},
 {0xec6bc50,0x8c,Shape::NameInt,Op::SetInt,Scope::Proxy,"GGCDDCOCKLG.SetInt(string,int)"},
 {0xec6b620,0xbc,Shape::IdVec4,Op::SetColor,Scope::Proxy,"GGCDDCOCKLG.SetColor(int,Color)"},
 {0xec6a024,0xbc,Shape::NameVec4,Op::SetColor,Scope::Proxy,"GGCDDCOCKLG.SetColor(string,Color)"},
 {0xec6af44,0xc0,Shape::Vec4,Op::SetMainColor,Scope::Proxy,"GGCDDCOCKLG.set_color(Color)"},
 {0xec6b564,0xbc,Shape::IdVec4,Op::SetVector,Scope::Proxy,"GGCDDCOCKLG.SetVector(int,Vector4)"},
 {0xec6c670,0xbc,Shape::NameVec4,Op::SetVector,Scope::Proxy,"GGCDDCOCKLG.SetVector(string,Vector4)"},
 {0xec6cf2c,0x8c,Shape::IdPtr,Op::SetTexture,Scope::Proxy,"GGCDDCOCKLG.SetTexture(int,Texture)"},
 {0xec6cd08,0x8c,Shape::NamePtr,Op::SetTexture,Scope::Proxy,"GGCDDCOCKLG.SetTexture(string,Texture)"},
 {0xec6aa60,0x7c,Shape::Ptr1,Op::SetMainTexture,Scope::Proxy,"GGCDDCOCKLG.set_mainTexture(Texture)"},
 {0xec6c564,0x98,Shape::IdVec2,Op::SetTextureOffset,Scope::Proxy,"GGCDDCOCKLG.SetTextureOffset(int,Vector2)"},
 {0xec6b004,0x98,Shape::IdVec2,Op::SetTextureScale,Scope::Proxy,"GGCDDCOCKLG.SetTextureScale(int,Vector2)"},
 {0xec6c814,0x74,Shape::Name,Op::EnableKeyword,Scope::Proxy,"GGCDDCOCKLG.EnableKeyword(string)"},
 {0xec6a344,0x74,Shape::Name,Op::DisableKeyword,Scope::Proxy,"GGCDDCOCKLG.DisableKeyword(string)"},
 {0xec6cd94,0x74,Shape::Int1,Op::EnableKeywordId,Scope::Proxy,"GGCDDCOCKLG.EnableKeywordID(int)"},
 {0xec6b464,0x8c,Shape::IntInt,Op::EnableKeywordId,Scope::Proxy,"GGCDDCOCKLG.EnableKeywordID(int,bool)"},
 {0xec6c5fc,0x74,Shape::Int1,Op::DisableKeywordId,Scope::Proxy,"GGCDDCOCKLG.DisableKeywordID(int)"},
 {0xec69ea4,0x8c,Shape::IntInt,Op::DisableKeywordId,Scope::Proxy,"GGCDDCOCKLG.DisableKeywordID(int,bool)"},
 {0xec6c72c,0x80,Shape::Ptr1,Op::SetKeywords,Scope::Proxy,"GGCDDCOCKLG.set_shaderKeywords(string[])"},
 {0xec69da4,0x74,Shape::Ptr1,Op::CopyPropertiesFrom,Scope::Proxy,"GGCDDCOCKLG.CopyPropertiesFromMaterial(Material)"},
 {0xec6b09c,0xa4,Shape::Lerp,Op::Lerp,Scope::Proxy,"GGCDDCOCKLG.Lerp(Material,Material,float)"},
 {0xec6ca80,0x7c,Shape::Ptr1,Op::SetShader,Scope::Proxy,"GGCDDCOCKLG.set_shader(Shader)"},
 {0xec6b740,0x80,Shape::Int1,Op::SetRenderQueue,Scope::Proxy,"GGCDDCOCKLG.set_renderQueue(int)"},
};
inline constexpr size_t kCount=sizeof(kHooks)/sizeof(kHooks[0]);
// Read-back entries (called by the Runtime on the Tick thread only).
inline constexpr abi::Offset rvaHasProperty=static_cast<abi::Offset>(0x80e739c),   // Material.HasPropertyImpl(int)
 rvaGetFloat=static_cast<abi::Offset>(0x80e6b98),rvaGetInt=static_cast<abi::Offset>(0x80e6bb8),
 rvaGetColor=static_cast<abi::Offset>(0x80e6c04),rvaGetVector=static_cast<abi::Offset>(0x80e6c58),   // INTERNAL_CALL_Get*Impl(self,id,out&)
 rvaGetTexture=static_cast<abi::Offset>(0x80e6d14),rvaGetScaleOffset=static_cast<abi::Offset>(0x80e70c0),
 rvaGetKeywords=static_cast<abi::Offset>(0x80e7920),rvaGetRenderQueue=static_cast<abi::Offset>(0x80e74e8),
 rvaGetShader=static_cast<abi::Offset>(0x80e672c),rvaGetTextureIds=static_cast<abi::Offset>(0x80e706c),
 rvaGetCustomFloat=static_cast<abi::Offset>(0x8f02b10),rvaFrameCount=static_cast<abi::Offset>(0x957a238),
 rvaPropertyToId=static_cast<abi::Offset>(0x9dd6d14);   // Shader.PropertyToID(string)

// ---------------------------------------------------------------- what is tracked
enum class Role:uint8_t{None,Original,Clone,Retired,Current,Assigned,Instance,Derived,Renderer,GameObject,Avatar,Block,Proxy};
inline const char* roleName(Role r){switch(r){case Role::Original:return "original";case Role::Clone:return "clone";case Role::Retired:return "retired clone";case Role::Current:return "current";
 case Role::Assigned:return "assigned";case Role::Instance:return "instance";case Role::Derived:return "copy";case Role::Renderer:return "renderer";case Role::GameObject:return "gameobject";
 case Role::Avatar:return "avatar";case Role::Block:return "block";case Role::Proxy:return "proxy";default:return "?";}}
// role | owner << 8 | slot << 24 | label << 40
inline uint64_t makeTag(Role r,uint16_t owner,int16_t slot,uint32_t label){return uint64_t(r)|(uint64_t(owner)<<8)|(uint64_t(uint16_t(slot))<<24)|(uint64_t(label&0xffffffu)<<40);}
inline Role roleOf(uint64_t t){return Role(t&0xff);}
inline uint16_t ownerOf(uint64_t t){return uint16_t(t>>8);}
inline int16_t slotOf(uint64_t t){return int16_t(uint16_t(t>>24));}
inline uint32_t labelOf(uint64_t t){return uint32_t(t>>40)&0xffffffu;}
inline bool materialRole(Role r){return r==Role::Original||r==Role::Clone||r==Role::Retired||r==Role::Current||r==Role::Assigned||r==Role::Instance||r==Role::Derived||r==Role::Proxy;}

// Open addressing on managed pointers. Readers (hooks, any thread) never lock: a key is published after its native
// pointer and tag; a hit counts only while the object's native pointer (+0x10, UnityEngine.Object and
// MaterialPropertyBlock alike) is still the one registered, so a reused address is a miss. Writers hold Trace::m.
class Table {
 static constexpr size_t N=4096,probe=64;static constexpr Ptr tomb=1;
 std::array<std::atomic<Ptr>,N> keys{};std::array<std::atomic<Ptr>,N> natives{};std::array<std::atomic<uint64_t>,N> tags{};
 std::array<uint32_t,N> gens{};std::array<uint64_t,N> expires{};
 static size_t slot(Ptr p){uint64_t x=p;x^=x>>33;x*=0xff51afd7ed558ccdull;x^=x>>33;return size_t(x)&(N-1);}
 public:
 size_t used=0;
 bool find(Ptr p,uint64_t& tag,bool check=true)const noexcept{if(p<=tomb)return false;size_t i=slot(p);
  for(size_t n=0;n<probe;n++,i=(i+1)&(N-1)){Ptr k=keys[i].load(std::memory_order_acquire);if(!k)return false;if(k!=p)continue;
   Ptr native=natives[i].load(std::memory_order_relaxed);uint64_t t=tags[i].load(std::memory_order_relaxed);
   if(check&&native&&load<Ptr>(p+0x10)!=native){return false;}tag=t;return true;}
  return false;}
 // gen 0: sticky until `expire` (ns); otherwise dropped by the first sweep of another generation.
 bool put(Ptr p,Ptr native,uint64_t tag,uint32_t gen,uint64_t expire){if(p<=tomb)return false;size_t i=slot(p),free=N;
  for(size_t n=0;n<probe;n++,i=(i+1)&(N-1)){Ptr k=keys[i].load(std::memory_order_relaxed);
   if(k==p){natives[i].store(native,std::memory_order_relaxed);tags[i].store(tag,std::memory_order_relaxed);if(gen||!gens[i])gens[i]=gen;expires[i]=expire;return true;}
   if(k==tomb&&free==N){free=i;}if(!k){if(free==N)free=i;break;}}
  if(free==N){return false;}natives[free].store(native,std::memory_order_relaxed);tags[free].store(tag,std::memory_order_relaxed);gens[free]=gen;expires[free]=expire;
  keys[free].store(p,std::memory_order_release);used++;return true;}
 void sweep(uint32_t gen,uint64_t now){for(size_t i=0;i<N;i++){Ptr k=keys[i].load(std::memory_order_relaxed);if(k<=tomb)continue;
   bool drop=gens[i]?gens[i]!=gen:expires[i]<now;if(drop){keys[i].store(tomb,std::memory_order_release);used--;}}}
 void clear(){for(size_t i=0;i<N;i++){keys[i].store(0,std::memory_order_release);gens[i]=0;expires[i]=0;}used=0;}
};

struct Payload {int32_t id=-1;Ptr name=0,name2=0;int32_t i0=0,i1=0;float v[4]={0,0,0,0};uint8_t nv=0;Ptr ref=0,ref2=0;};
struct Event {uint64_t seq=0,ns=0,lastNs=0;uint32_t frame=0,lastFrame=0,repeat=0;uint16_t hook=0xffff;Op op=Op::Marker;uint8_t own=0,nv=0;int32_t tid=0;Ptr target=0;uint64_t tag=0;
 int32_t id=-1,name=-1,i0=0,i1=0;float v[4]={0,0,0,0};Ptr ref=0,ref2=0;std::array<Ptr,3> caller{};};
enum Kind:uint8_t{KindFloat=1,KindInt=2,KindColor=4,KindVector=8,KindTexture=16,KindScaleOffset=32};
constexpr uint8_t kindOf(Op o){switch(o){
 case Op::SetFloat:case Op::CustomFloat:case Op::SlotCustomFloat:case Op::GroupCustomFloat:return KindFloat;
 case Op::SetInt:return KindInt;
 case Op::SetColor:case Op::SetMainColor:case Op::CustomColor:case Op::SlotCustomColor:return KindColor;
 case Op::SetVector:case Op::CustomVector:case Op::SlotCustomVector:case Op::GroupCustomVector:return KindVector;
 case Op::SetTexture:case Op::SetMainTexture:case Op::CustomTexture:case Op::SlotCustomTexture:return KindTexture;
 case Op::SetTextureOffset:case Op::SetTextureScale:case Op::SetMainTextureOffset:case Op::SetMainTextureScale:return KindScaleOffset;
 default:return 0;}}
inline bool keywordIdOp(Op o){return o==Op::EnableKeywordId||o==Op::DisableKeywordId;}
inline bool customOp(Op o){return o>=Op::SlotCustomFloat&&o<=Op::GroupEnablePropertyArray;}

// Threads: game writes are "GIBox" while one of the Runtime's entry points is on the stack (OwnScope).
inline thread_local unsigned writeDepth=0,proxyDepth=0,ownDepth=0;
struct OwnScope {OwnScope(){++ownDepth;}~OwnScope(){--ownDepth;}OwnScope(const OwnScope&)=delete;};

class Trace {
 public:
 std::atomic<bool> armed{false};std::atomic<uint64_t> testClockMs{0},dropped{0};
 using FrameFn=int32_t(*)();std::atomic<FrameFn> frameFn{nullptr};
 Ptr imageBase=0,imageSize=0;Table table;std::array<void*,kCount> originals{};
 // install(): entries already hooked, and the code each one hooked; everything: install() has run in this process.
 std::array<bool,kCount> done{};std::map<uintptr_t,size_t> taken;bool everything=false;unsigned hooked=0;
 std::mutex m;  // everything below, and Table writes
 static constexpr size_t ringSize=16384;std::vector<Event> ring;uint64_t head=0;
 struct Key {Ptr target;uint8_t op;int32_t id,name,i0;bool operator==(const Key& o)const{return target==o.target&&op==o.op&&id==o.id&&name==o.name&&i0==o.i0;}};
 struct KeyHash {size_t operator()(const Key& k)const{uint64_t h=k.target*0x9e3779b97f4a7c15ull;h^=uint64_t(k.op)<<56^uint64_t(uint32_t(k.id))<<20^uint64_t(uint32_t(k.name))^uint64_t(uint32_t(k.i0))<<40;return size_t(h^(h>>29));}};
 std::unordered_map<Key,uint64_t,KeyHash> last;
 std::vector<std::string> labels{"?"},strings;std::unordered_map<std::string,int32_t> stringIndex;
 std::unordered_map<int32_t,int32_t> propertyNames,keywordNames;   // id -> strings index
 std::map<int32_t,uint8_t> seenIds,seenCustomIds;std::map<std::string,uint8_t> seenNames;
 struct Owner {std::string model,part;unsigned character=0;};std::vector<Owner> owners;
 uint32_t generation=1;uint64_t recorded=0;

 uint64_t nowNs()const{auto t=testClockMs.load(std::memory_order_relaxed);if(t)return t*1000000ull;
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
 static std::string managedString(Ptr s,size_t max=96){if(!s)return {};int32_t n=load<int32_t>(s+0x10);if(n<0)return {};std::string out;
  for(int32_t i=0;i<n&&out.size()<max;i++){uint16_t c=load<uint16_t>(s+0x14+2*size_t(i));out+=c>=32&&c<127?char(c):'?';}return out;}
 int32_t intern(const std::string& s){auto it=stringIndex.find(s);if(it!=stringIndex.end())return it->second;if(strings.size()>=16384)return -1;
  int32_t i=int32_t(strings.size());strings.push_back(s);stringIndex.emplace(s,i);return i;}
 uint32_t label(const std::string& s){if(labels.size()>=(1u<<18))return 0;labels.push_back(s);return uint32_t(labels.size()-1);}
 // ---- registry (Runtime, Tick thread)
 uint16_t owner(const std::string& model,const std::string& part,unsigned character){std::lock_guard<std::mutex> l(m);
  for(size_t i=0;i<owners.size();i++)if(owners[i].model==model&&owners[i].part==part){owners[i].character=character;return uint16_t(i+1);}
  if(owners.size()>=0xfffe){return 0;}owners.push_back({model,part,character});return uint16_t(owners.size());}
 void beginRefresh(){std::lock_guard<std::mutex> l(m);generation=generation==0xffffffffu?1:generation+1;}
 void put(Ptr obj,Role r,uint16_t ownerId,int16_t slot,const std::string& text){if(!obj)return;std::lock_guard<std::mutex> l(m);
  uint64_t tag=0;if(table.find(obj,tag,false)&&labelOf(tag)&&roleOf(tag)==r&&ownerOf(tag)==ownerId&&slotOf(tag)==slot&&labels[labelOf(tag)]==text){table.put(obj,load<Ptr>(obj+0x10),tag,generation,0);return;}
  table.put(obj,load<Ptr>(obj+0x10),makeTag(r,ownerId,slot,label(text)),generation,0);}
 bool keep(Ptr obj){if(!obj)return false;std::lock_guard<std::mutex> l(m);uint64_t tag=0;if(!table.find(obj,tag))return false;table.put(obj,load<Ptr>(obj+0x10),tag,generation,0);return true;}
 void endRefresh(){std::lock_guard<std::mutex> l(m);table.sweep(generation,nowNs());}
 // ---- registry (hooks): objects the game made from, or put on, tracked ones; kept two minutes after the last sighting.
 uint64_t derive(Ptr obj,Role r,uint64_t source,int16_t slot,const char* how){uint64_t tag=0;if(!obj||table.find(obj,tag))return tag;std::lock_guard<std::mutex> l(m);
  std::string text=std::string(roleName(r))+(slot>=0?(r==Role::Proxy?" int ":" slot ")+std::to_string(slot):std::string())+" ("+how+") of "+labels[labelOf(source)<labels.size()?labelOf(source):0];tag=makeTag(r,ownerOf(source),slot,label(text));
  if(!table.put(obj,load<Ptr>(obj+0x10),tag,0,nowNs()+120000000000ull)){dropped++;}return tag;}
 bool groupTracked(Ptr rds,Ptr& subject,uint64_t& tag)const{auto n=load<uint64_t>(rds+0x18);if(n>256)return false;
  for(uint64_t i=0;i<n;i++){Ptr r=load<Ptr>(rds+0x20+8*i);if(r&&table.find(r,tag)){subject=r;return true;}}return false;}
 bool proxyTracked(Ptr self,uint64_t& tag){if(table.find(self,tag))return true;uint64_t source=0;
  for(unsigned off:{0x18u,0x28u,0x30u}){Ptr x=load<Ptr>(self+off);if(x&&table.find(x,source)){tag=derive(self,Role::Proxy,source,int16_t(load<int32_t>(self+0x20)),"proxy");return tag!=0;}}
  return false;}
 // ---- events
 static void stackOf(Ptr& lo,Ptr& hi){static thread_local Ptr l=0,h=0;static thread_local bool done=false;
  if(!done){done=true;pthread_attr_t a;if(pthread_getattr_np(pthread_self(),&a)==0){void* addr=nullptr;size_t size=0;if(pthread_attr_getstack(&a,&addr,&size)==0){l=reinterpret_cast<Ptr>(addr);h=l+size;}pthread_attr_destroy(&a);}}
  lo=l;hi=h;}
 Ptr rva(Ptr a)const{return imageSize&&a>=imageBase&&a<imageBase+imageSize?a-imageBase:a;}
 // The return address into the hooked function's caller, then two more frames up the frame-pointer chain.
 void callers(Ptr lr,Ptr fp,std::array<Ptr,3>& out)const{out={rva(lr),0,0};Ptr lo=0,hi=0;stackOf(lo,hi);if(!lo||fp<lo||fp+16>hi||(fp&7))return;
  Ptr f=load<Ptr>(fp);for(size_t i=1;i<3;i++){if(f<=fp||f<lo||f+16>hi||(f&7))return;Ptr ret=load<Ptr>(f+8);if(!ret)return;out[i]=rva(ret);fp=f;f=load<Ptr>(f);}}
 static int32_t tid(){static thread_local int32_t t=int32_t(::syscall(SYS_gettid));return t;}
 void record(uint16_t hook,Op op,Ptr target,uint64_t tag,const Payload& p,Ptr lr,Ptr fp){
  Event e;e.ns=e.lastNs=nowNs();auto ff=frameFn.load(std::memory_order_relaxed);e.frame=e.lastFrame=ff?uint32_t(ff()):0;e.hook=hook;e.op=op;e.own=ownDepth?1:0;e.tid=tid();
  e.target=target;e.tag=tag;e.id=p.id;e.i0=p.i0;e.i1=p.i1;e.nv=p.nv;std::memcpy(e.v,p.v,sizeof e.v);e.ref=p.ref;e.ref2=p.ref2;if(lr)callers(lr,fp,e.caller);
  std::string name;if(p.name)name=managedString(p.name);if(p.name2)name+="="+managedString(p.name2);
  if(op==Op::SetKeywords&&p.ref){auto n=load<uint64_t>(p.ref+0x18);for(uint64_t i=0;i<n&&i<24;i++)name+=(name.empty()?"":" ")+managedString(load<Ptr>(p.ref+0x20+8*i),64);if(!n)name="(none)";e.ref=0;}
  std::lock_guard<std::mutex> l(m);if(!name.empty())e.name=intern(name);
  if(materialRole(roleOf(tag))){uint8_t k=kindOf(op);if(k){if(e.id>=0&&seenIds.size()<512)seenIds[e.id]|=k;else if(!name.empty()&&seenNames.size()<512)seenNames[name]|=k;}}
  if(customOp(op)&&e.id>=0&&seenCustomIds.size()<256)seenCustomIds[e.id]|=kindOf(op);
  if(ring.size()!=ringSize)ring.assign(ringSize,Event{});
  Key key{target,uint8_t(op),e.id,e.name,e.i0};auto it=last.find(key);
  if(it!=last.end()){auto& o=ring[it->second%ringSize];if(o.seq==it->second&&o.own==e.own&&o.nv==e.nv&&std::memcmp(o.v,e.v,sizeof e.v)==0&&o.i1==e.i1&&o.ref==e.ref&&o.ref2==e.ref2){o.repeat++;o.lastNs=e.ns;o.lastFrame=e.frame;recorded++;return;}}
  e.seq=++head;ring[e.seq%ringSize]=e;if(last.size()>65536)last.clear();last[key]=e.seq;recorded++;}
 void marker(Ptr target,const std::string& what){uint64_t tag=0;table.find(target,tag,false);Event e;e.ns=e.lastNs=nowNs();auto ff=frameFn.load(std::memory_order_relaxed);e.frame=e.lastFrame=ff?uint32_t(ff()):0;
  e.op=Op::Marker;e.own=1;e.tid=tid();e.target=target;e.tag=tag;std::lock_guard<std::mutex> l(m);e.name=intern(what);if(ring.size()!=ringSize)ring.assign(ringSize,Event{});e.seq=++head;ring[e.seq%ringSize]=e;}
 void named(Op op,Ptr s,int32_t id){if(id<0)return;auto& map=op==Op::KeywordToId?keywordNames:propertyNames;
  {std::lock_guard<std::mutex> l(m);if(map.count(id)||map.size()>=16384)return;}auto text=managedString(s);std::lock_guard<std::mutex> l(m);auto i=intern(text);if(i>=0){map.emplace(id,i);}}
 // ---- read side (Runtime)
 std::vector<Event> collect(uint64_t from,uint64_t to,bool& truncated){std::lock_guard<std::mutex> l(m);std::vector<Event> out;truncated=false;if(ring.empty())return out;
  uint64_t first=head>=ringSize?head-ringSize+1:1;for(uint64_t s=first;s<=head;s++){auto& e=ring[s%ringSize];if(e.seq!=s)continue;if(s==first&&head>=ringSize&&e.ns>from)truncated=true;if(e.lastNs>=from&&e.ns<=to)out.push_back(e);}
  return out;}
 std::string text(int32_t i){std::lock_guard<std::mutex> l(m);return i>=0&&size_t(i)<strings.size()?strings[size_t(i)]:std::string();}
 std::string labelText(uint64_t tag){std::lock_guard<std::mutex> l(m);auto i=labelOf(tag);return i<labels.size()?labels[i]:std::string("?");}
 std::string property(int32_t id){std::lock_guard<std::mutex> l(m);auto it=propertyNames.find(id);return it==propertyNames.end()?std::string():strings[size_t(it->second)];}
 std::string keyword(int32_t id){std::lock_guard<std::mutex> l(m);auto it=keywordNames.find(id);return it==keywordNames.end()?std::string():strings[size_t(it->second)];}
 int32_t propertyId(const std::string& name){std::lock_guard<std::mutex> l(m);for(auto& kv:propertyNames)if(strings[size_t(kv.second)]==name)return kv.first;return -1;}
 std::map<int32_t,uint8_t> materialIds(){std::lock_guard<std::mutex> l(m);auto out=seenIds;if(seenNames.empty())return out;std::unordered_map<std::string,int32_t> ids;
  for(auto& p:propertyNames){ids.emplace(strings[size_t(p.second)],p.first);}for(auto& kv:seenNames){auto it=ids.find(kv.first);if(it!=ids.end())out[it->second]|=kv.second;}return out;}
 // Parameters written by name whose ID no hooked PropertyToID call revealed (the Runtime asks the engine for them).
 std::vector<std::string> unresolvedNames(){std::lock_guard<std::mutex> l(m);std::set<std::string> known;for(auto& p:propertyNames)known.insert(strings[size_t(p.second)]);
  std::vector<std::string> out;for(auto& kv:seenNames)if(!known.count(kv.first))out.push_back(kv.first);return out;}
 void learnProperty(int32_t id,const std::string& name){if(id<0)return;std::lock_guard<std::mutex> l(m);if(propertyNames.count(id))return;auto i=intern(name);if(i>=0){propertyNames.emplace(id,i);}}
 std::map<int32_t,uint8_t> customIds(){std::lock_guard<std::mutex> l(m);return seenCustomIds;}
 std::string labelOfObject(Ptr p){uint64_t tag=0;if(!table.find(p,tag,false))return {};return labelText(tag);}
 bool ownerOfTag(uint64_t tag,Owner& out){std::lock_guard<std::mutex> l(m);auto o=ownerOf(tag);if(!o||o>owners.size())return false;out=owners[o-1];return true;}
};
inline Trace& trace(){static Trace* t=new Trace();return *t;}   // process lifetime: Dobby trampolines point into it

// ---------------------------------------------------------------- hooks
struct V4 {float x,y,z,w;};struct V2 {float x,y;};
template<size_t I>struct Depth {bool on;
 Depth(){constexpr Scope s=kHooks[I].scope;if constexpr(s==Scope::Names)on=true;else if constexpr(s==Scope::Proxy)on=proxyDepth++==0;else on=writeDepth++==0;}
 ~Depth(){constexpr Scope s=kHooks[I].scope;if constexpr(s==Scope::Proxy)--proxyDepth;else if constexpr(s!=Scope::Names)--writeDepth;}
 Depth(const Depth&)=delete;};
template<size_t I,class F>inline F orig(){return reinterpret_cast<F>(trace().originals[I]);}
template<size_t I>inline void note(Ptr target,const Payload& p,Ptr lr,Ptr fp)noexcept{auto& t=trace();if(!t.armed.load(std::memory_order_relaxed)||!target)return;
 try{constexpr Scope s=kHooks[I].scope;uint64_t tag=0;Ptr subject=target;
  if constexpr(s==Scope::Group){if(!t.groupTracked(target,subject,tag))return;}
  else if constexpr(s==Scope::Proxy){if(!t.proxyTracked(target,tag))return;}
  else{if(!t.table.find(target,tag))return;}
  t.record(uint16_t(I),kHooks[I].op,subject,tag,p,lr,fp);}catch(...){t.dropped++;}}
// After the original: `obj` came from, or was put on, the tracked `from` (material, renderer, proxy).
template<size_t I>inline void follow(Ptr from,Ptr obj,Role role,int16_t slot,Payload p,Ptr lr,Ptr fp)noexcept{auto& t=trace();if(!t.armed.load(std::memory_order_relaxed)||!from||!obj)return;
 try{uint64_t source=0;constexpr Scope s=kHooks[I].scope;if constexpr(s==Scope::Proxy){if(!t.proxyTracked(from,source))return;}else if(!t.table.find(from,source))return;
  uint64_t tag=t.derive(obj,role,source,slot>=0?slot:slotOf(source),opName(kHooks[I].op));
  if constexpr(s==Scope::Created){p.ref=from;t.record(uint16_t(I),kHooks[I].op,obj,tag?tag:source,p,lr,fp);}
  else{p.ref=obj;t.record(uint16_t(I),kHooks[I].op,from,source,p,lr,fp);}}catch(...){t.dropped++;}}
#define GLOW_SITE const Ptr lr_=reinterpret_cast<Ptr>(__builtin_extract_return_addr(__builtin_return_address(0)));const Ptr fp_=reinterpret_cast<Ptr>(__builtin_frame_address(0));Depth<I> d_
#define GLOW_NOTE(target,...) if(d_.on){Payload p;__VA_ARGS__;note<I>(target,p,lr_,fp_);}
// Write shapes: recorded (trace), then made.
#define GLOW_WRITE(F,FILL,...) GLOW_SITE;const auto f_=orig<I,F>();Payload p;if(d_.on){FILL note<I>(s,p,lr_,fp_);}f_(s,__VA_ARGS__);
template<size_t I>void hIdFloat(Ptr s,int32_t id,float v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,float,Ptr),{p.id=id;p.v[0]=v;p.nv=1;},id,v,mi)}
template<size_t I>void hNameFloat(Ptr s,Ptr n,float v,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,float,Ptr),{p.name=n;p.v[0]=v;p.nv=1;},n,v,mi)}
template<size_t I>void hIdInt(Ptr s,int32_t id,int32_t v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,int32_t,Ptr),{p.id=id;p.i0=v;},id,v,mi)}
template<size_t I>void hNameInt(Ptr s,Ptr n,int32_t v,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,int32_t,Ptr),{p.name=n;p.i0=v;},n,v,mi)}
template<size_t I>void hIdRef4(Ptr s,int32_t id,const float* v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,const float*,Ptr),{p.id=id;if(v){std::memcpy(p.v,v,16);}p.nv=4;},id,v,mi)}
template<size_t I>void hIdVec4(Ptr s,int32_t id,V4 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,V4,Ptr),{p.id=id;std::memcpy(p.v,&v,16);p.nv=4;},id,v,mi)}
template<size_t I>void hNameVec4(Ptr s,Ptr n,V4 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,V4,Ptr),{p.name=n;std::memcpy(p.v,&v,16);p.nv=4;},n,v,mi)}
template<size_t I>void hVec4(Ptr s,V4 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,V4,Ptr),{std::memcpy(p.v,&v,16);p.nv=4;},v,mi)}
template<size_t I>void hIdPtr(Ptr s,int32_t id,Ptr x,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,Ptr,Ptr),{p.id=id;p.ref=x;},id,x,mi)}
template<size_t I>void hNamePtr(Ptr s,Ptr n,Ptr x,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,Ptr,Ptr),{p.name=n;p.ref=x;},n,x,mi)}
template<size_t I>void hPtr1(Ptr s,Ptr x,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,Ptr),{p.ref=x;},x,mi)}
template<size_t I>void hIdRef2(Ptr s,int32_t id,const float* v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,const float*,Ptr),{p.id=id;if(v){std::memcpy(p.v,v,8);}p.nv=2;},id,v,mi)}
template<size_t I>void hIdVec2(Ptr s,int32_t id,V2 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,V2,Ptr),{p.id=id;std::memcpy(p.v,&v,8);p.nv=2;},id,v,mi)}
template<size_t I>void hNameVec2(Ptr s,Ptr n,V2 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,V2,Ptr),{p.name=n;std::memcpy(p.v,&v,8);p.nv=2;},n,v,mi)}
template<size_t I>void hVec2(Ptr s,V2 v,Ptr mi){GLOW_WRITE(void(*)(Ptr,V2,Ptr),{std::memcpy(p.v,&v,8);p.nv=2;},v,mi)}
template<size_t I>void hIdArray(Ptr s,int32_t id,Ptr a,int32_t n,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,Ptr,int32_t,Ptr),{p.id=id;p.i0=n;},id,a,n,mi)}
template<size_t I>void hInt1(Ptr s,int32_t v,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,Ptr),{p.i0=v;if(keywordIdOp(kHooks[I].op)){p.id=v;}},v,mi)}
template<size_t I>void hLerp(Ptr s,Ptr a,Ptr b,float x,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,Ptr,float,Ptr),{p.ref=a;p.ref2=b;p.v[0]=x;p.nv=1;},a,b,x,mi)}
template<size_t I>void hName(Ptr s,Ptr n,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,Ptr),{p.name=n;},n,mi)}
template<size_t I>void hIntInt(Ptr s,int32_t a,int32_t b,Ptr mi){GLOW_WRITE(void(*)(Ptr,int32_t,int32_t,Ptr),{p.i0=a;p.i1=b;if(keywordIdOp(kHooks[I].op)){p.id=a;}},a,b,mi)}
template<size_t I>void hNone(Ptr s,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr),{},mi)}
template<size_t I>void hPtr2(Ptr s,Ptr a,Ptr b,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,Ptr,Ptr),{p.name=a;p.name2=b;},a,b,mi)}
template<size_t I>void hPtrInt(Ptr s,Ptr a,int32_t b,Ptr mi){GLOW_WRITE(void(*)(Ptr,Ptr,int32_t,Ptr),{p.name=a;p.i0=b;},a,b,mi)}
#undef GLOW_WRITE
template<size_t I>void hSlotFloat(Ptr s,int32_t m,int32_t id,float v,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i0=m;p.i1=u&0xff;p.v[0]=v;p.nv=1)orig<I,void(*)(Ptr,int32_t,int32_t,float,int32_t,Ptr)>()(s,m,id,v,u,mi);}
template<size_t I>void hSlotRef(Ptr s,int32_t m,int32_t id,const float* v,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i0=m;p.i1=u&0xff;if(v)std::memcpy(p.v,v,16);p.nv=4)orig<I,void(*)(Ptr,int32_t,int32_t,const float*,int32_t,Ptr)>()(s,m,id,v,u,mi);}
template<size_t I>void hSlotPtr(Ptr s,int32_t m,int32_t id,Ptr x,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i0=m;p.i1=u&0xff;p.ref=x)orig<I,void(*)(Ptr,int32_t,int32_t,Ptr,int32_t,Ptr)>()(s,m,id,x,u,mi);}
template<size_t I>uint32_t hSlotRemove(Ptr s,int32_t m,int32_t id,int32_t u,Ptr mi){GLOW_SITE;auto r=orig<I,uint32_t(*)(Ptr,int32_t,int32_t,int32_t,Ptr)>()(s,m,id,u,mi);GLOW_NOTE(s,p.id=id;p.i0=m;p.i1=int32_t(r&0xff))return r;}
template<size_t I>void hCustomFloat(Ptr s,int32_t id,float v,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i1=u&0xff;p.v[0]=v;p.nv=1)orig<I,void(*)(Ptr,int32_t,float,int32_t,Ptr)>()(s,id,v,u,mi);}
template<size_t I>void hCustomRef(Ptr s,int32_t id,const float* v,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i1=u&0xff;if(v)std::memcpy(p.v,v,16);p.nv=4)orig<I,void(*)(Ptr,int32_t,const float*,int32_t,Ptr)>()(s,id,v,u,mi);}
template<size_t I>void hCustomPtr(Ptr s,int32_t id,Ptr x,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i1=u&0xff;p.ref=x)orig<I,void(*)(Ptr,int32_t,Ptr,int32_t,Ptr)>()(s,id,x,u,mi);}
template<size_t I>uint32_t hCustomRemove(Ptr s,int32_t id,int32_t u,Ptr mi){GLOW_SITE;auto r=orig<I,uint32_t(*)(Ptr,int32_t,int32_t,Ptr)>()(s,id,u,mi);GLOW_NOTE(s,p.id=id;p.i1=int32_t(r&0xff))return r;}
template<size_t I>void hCustomArray(Ptr s,int32_t id,Ptr a,int32_t n,int32_t u,Ptr mi){GLOW_SITE;GLOW_NOTE(s,p.id=id;p.i0=n;p.i1=u&0xff)orig<I,void(*)(Ptr,int32_t,Ptr,int32_t,int32_t,Ptr)>()(s,id,a,n,u,mi);}
template<size_t I>void hGroupFloat(Ptr rds,int32_t id,float v,Ptr mi){GLOW_SITE;GLOW_NOTE(rds,p.id=id;p.v[0]=v;p.nv=1)orig<I,void(*)(Ptr,int32_t,float,Ptr)>()(rds,id,v,mi);}
template<size_t I>void hGroupRef(Ptr rds,int32_t id,const float* v,Ptr mi){GLOW_SITE;GLOW_NOTE(rds,p.id=id;if(v)std::memcpy(p.v,v,16);p.nv=4)orig<I,void(*)(Ptr,int32_t,const float*,Ptr)>()(rds,id,v,mi);}
template<size_t I>void hGroupInt(Ptr rds,int32_t b,Ptr mi){GLOW_SITE;GLOW_NOTE(rds,p.i0=b&0xff)orig<I,void(*)(Ptr,int32_t,Ptr)>()(rds,b,mi);}
template<size_t I>void hGroupPtr(Ptr rds,Ptr a,Ptr mi){GLOW_SITE;GLOW_NOTE(rds,p.ref=a)orig<I,void(*)(Ptr,Ptr,Ptr)>()(rds,a,mi);}
template<size_t I>uint32_t hIdRemove(Ptr s,int32_t id,Ptr mi){GLOW_SITE;auto r=orig<I,uint32_t(*)(Ptr,int32_t,Ptr)>()(s,id,mi);GLOW_NOTE(s,p.id=id;p.i1=int32_t(r&0xff))return r;}
// Registration shapes: the call goes first, then what it produced or attached is tracked.
template<size_t I>void hMats(Ptr s,Ptr arr,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,Ptr)>()(s,arr,mi);
 if(d_.on&&arr){auto n=load<uint64_t>(arr+0x18);for(uint64_t i=0;i<n&&i<32;i++){Payload p;p.i0=int32_t(i);follow<I>(s,load<Ptr>(arr+0x20+8*i),Role::Assigned,int16_t(i),p,lr_,fp_);}}}
template<size_t I>void hMat(Ptr s,Ptr x,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,Ptr)>()(s,x,mi);if(d_.on)follow<I>(s,x,Role::Assigned,0,Payload{},lr_,fp_);}
template<size_t I>void hMatAt(Ptr s,Ptr x,int32_t at,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,int32_t,Ptr)>()(s,x,at,mi);if(d_.on){Payload p;p.i0=at;follow<I>(s,x,Role::Assigned,int16_t(at),p,lr_,fp_);}}
template<size_t I>Ptr hGetMat(Ptr s,Ptr mi){GLOW_SITE;Ptr r=orig<I,Ptr(*)(Ptr,Ptr)>()(s,mi);if(d_.on)follow<I>(s,r,Role::Instance,0,Payload{},lr_,fp_);return r;}
template<size_t I>Ptr hGetMats(Ptr s,Ptr mi){GLOW_SITE;Ptr r=orig<I,Ptr(*)(Ptr,Ptr)>()(s,mi);
 if(d_.on&&r){auto n=load<uint64_t>(r+0x18);for(uint64_t i=0;i<n&&i<32;i++){Payload p;p.i0=int32_t(i);follow<I>(s,load<Ptr>(r+0x20+8*i),Role::Instance,int16_t(i),p,lr_,fp_);}}return r;}
template<size_t I>void hBlockInt(Ptr s,Ptr b,int32_t i,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,int32_t,Ptr)>()(s,b,i,mi);if(d_.on){Payload p;p.i0=i;follow<I>(s,b,Role::Block,-1,p,lr_,fp_);}}
template<size_t I>void hBlock(Ptr s,Ptr b,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,Ptr)>()(s,b,mi);if(d_.on)follow<I>(s,b,Role::Block,-1,Payload{},lr_,fp_);}
template<size_t I>void hCreate(Ptr obj,Ptr source,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,Ptr)>()(obj,source,mi);if(d_.on)follow<I>(source,obj,Role::Derived,-1,Payload{},lr_,fp_);}
template<size_t I>Ptr hClone(Ptr data,Ptr mi){GLOW_SITE;Ptr r=orig<I,Ptr(*)(Ptr,Ptr)>()(data,mi);if(d_.on)follow<I>(data,r,Role::Derived,-1,Payload{},lr_,fp_);return r;}
template<size_t I>int32_t hNameToId(Ptr n,Ptr mi){Depth<I> d_;int32_t r=orig<I,int32_t(*)(Ptr,Ptr)>()(n,mi);if(n&&trace().armed.load(std::memory_order_relaxed))try{trace().named(kHooks[I].op,n,r);}catch(...){}return r;}
template<size_t I>void hProxyCtor(Ptr s,Ptr x,Ptr r,int32_t slot,Ptr mi){GLOW_SITE;orig<I,void(*)(Ptr,Ptr,Ptr,int32_t,Ptr)>()(s,x,r,slot,mi);
 if(d_.on&&trace().armed.load(std::memory_order_relaxed))try{auto& t=trace();uint64_t source=0;if(!(r&&t.table.find(r,source))&&!(x&&t.table.find(x,source)))return;
  uint64_t tag=t.derive(s,Role::Proxy,source,int16_t(slot),"proxy");Payload p;p.ref=x;p.ref2=r;p.i0=slot;t.record(uint16_t(I),kHooks[I].op,s,tag?tag:source,p,lr_,fp_);}catch(...){trace().dropped++;}}
template<size_t I>Ptr hProxyTarget(Ptr s,int32_t create,Ptr mi){GLOW_SITE;Ptr r=orig<I,Ptr(*)(Ptr,int32_t,Ptr)>()(s,create,mi);if(d_.on){Payload p;p.i0=create&0xff;follow<I>(s,r,Role::Instance,-1,p,lr_,fp_);}return r;}
#undef GLOW_NOTE
#undef GLOW_SITE
template<size_t I>void* replacement(){constexpr Shape s=kHooks[I].shape;
#define GLOW_R(x) if constexpr(s==Shape::x)return reinterpret_cast<void*>(&h##x<I>);else
 GLOW_R(IdFloat)GLOW_R(NameFloat)GLOW_R(IdInt)GLOW_R(NameInt)GLOW_R(IdRef4)GLOW_R(IdVec4)GLOW_R(NameVec4)GLOW_R(Vec4)GLOW_R(IdPtr)GLOW_R(NamePtr)GLOW_R(Ptr1)GLOW_R(IdRef2)GLOW_R(IdVec2)
 GLOW_R(NameVec2)GLOW_R(Vec2)GLOW_R(IdArray)GLOW_R(Int1)GLOW_R(Lerp)GLOW_R(Name)GLOW_R(IntInt)GLOW_R(None)GLOW_R(Ptr2)GLOW_R(PtrInt)GLOW_R(Create)GLOW_R(Clone)GLOW_R(Mats)GLOW_R(Mat)
 GLOW_R(MatAt)GLOW_R(GetMat)GLOW_R(GetMats)GLOW_R(BlockInt)GLOW_R(Block)GLOW_R(SlotFloat)GLOW_R(SlotRef)GLOW_R(SlotPtr)GLOW_R(SlotRemove)GLOW_R(CustomFloat)GLOW_R(CustomRef)GLOW_R(CustomPtr)
 GLOW_R(CustomRemove)GLOW_R(CustomArray)GLOW_R(GroupFloat)GLOW_R(GroupRef)GLOW_R(GroupInt)GLOW_R(GroupPtr)GLOW_R(IdRemove)GLOW_R(NameToId)GLOW_R(ProxyCtor)GLOW_R(ProxyTarget)
 return nullptr;
#undef GLOW_R
}
template<size_t... I>std::array<void*,kCount> replacementsOf(std::index_sequence<I...>){return {replacement<I>()...};}
inline const std::array<void*,kCount>& replacements(){static const auto r=replacementsOf(std::make_index_sequence<kCount>{});return r;}

// ---------------------------------------------------------------- install
// An unconditional "B imm26" is followed to its target (up to four hops). Otherwise the entry is hooked where it is,
// but only when the dump leaves at least 16 bytes before the next method; anything shorter is reported, not patched.
inline bool branch(uint32_t insn,uintptr_t at,uintptr_t& to){if((insn&0xFC000000u)!=0x14000000u)return false;int64_t imm=int64_t(insn&0x03FFFFFFu);if(imm&0x02000000)imm-=0x04000000;
 to=uintptr_t(int64_t(at)+imm*4);return true;}
inline bool resolveEntry(const uint8_t* image,uintptr_t size,const Hook& h,uintptr_t& target,std::string& how){uintptr_t at=h.rva;how.clear();
 for(int hop=0;hop<4;hop++){if(size&&at+4>size){how="outside the image";return false;}uint32_t insn;std::memcpy(&insn,image+at,4);uintptr_t to=0;
  if(!branch(insn,at,to)){break;}char b[32];std::snprintf(b,sizeof b,"%sB 0x%lx",how.empty()?"":" ",static_cast<unsigned long>(to));how+=b;at=to;}
 if(at==h.rva&&h.span<16){char b[48];std::snprintf(b,sizeof b,"%lu-byte entry is not a branch",static_cast<unsigned long>(h.span));how=b;return false;}
 if(size&&at+16>size){how="outside the image";return false;}
 if(how.empty()){how="direct";}target=at;return true;}
// Every entry, each hooked at most once per process: a later call adds only the ones not done yet. Without an installer
// (host tests) nothing is marked as done.
inline std::string install(Engine& e){auto& t=trace();if(!e.hook)return "no inline hook installer in this process";
 if(!t.imageBase){t.imageBase=e.base;t.imageSize=e.size;}const uint8_t* image=reinterpret_cast<const uint8_t*>(e.base);const auto& reps=replacements();
 unsigned tried=0,hooked=0,shared=0,skipped=0,failed=0;std::string lines;char b[160];
 for(size_t i=0;i<kCount;i++){if(t.done[i])continue;t.done[i]=true;tried++;const Hook& h=kHooks[i];uintptr_t target=0;std::string how;
  std::snprintf(b,sizeof b,"\n%s 0x%lx: ",h.name,static_cast<unsigned long>(h.rva));lines+=b;
  if(!resolveEntry(image,e.size,h,target,how)){skipped++;lines+="skipped, "+how;continue;}
  lines+=how;auto prev=t.taken.find(target);
  if(prev!=t.taken.end()){shared++;lines+=std::string(", same code as ")+kHooks[prev->second].name+(kHooks[prev->second].shape==h.shape?"":" (other signature: left to it)");continue;}
  std::string bytes;for(int k=0;k<16;k++){std::snprintf(b,sizeof b,"%02x",image[target+k]);bytes+=b;}lines+=" ["+bytes+"]";
  if(!reps[i]||!e.hook(target,reps[i],&t.originals[i])||!t.originals[i]){failed++;t.originals[i]=nullptr;lines+=", hook failed";continue;}
  t.taken[target]=i;t.hooked++;hooked++;}
 if(e.size&&!t.frameFn.load())t.frameFn.store(reinterpret_cast<Trace::FrameFn>(e.resolver?e.resolver(rvaFrameCount):e.base+rvaFrameCount));
 t.everything=true;
 std::snprintf(b,sizeof b,"%u of %u entries hooked, %u share code with another entry, %u skipped, %u failed (%u hooked in this process)",hooked,tried,shared,skipped,failed,t.hooked);
 return std::string(b)+lines;}

// ---------------------------------------------------------------- reading the record
inline std::string number(float f){if(!std::isfinite(f))return "nan";char b[32];std::snprintf(b,sizeof b,"%.4g",double(f));return b;}
inline std::string vec(const float* v,unsigned n){std::string s="(";for(unsigned i=0;i<n;i++)s+=(i?",":"")+number(v[i]);return s+")";}
inline std::string hexPtr(Ptr p){char b[24];std::snprintf(b,sizeof b,"0x%llx",static_cast<unsigned long long>(p));return b;}
struct Reader {   // names and labels for one summary
 Trace& t;std::string object(Ptr p){if(!p)return "null";auto l=t.labelOfObject(p);return l.empty()?hexPtr(p):l;}
 std::string property(const Event& e){if(e.name>=0&&!keywordIdOp(e.op)){auto s=t.text(e.name);if(!s.empty())return s;}
  if(keywordIdOp(e.op)){auto k=t.keyword(e.id);return (k.empty()?"keyword":k)+"#"+std::to_string(e.id);}
  if(e.id<0){return "";}auto n=t.property(e.id);return (n.empty()?"":n)+"#"+std::to_string(e.id);}
 std::string value(const Event& e){switch(e.op){
  case Op::SetInt:case Op::SetRenderQueue:case Op::SetEnabled:case Op::SetActive:case Op::SetMaterialLayer:case Op::AllowSlotProperties:case Op::AllowInstancedMaterial:case Op::GroupEnableProperty:return std::to_string(e.i0);
  case Op::SetTexture:case Op::SetMainTexture:case Op::CustomTexture:case Op::SlotCustomTexture:return "texture "+object(e.ref);
  case Op::SetShader:return "shader "+hexPtr(e.ref);case Op::SetOwner:return "owner "+object(e.ref);
  case Op::CopyPropertiesFrom:case Op::CopyAsCloneFrom:case Op::CreatedFrom:case Op::ClonedFrom:return "from "+object(e.ref);
  case Op::Lerp:return object(e.ref)+" .. "+object(e.ref2)+" t="+number(e.v[0]);
  case Op::SetMaterials:case Op::GotMaterials:return "slot "+std::to_string(e.i0)+" = "+object(e.ref);
  case Op::SetMaterial:case Op::GotMaterial:return object(e.ref);case Op::SetMaterialAt:return "slot "+std::to_string(e.i0)+" = "+object(e.ref);
  case Op::SetPropertyBlock:case Op::GotPropertyBlock:return "block "+object(e.ref);
  case Op::SetSlotPropertyBlock:case Op::GotSlotPropertyBlock:return "slot "+std::to_string(e.i0)+" block "+object(e.ref);
  case Op::EnableKeywordId:case Op::DisableKeywordId:return e.i1?"modify":"";
  case Op::SetFloatArray:case Op::SetVectorArray:case Op::SetColorArray:case Op::CustomVectorArray:return std::to_string(e.i0)+" values";
  case Op::SlotCustomRemoveFloat:case Op::SlotCustomRemoveVector:case Op::SlotCustomRemoveColor:return "slot "+std::to_string(e.i0)+(e.i1?" removed":" (absent)");
  case Op::CustomRemoveFloat:case Op::CustomRemoveVector:case Op::CustomRemoveColor:case Op::BlockRemoveFloat:case Op::BlockRemoveColor:return e.i1?"removed":"(absent)";
  case Op::SetShaderPass:return e.i0?"on":"off";case Op::ProxyCreated:return "over "+object(e.ref)+", int "+std::to_string(e.i0)+", renderer "+object(e.ref2);
  case Op::ProxyTarget:return "-> "+object(e.ref)+(e.i0?" (create)":"");case Op::GroupEnablePropertyArray:return "array "+hexPtr(e.ref);
  default:break;}
  std::string s=e.nv==1?number(e.v[0]):e.nv?vec(e.v,e.nv):"";if(e.op>=Op::SlotCustomFloat&&e.op<=Op::SlotCustomTexture)s="slot "+std::to_string(e.i0)+" "+s;return s;}
};
// One window as log lines (without the "glow-write" key): per object and parameter, how often, first and last value, the values in between, who called.
inline std::vector<std::string> summarize(Trace& t,const std::vector<Event>& events,const std::function<bool(uint64_t)>& keep,uint64_t t0,unsigned number,size_t maxRows,std::string& counts){
 Reader r{t};struct Row {Event first,last;uint64_t count=0;std::vector<std::string> values;bool game=false,own=false;Event gameFirst;bool haveGame=false;};
 std::map<std::tuple<Ptr,uint8_t,int32_t,int32_t,int32_t>,Row> rows;uint64_t game=0,own=0;
 for(auto& e:events){if(!keep(e.tag))continue;auto& row=rows[{e.target,uint8_t(e.op),e.id,e.name,e.i0}];uint64_t n=1+e.repeat;
  if(!row.count){row.first=e;}row.last=e;row.count+=n;(e.own?own:game)+=n;if(e.own)row.own=true;else{row.game=true;if(!row.haveGame){row.gameFirst=e;row.haveGame=true;}}
  auto v=r.value(e);if(row.values.empty()||row.values.back()!=v){if(row.values.size()<6)row.values.push_back(v);else row.values.back()="…"+v;}}
 counts=std::to_string(game)+" by the game, "+std::to_string(own)+" by GIBox";
 std::vector<Row*> order;for(auto& kv:rows)order.push_back(&kv.second);
 std::stable_sort(order.begin(),order.end(),[](Row* a,Row* b){if(a->game!=b->game)return a->game;if(a->first.ns!=b->first.ns)return a->first.ns<b->first.ns;return a->first.seq<b->first.seq;});
 auto ms=[&](uint64_t ns){long long d=(long long)(ns/1000000)-(long long)(t0/1000000);return (d>=0?"+":"")+std::to_string(d)+"ms";};
 std::vector<std::string> out;size_t n=0;
 for(Row* row:order){if(n++>=maxRows){out.push_back("#"+std::to_string(number)+" … "+std::to_string(order.size()-maxRows)+" more rows not shown");break;}
  auto& e=row->first;std::string who=row->game&&row->own?"game+GIBox":row->game?"game":"GIBox";
  std::string line="#"+std::to_string(number)+" "+ms(e.ns)+" f"+std::to_string(e.frame)+" "+who+" x"+std::to_string(row->count)+" "+opName(e.op);
  auto prop=r.property(e);if(!prop.empty())line+=" "+prop;line+=" on "+(e.op==Op::Marker?r.object(e.target):t.labelText(e.tag));
  std::string vals;for(auto& v:row->values)vals+=(vals.empty()?"":" -> ")+v;if(!vals.empty())line+=": "+vals;
  if(row->count>1)line+=" (last "+ms(row->last.lastNs)+" f"+std::to_string(row->last.lastFrame)+")";
  auto& c=row->haveGame?row->gameFirst.caller:e.caller;if(c[0]){line+=" by ";for(size_t i=0;i<3&&c[i];i++)line+=(i?"<":"")+hexPtr(c[i]);}
  out.push_back(line);}
 return out;}
}
