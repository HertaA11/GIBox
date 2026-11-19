# 国际服 7.2 功能补齐

适用 Build ID：`32f920af1fd831959a440d294fa996a09657afb8`。

依据：用户提供的国际服 `/storage/emulated/0/dump.cs`、国服 7.2 dump，以及本机已安装国际服的 `libyuanshen.so`。ELF GNU Build ID 与 dump 一致；下表所有方法的 RVA 均落在 ELF 可执行段。Unity 方法按 MethodIndex、完整类名和签名核对；游戏类的方法按完整类名和签名定位，**不能沿用国服的 MethodIndex**。

补齐静态武器的 MeshFilter / 新建 Mesh、加载图片（含重复传送、比例和 Clamp）、取消角色渐隐及右上角随身合成台。NPC 和风之翼原有蒙皮路径继续使用原来的双服实现，MeshFilter 静态发现现在也可用于国际服。

## 方法对应

| 配置项 | 国际服声明 | MethodIndex | RVA |
| --- | --- | ---: | --- |
| `loadingRender` | `UnityEngine.Canvas.SendWillRenderCanvases` | 23205 | `0x13104728` |
| `loadingStart` | `MoleMole.MonoLoadingCanvas.Start` | 403784 | `0xe421750` |
| `loadingBackground` | `MoleMole.MonoLoadingCanvas.get_bgImg` | 403783 | `0xe421748` |
| `loadingDestroy` | `MoleMole.MonoLoadingCanvas.OnDestroy` | 403779 | `0xe421630` |
| `imageEnable` | `UnityEngine.UI.Image.OnEnable` | 28406 | `0x10fbdea4` |
| `imageDisable` | `UnityEngine.UI.Image.OnDisable` | 28407 | `0x10fbdf44` |
| `imageSprite` | `UnityEngine.UI.Image.get_sprite` | 28369 | `0x10fba524` |
| `imageSetSprite` | `UnityEngine.UI.Image.set_sprite` | 28370 | `0x10fba52c` |
| `imageSetOverride` | `UnityEngine.UI.Image.set_overrideSprite` | 28372 | `0x10fbaa20` |
| `imageType` | `UnityEngine.UI.Image.get_type` | 28374 | `0x10fbaba4` |
| `imageSetType` | `UnityEngine.UI.Image.set_type` | 28375 | `0x10fbabac` |
| `imageAspect` | `UnityEngine.UI.Image.get_preserveAspect` | 28376 | `0x10fbabdc` |
| `imageSetAspect` | `UnityEngine.UI.Image.set_preserveAspect` | 28377 | `0x10fbabe4` |
| `graphicRect` | `UnityEngine.UI.Graphic.get_rectTransform` | 28301 | `0x10f41c20` |
| `rectBounds` | `UnityEngine.RectTransform.get_rect` | 19193 | `0x91bf7cc` |
| `textureWrap` | `UnityEngine.Texture.set_wrapMode` | 18682 | `0xa86596c` |
| `textureFilter` | `UnityEngine.Texture.set_filterMode` | 18678 | `0xa8658dc` |
| `graphicColor` | `UnityEngine.UI.Graphic.get_color` | 28281 | `0x10f41928` |
| `graphicSetColor` | `UnityEngine.UI.Graphic.set_color` | 28282 | `0x10f41934` |
| `spriteCreate` | `UnityEngine.Sprite.Create` | 18634 | `0x8435f80` |
| `imageActive` | `UnityEngine.Behaviour.get_isActiveAndEnabled` | 16275 | `0xa7eb780` |
| `goActive` | `UnityEngine.GameObject.get_activeInHierarchy` | 16843 | `0x97a818c` |
| `goActiveSelf` | `UnityEngine.GameObject.get_activeSelf` | 16842 | `0x97a8188` |
| `goSetActive` | `UnityEngine.GameObject.SetActive` | 16841 | `0x97a8184` |
| `filterMesh` | `UnityEngine.MeshFilter.get_sharedMesh` | 19618 | `0xabc1a74` |
| `filterSetMesh` | `UnityEngine.MeshFilter.set_sharedMesh` | 19619 | `0xabc1c2c` |
| `cameraFadeSet` | `BGMCJLBBGJF.PGODCGCKIHI` | 177305 | `0xd36e1f4` |
| `synthesisHudFrame` | `BFIFKIIDFKN.LateUpdateView` | 451970 | `0x115e3014` |
| `synthesisClick` | `UnityEngine.Events.UnityEvent.Invoke` | 20457 | `0x903e454` |
| `synthesisOpen` | `OADFBEEGOFD.EJNIGFFNPCC` | 504322 | `0x8947a00` |
| `uiGoTransform` | `UnityEngine.GameObject.get_transform` | 16836 | `0x97a7f74` |
| `uiComponentTransform` | `UnityEngine.Component.get_transform` | 16602 | `0x80e876c` |
| `uiChildCount` | `UnityEngine.Transform.get_childCount` | 19161 | `0x91c458c` |
| `uiChild` | `UnityEngine.Transform.GetChild` | 19179 | `0x91c47ec` |
| `uiSetParent` | `UnityEngine.Transform.SetParent` | 19102 | `0x91c2e64` |
| `uiSetSibling` | `UnityEngine.Transform.SetSiblingIndex` | 19165 | `0x91c45f4` |
| `uiSetName` | `UnityEngine.Object.set_name` | 18950 | `0x91bf0a8` |
| `uiFindChild` | `UnityEngine.Transform.Find` | 19167 | `0x91c4680` |
| `uiNewClick` | `UnityEngine.UI.Button.ButtonClickedEvent.ButtonClickedEvent` | 28096 | `0x10fc3160` |
| `uiSetClick` | `UnityEngine.UI.Button.set_onClick` | 28091 | `0x11d71c74` |
| `uiSetTransition` | `UnityEngine.UI.Selectable.set_transition` | 28836 | `0xaca2cdc` |
| `uiTargetGraphic` | `UnityEngine.UI.Selectable.set_targetGraphic` | 28844 | `0xaca2fa8` |
| `meshInit` | `UnityEngine.Mesh.Mesh` | 19668 | `0xabc4348` |

