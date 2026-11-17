# One UI 界面

UI 基础使用 [tribalfs/oneui-design](https://github.com/tribalfs/oneui-design) 的 One UI 8 版本（`0.9.20+oneui8`）及其配套 SESL。`MainActivity` 使用 SESL `AppCompatActivity`，主页、功能搜索、辅助功能、设置和关于由 `OneUiPreferencesFragment : PreferenceFragmentCompat` 显示，标题栏使用 `ToolbarLayout`。设置开关、单选列表及其弹窗来自 SESL。

设置读取与写入仍交给 `AppState` / `Control`；所有 Preference 关闭自身持久化，避免另存一套不参与游戏控制的 SharedPreferences。操作失败时继续显示后端实际值。页面刷新保留滚动位置，切换游戏版本或离开页面时关闭旧的选择弹窗。

圆角与分组使用 SESL 的圆角绘制和 oneui-design 的 `InsetPreferenceCategory`。组间距为 20dp，页面两侧各留 12dp；组内分割线随文字位置缩进，组与组之间不画分割线。图标使用圆形彩色底和线条图案。深浅主题继承 `OneUITheme`，支持 One UI 的主题元数据。

主页依次显示模组管理、辅助功能、设置和关于，右上角可搜索功能。所有功能页面保留游戏版本选择。设置直接列出目录、日志和全部调试工具，没有额外的调试分类页面。关于页底部提供查看源代码按钮。

模组管理、详情、导入选择、日志和手动转换暂时保留现有 Compose 工作流，通过 `ComposeView` 与原生设置页面共用导航及后端；这些页面没有迁为 Preference。旧的 Compose 设置、调试和辅助功能列表已经移除。

## 构建依赖

依赖发布在 GitHub Packages，公开包也需要读取凭据。根据[上游说明](https://github.com/tribalfs/sesl-androidx#usage)，本地可在用户目录的 `~/.gradle/gradle.properties` 配置：

```properties
gpr.user=你的GitHub用户名
gpr.key=具有read:packages权限的token
```

也支持环境变量 `SESL_MAVEN_USER` / `SESL_MAVEN_TOKEN`，或 `GITHUB_ACTOR` / `GITHUB_TOKEN`（token 也可用 `GH_TOKEN`）。不要将凭据放进仓库。CI 优先使用同名 `SESL_MAVEN_USER` / `SESL_MAVEN_TOKEN` 仓库 Secrets，否则使用执行工作流的账号及工作流 token；若该 token 无法读取上游的跨仓库包，需要设置上述 Secrets。

按照上游要求排除了被 SESL 替换的官方 AndroidX / Material View 模块，避免重复类；Compose 自身的库继续保留。仅通过主机测试不能证明 SESL UI 可以编译或在设备正常显示，仍需在依赖可用的环境运行 `gradle :app:assembleDebug` 并实机检查。

SESL 8 的 `picker-app`、`picker-color` 和 `picker-basic` 共用 `androidx.picker` 资源命名空间。[AGP 9 默认要求依赖库命名空间唯一](https://developer.android.com/build/releases/agp-9-0-0-release-notes#behavior-changes)，因此项目使用 `android.uniquePackageNames=false` 保留此前的合并行为。这一配置只放宽命名空间校验，不替换 Manifest 节点，也不关闭重复类检查；待上游拆分命名空间后可移除。
