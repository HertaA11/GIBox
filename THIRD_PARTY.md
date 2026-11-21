# 第三方依赖

这些是通用开源依赖，来自官方源码或构建仓库。

包含 3DMigoto 衍生代码以后，GIBox 组合版本按 GPLv3 发布（见 `LICENSE`、`COPYING`）；发布时需随附完整对应源码和构建脚本，并保留第三方内容各自的许可和来源声明。

| 依赖 | 来源与固定版本 | 用途 / 交付形式 |
|---|---|---|
| 3DMigoto（XXMI 分支） | https://github.com/SpectrumQT/XXMI-Libs-Package · commit `a7847eae82aaac32175c9a7d199673d5c9323bcd`（原项目 https://github.com/bo3b/3Dmigoto） | INI 命令列表逻辑核心 `app/src/main/cpp/migoto/`：按上游语义移植的衍生代码，不含上游原文件。GPLv3。来源、对应的上游函数、文件哈希、修改记录、上游 `COPYING.txt` 和 `AUTHORS.txt` 在 `third_party/3dmigoto/`。 |
| GI-Model-Importer-Assets（PC hash 数据库） | https://github.com/SilentNightSound/GI-Model-Importer-Assets · commit `2039d16d4b64696098ba53cd69888ce967397be9`（2026-08-03）· `PlayerCharacterData/*/hash.json` | `app/src/main/assets/pc-hashes.json`：每个角色各部件在 PC 上的索引缓冲 hash、各段起点（match_first_index）和贴图 hash，由 `tools/make_pc_hashes.py` 从上游文件压缩而来（只删字段、不改数值），用于在安卓上认出纯贴图 MOD 写的是哪个部件、哪张贴图。GPLv3。 |
| YuanShenResources（游戏数据表） | https://gitlab.com/GuraFoundation/YuanShenResources · commit `f11cf545a7eee32a29999f960ebfba305f896ed3`（CNCBWin7.2.50，2026-11-04）· `ExcelBinOutput/AvatarCostumeExcelConfigData.json`、`TextMap/TextMap_MediumCHS.json`、`TextMap/TextMap_MediumEN.json` | `app/src/main/assets/outfits.json`：GIBox 自己的外观对照表，每个有皮肤的角色的原皮和各皮肤（皮肤 ID、游戏模型名、中英文名），由 `tools/make_outfits.py` 生成；只取编号和名称这类事实数据。与 PC 角色名的对应由 GIBox 核对：手机采集的 hash 表能定的按它，其余在脚本里逐条写明（JeanCN、XingqiuBamboo 的 PC 索引缓冲 hash 与原皮相同，不能按 hash 认）。 |
| Dobby | https://github.com/jmpews/Dobby · commit `0932d69c320e786672361ab53825ba8f4245e9d3` | Tick instrumentation；CMake 下载完整源码并编译静态链接。Apache-2.0，许可证 `third_party/Dobby-LICENSE`。官方头文件另存 `third_party/dobby-api/dobby.h` 供宿主语法检查。该版本使用内建 ARM64 assembler，避免原固定版本的 Mach-O 汇编与 Linux 平台接口编译错误。 |
| bcdec | https://github.com/iOrange/bcdec · commit `80859ed3b7afb1c527a2a99d70c61457bea72d0c` · v0.985 | BC 压缩块解码；完整单头文件包含在 `third_party/bcdec/bcdec.h`，许可证在文件末尾（MIT / Unlicense 可选）。DDS 容器和 JNI 是本工程编写。 |
| Xposed API | `de.robv.android.xposed:api:82` · https://api.xposed.info/ | 仅 compileOnly；正式运行由 LSPosed 提供。 |
| libxposed API / Service | `io.github.libxposed:api:102.0.0`（compileOnly）、`io.github.libxposed:service:102.0.0` · https://github.com/libxposed | 新接口入口 `ModernEntry`（`META-INF/xposed`）和 App 里读取 LSPosed 作用域的服务。Apache-2.0。由 Gradle 下载。 |
| Jetpack Compose / Activity | `androidx.activity:activity-compose:1.13.0` 及 COUI-Kit 的 Compose 依赖（Google Maven） | Compose 页面宿主与基础布局。Apache-2.0。由 Gradle 下载。 |
| COUI-Kit | https://github.com/ItosEO/COUI-Kit · `io.github.itoseo:coui:0.0.1-alpha11`、`io.github.itoseo:coui-icons:0.0.1-alpha11`（Maven Central） | App 控件及图标库。库自己的代码为 MPL-2.0，所含第三方内容见[上游 NOTICE](https://github.com/ItosEO/COUI-Kit/blob/main/NOTICE)。由 Gradle 下载。 |
| suqi8/coui | https://github.com/suqi8/coui · commit `e4e915ca67ea0f2689c746822dcc5fda1f84e817` | 运行日志使用 `Refresh`、`Delete`、`Share` 的 Regular 矢量路径，转换为 Android drawable。底栏「关于」使用 df5883f 分支快照中的 Regular.Info。Apache-2.0，许可证见 `third_party/suqi8-coui-LICENSE`。 |
| Kotlin | Kotlin 2.4.20 与 Compose 编译器插件 | App 界面用 Kotlin 写；转换器、导入、游戏端仍是 Java / C++。 |
| Android SDK / NDK / Gradle / AGP | 工程与 README 中的指定版本 | 标准构建工具与系统 API，不包含在源码 ZIP 中。 |

