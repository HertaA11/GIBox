#pragma once
#include <atomic>
#include <cstdint>
namespace ms::abi {
inline constexpr const char* buildId="6d85e3141bcd6b508f3e853bab508cc407278eac";
// RVAs valid only for the above GNU Build ID (game 7.2). Every method RVA below was carried over from the 7.1 build
// (2b4dd731...) by its declaration in the two dump.cs files: by name and signature where names are not obfuscated, by the
// class's shape and the method's signature where they are (tick: LFEMMJADHPE.Tick, receive: GCGEJOPPGAE's only
// (ulong, bool, uint, GameObject) method, the material proxy DCCKGJFKIID at shape score 1.0). Native code
// and data that no dump declares are notFound+n here until found in this build's libyuanshen.so (pendingRequired):
// distinct values no RVA of this image reaches, never called (the runtime does not start, an optional hook is skipped).
inline constexpr const char* pendingRequired="";
// The engine allocation for the skin weights: on (0.7.79 had it off after the 0.7.78 crash, whose cause was the wrong
// nativeAllocator, see below). Off: the mesh's own skin block is reused when it has room. A variable so host tests run both.
inline bool allocVerified=true;
// MemoryManager: its "initialised" byte (Allocate takes the label's allocator only when it is 1).
inline constexpr uintptr_t managerActive=0xc4e;
// The entity manager: *(base + manager) + managerSlot (7.1: 0x159c1638, 0x8c00). Read from the 7.2 accessor that
// returns it, NKIIBJKODNH.POKJNBEFKHA() at 0xc887d04 (7.1: AHJIDBJFIJE.MKFJCCPNOAC()):
// ADRP X8, 0x15809000; MOV W9, #0x9070; LDR X8, [X8, #0x5a0]; LDR X0, [X8, X9]; RET.
inline constexpr uintptr_t managerSlot=0x9070;
// Found in the 7.2 libyuanshen.so (gibox_find_72 over the user's copy), each checked two ways:
//  typeObject 0x43b1824: the 7.1 entry bytes, and the call right before FindObjectsOfType / GetComponentsInChildren.
//  classFromType 0x43b24b4: the 7.1 entry bytes, at one place only.
//  wrapperFor 0x5b4dd98: Texture2D.get_whiteTexture loads a native texture and tail-calls it, as in 7.1; +0xbbe34 from
//   7.1 like findObjectsNative next to it (+0xbbe30), FindObjectsOfType's MOV W1,#1; B target.
//  objectNew 0x43cdaf8: the call right before 4155 of 8290 constructor calls (the next one 246), every
//   MaterialPropertyBlock..ctor call among them: the game's own `new` (7.1 used 0xfa5e86c, another entry).
//  lodBind 0x584ffc4, meshBones 0x579ca94: the 7.1 entry bytes, each the only hit near where the 7.1 address lands with
//   the shift of the pairs above (19 and 34 hits in the whole image); 0xb3530 apart (7.1: 0xb3290).
//  arrayNew 0x43beb7c: what Guid.ToByteArray, BitConverter.GetBytes (short/int/long/uint/ulong), MemoryStream(int),
//   String.ToCharArray and Delegate.GetInvocationList call with X0 = the array type and W1 = the length their source
//   gives (16, 2, 4, 8, 4, 8, 1); 34660 calls (7.1 used 0x43252b8, another entry). Reads the class's element size at +0xb4.
//  nativeAlloc 0x517b2b4: MemoryManager::Reallocate(manager, ptr, size, align, label, options, file, line) (decompiled):
//   a null ptr goes to Allocate(manager, size, align, label, ...) at 0x43b4138, which uses the label's allocator
//   (manager + 0x1e38 + 16 * label for labels up to 0xa8) once the manager's byte +0xc4e is 1, else its fallback +0xc60.
//  nativeAllocator 0x15d92120: the MemoryManager instance (every caller passes *(0x15d92120), created there on first use).
//   0.7.78 used 0x156a0840 by mistake: the bump pointer that instance is placed with (+0x2950 per use), not a manager;
//   its "manager" was unused buffer space (+0xc4e 0, +0xc60 null): the device crash.
//  The native Mesh data offsets GIBox reads (+0x48, then +0x74 vertices, +0xc0 submeshes, +0xe0 bones) are what
//   Mesh.get_vertexCount / get_subMeshCount / get_bindposes read in 7.2 (bindposes: data +0xd0, count +0xe0, so the
//   next dynamic array, the skin, is +0xf0 / label +0xf8 / size +0x100 / capacity +0x108 as before).
// GCGEJOPPGAE (7.1 JDIIHHFJKBM, the entity): its MonoVisualEntityTool backing field (the only field of that type; 7.1:
// 0xf8); MonoVisualEntityTool's SkinnedMeshRenderer[] (the only one; 0x68 in both builds).
inline constexpr uintptr_t entityVisual=0x320,visualRenderers=0x68;
// Fields 7.2 reshuffled among others of the same type, so the dump alone cannot tell them apart: the runtime picks them
// from these candidates (every field of that type in the 7.2 classes) by what they hold in the game (Runtime::calibrate):
// the manager's List<GCGEJOPPGAE> (7.1: 0x10) and current avatar's runtime ID (uint, 7.1: 0x288); the entity's config
// ID (uint, 7.1: 0x3a4) and runtime ID (uint, 7.1: 0x430).
inline constexpr uint16_t managerLists[]={0x18,0x30,0x60,0x70,0x98,0x160,0x170,0x180,0x188,0x1e0,0x1f0,0x1f8};
inline constexpr uint16_t managerIds[]={0x230,0x234,0x260,0x2a8,0x2c0};
inline constexpr uint16_t entityIds[]={0x330,0x374,0x3a4,0x3cc,0x3ec,0x3f4,0x448,0x45c,0x464,0x468,0x49c,0x4a4};
// The entity's config and runtime ID fields once the runtime has found them (0 before), for the hand-off diagnostics.
inline std::atomic<uint16_t> liveConfig{0},liveRuntime{0};
// The entity's root GameObject field (7.1: 0x278; 7.2: one of 0x1e0 / 0x268, not identified yet): 0 = not read.
inline std::atomic<uint16_t> liveRoot{0};
#ifdef GIBOX_HOST_TEST
inline constexpr uintptr_t notFound=0x15fff000;   // host tests: inside their fake image, which answers these too
inline constexpr bool found(uintptr_t rva){return rva!=0;}
#else
inline constexpr uintptr_t notFound=0xfffff000;
inline constexpr bool found(uintptr_t rva){return rva!=0&&rva<notFound;}
#endif
enum Offset:uintptr_t {
 tick=0x7e8cb08,manager=0x158095a0,
 getMesh=0x8f4217c,setMesh=0x8f4235c,getBones=0x8f420b4,getMaterials=0x8f03940,setMaterials=0x8f0399c,
 clone=0x8ffeb20,destroy=0x8ffe3a0,getName=0x8ffe494,getParent=0x8fffc18,
 arrayNew=0x43beb7c,objectNew=0x43cdaf8,pin=0xed0741c,unpin=0xed07434,
 // Mesh.set_isReadable(bool).
 setReadable=0xc53a98c,setIndexFormat=0xc53a828,getBindposes=0xc53a984,
 getPositions=0xc53adcc,setPositions=0xc53addc,setNormals=0xc53adfc,
 getUV=0xc53ae28,setUV=0xc53ae38,setUV2=0xc53ae58,setUV3=0xc53ae78,setUV4=0xc53ae98,
 getColors=0xc53aec4,setColors=0xc53aed8,getTangents=0xc53ae0c,setTangents=0xc53ae18,
 getTriangles=0xc53b1f4,setTriangles=0xc53b298,setSubmeshCount=0xc53a9dc,setSubmeshTriangles=0xc53b7a0,
 meshFinish=0xc53bf14,meshUpload=0xc53c15c,
 // dump.cs: public Mesh.MarkDynamic(), static Time.get_frameCount().
 meshDynamic=0xc53c124,frameCount=0x957a238,
 textureType=0x8dcbaac,textureInit=0x8dcb9d8,texturePixels=0x8dcbe44,textureApply=0x8dcbf68,
 // dump.cs: UnityEngine.Object.set_hideFlags(HideFlags) / get_hideFlags() (MethodIndex 18972 / 18971; HideFlags.DontUnloadUnusedAsset = 32).
 setHideFlags=0x8ffd8a0,getHideFlags=0x8ffe62c,
 mainTexture=0x80e67cc,
 // newString(null, const char*) returns a managed System.String*; the two Material calls take that pointer (a property NAME), not an int ID.
 newString=0x8ed74ac,hasProperty=0x80e72c8,setTexture=0x80e7dbc,
 // Engine allocator: alloc(*(B+nativeAllocator), 0, bytes, 16, label 15, 0, name, 1); the skin-weight block of a built mesh.
 nativeAlloc=0x517b2b4,nativeAllocator=0x15d92120,
 // Object.FindObjectsOfType(Type): managed entry is "MOV W1,#1 (active scene objects); B 0x5a982c0".
 // The native body reads [Type+0x10] (Il2CppType*), strings "FindAllObjectsOfType: ...". Returns Object[].
 findObjectsOfType=0x8ffe48c,
 // The native body itself: (Type, mode). The object flag word (+0xC) filters by mode: 1 skips persistent objects (bit 20,
 // assets) and inactive ones; 2 skips nothing (the "MOV W1,#2" thunk at 0x49ea170, Resources.FindObjectsOfTypeAll).
 // Capture (0.7.86) calls it with mode 2 for every loaded Mesh (hash-report.txt "loaded, not shown").
 findObjectsNative=0x5b540f0,
 // GameObject.GetComponentsInChildren(Type, bool includeInactive) -> Component[]; Component.get_gameObject.
 componentsInChildren=0x9577734,gameObject=0x80e5b50,
 // JDIIHHFJKBM.AAKKKEMMMCA(this, ulong key, bool success, uint handle, GameObject asset, MethodInfo*): the game's async
 // model hand-off. In handoff-20377 (7 hand-offs, 5 characters, party screen / detail page) the asset arrived as an
 // inactive GameObject whose SkinnedMeshRenderers already had Mesh, materials and bones, on the Tick thread, and nothing
 // changed them until after the entity's ready/activate calls.
 receive=0x9564b4c,
 // The game's own mesh + bone binding (locator capture, PID 25113): both call the engine's internal mesh setter
 // (0x56d82b0, not the managed set_sharedMesh) and then bind bones from the mesh's bone-name hashes (native Mesh +0x90 is
 // their count). This is how the game put its mesh back over ours (hand-off, LOD changes).
 //   lodBind(loader native, Mesh native, renderer native)                    - native LOD switch
 //   meshBones(renderer native, Mesh native, hash container, bone container) - SkinnedMeshRenderer.BindMeshBones
 lodBind=0x584ffc4,meshBones=0x579ca94,
 // Scripting::ScriptingWrapperFor(Object*) -> managed object (Texture2D.whiteTexture tail-calls it).
 wrapperFor=0x5b4dd98,
 // il2cpp Reflection::GetTypeObject(const Il2CppType*) -> System.Type; IL2CPP typeof code calls it right before 0x8ffe48c.
 typeObject=0x43b1824,
 // il2cpp Class::FromIl2CppType(const Il2CppType*, bool) -> Il2CppClass*; reads the type enum at +0xA.
 classFromType=0x43b24b4,
 // Texture-only diagnostics (texture_diag.hpp), read-only getters from dump.cs of this build. This build exports no il2cpp_*
 // API (0.7.49 device log: "missing export il2cpp_domain_get"), so the named-method path cannot reach them.
 //   Mesh: get_subMeshCount(), get_vertexCount(), get_isReadable(), and the icalls behind GetIndexStart(int) /
 //   GetIndexCount(int), GetIndexStartImpl / GetIndexCountImpl (no managed check that could throw through our frames; called
 //   only for 0 <= submesh < get_subMeshCount()). The build has no Mesh.GetTopology, GetBaseVertex or get_indexFormat.
 //   Texture: static Internal_GetWidth(Texture) / Internal_GetHeight(Texture) (any texture); Texture2D: get_format(),
 //   get_mipmapCount() (called only on objects whose class is Texture2D.whiteTexture's: Texture2D is sealed).
 meshSubmeshes=0xc53a9b8,meshVertices=0xc53a994,meshReadable=0xc53a988,meshIndexStart=0xc53a82c,meshIndexCount=0xc53a874,
 textureWidth=0xc808dc0,textureHeight=0xc808de4,textureFormat=0x8dcbaa8,textureMips=0x8dcb9b0
};
// First 16 code bytes of the hand-off and binding entries (from the handoff and locator captures);
// the hand-off hook is installed only when all match.
struct CodeCheck {Offset rva;const char* hex;};
inline constexpr CodeCheck handoffChecks[]={{componentsInChildren,"e403022ae2031f2a23008052e5031f2a"},
 {gameObject,"614d1c16604d1c16c00000b4080840f9"},{receive,"fe4fbfa9288706f0e50304aae403032a"}};
inline constexpr CodeCheck bindChecks[]={{lodBind,"ff8304d1e9230b6dfd7b0ca9fc6f0da9"},{meshBones,"ff8304d1e85b00fdfd7b0ca9fc6f0da9"}};
}