## 字段布局

- `BFIFKIIDFKN.EIBHHLHHNHC`（`MonoInLevelMainPage`）：国服 `+0x4f0`，国际服 `+0x238`，由客户端配置选择。
- `MonoInLevelMainPage.grpTopBtn`：两服均为 `+0x98`。
- `MonoInLevelMainPage._bagPageToggle`：两服均为 `+0xf0`。
- `UI.Button.m_OnClick`：两服均为 `+0xd8`。
- `MonoLoadingCanvas._bgImg`：两服均为 `+0x28`；`UI.Image.m_OverrideSprite`：两服均为 `+0xe0`。

## 入口指令

国际服表已覆盖当前全部 125 项偏移；包括旧的模型交接、网格绑定在内，14 个挂钩入口的校验字节均与本机 ELF 精确一致。以下列出此次新增挂钩的 16 字节，来自该 Build ID 的国际服 ELF，不从国服地址推算。运行时沿用现有指令校验，并随客户端选择相应签名；不符合时跳过该功能的挂钩。

| 配置项 | 入口 16 字节 |
| --- | --- |
| `cameraFadeSet` | `ffc300d1e80b00fdfe0f00f9f44f02a9` |
| `synthesisHudFrame` | `fe4fbfa9680d02f008a97f391f050071` |
| `synthesisClick` | `fe5fbda9f65701a9f44f02a9f30300aa` |
| `synthesisOpen` | `ff4305d1fd7b0fa9fc6f10a9fa6711a9` |
| `loadingStart` | `fe0f1ff8289b03d0e10300aa08c14e39` |
| `loadingDestroy` | `fe0f1ff8289b03d0e10300aa08ad4e39` |
| `imageEnable` | `fe4fbfa9f30300aada0afe97e80313aa` |
| `imageDisable` | `fe4fbfa9f30300aa2d0bfe9768124439` |
| `loadingRender` | `883601b008d141f9087940f9a80000b4` |

## 验证范围

主机回归覆盖两服配置下的合成按钮创建、点击隔离、限流、界面重建和引用释放，以及加载图片连续传送、背景更换、比例设置与还原。配置测试覆盖未知 Build ID 不改变当前配置、国际服指令签名加载、切回国服时恢复地址、字段和签名。

上述是离线地址/指令核对与主机模拟测试，不代表已经完成国际服实机功能验证。合成入口仅发起游戏内请求，实际操作结果仍由游戏及服务端处理。
