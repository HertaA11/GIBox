# 游戏内工具（1.0.16，本地待实测）

设置 → 通用 → 穿行摇杆，默认关闭。进入游戏后右侧出现「穿行」；点击展开，拖动标题移动。穿行摇杆按相机水平朝向移动，上升/下降须按住，速度范围 1–20。折叠面板、Activity 暂停、关闭功能时停止输入并在游戏 Tick 中恢复物理状态。输入超过 300ms 未续期或实体位置大幅偏离目标时暂停当前开启轮次，须关闭再开启。

App 设置 → 通用另有独立「打开合成台」按钮，不依赖穿行摇杆开关。先进入游戏大世界，再切回 App 点击按钮并返回游戏；控制文件计数变化在游戏 Tick 中转为一次请求，游戏重启不重放旧请求。

## 地址依据

仅国服 7.2 Build ID `6d85e3141bcd6b508f3e853bab508cc407278eac`。来自用户提供的 dump 和同版本 libyuanshen.so，国际服无对应表项时拒绝调用。

- `OADFBEEGOFD.EJNIGFFNPCC(string,string,Action,GCGEJOPPGAE,BaseInterAction)`，RVA `0xfdba624`：常规交互分发。`Synthesis` 分支在 `0xfdbed80`；`0xfdbedbc` 调用合成控制器 `PHOPLACCBHL` 构造器 `0x106e37f0`。实体参数为空的路径在 `0xfdc2454`，界面实体 ID 写 0。以 `Synthesis`、空第二字符串和空回调/实体/交互发起请求；bool 返回只表示入口接收情况，不表示合成成功。
- `GCGEJOPPGAE.HJNNCKFODKI()`，`0x9551d6c`：返回实体物理根 Transform（字段 `+0x1f0`）。
- `GCGEJOPPGAE.HDDAEHAKDIF()`，`0x954eccc`：物理根的 Unity 坐标。
- `GCGEJOPPGAE.NBDINMJANEN(Vector3,bool)`，`0x9539088`：经 `0x9539234` 更新 Transform 与实体位置缓存。输入为 Unity 坐标，方法内部补偿世界坐标偏移，不手动改缓存字段。
- Unity 位置、朝向、Camera、Collider 和 Rigidbody 接口使用 dump 对应方法。启用前要求物理根坐标与实体坐标一致，找到同根 Rigidbody、Collider 且 attachedRigidbody 对应；不满足即暂停。

## 验证范围

宿主假引擎测试验证调用签名、参数、移动计算、禁用/输入超时、物理状态恢复与失败重试、位置回写中止、未适配地址拒绝。Java 宿主检查仅验证 API 签名和编译。

尚未完成真机验证：实体根上组件的实际分布、游戏状态机是否回写位置、交互入口在脱离合成台时是否打开页面，以及服务端是否允许合成。组件条件不满足会显示具体原因；不扩大搜索到未知物理对象，不声称已经能在服务端完成合成。
