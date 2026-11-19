package com.gibox.tool

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Group

@Composable
internal fun ManualScreen(state: AppState) {
    var source by rememberSaveable { mutableStateOf("/storage/emulated/0/Download/Mod") }
    var bones by rememberSaveable { mutableStateOf(state.boneRoot.path) }
    var id by rememberSaveable { mutableStateOf("10000107") }
    var name by rememberSaveable { mutableStateOf("我的模型") }
    var mapping by rememberSaveable { mutableStateOf("") }
    var variants by rememberSaveable { mutableStateOf(true) }
    PreferencePage("手动转换", state::back, bottom = {
        BottomAction("开始转换") {
            val charId = id.trim().toIntOrNull() ?: 0
            val model = name.trim()
            if (charId !in 10000000..10001999 || model.isEmpty() || model.contains('/') || model == "." || model == "..")
                state.prompt = Prompt("错误", "角色 ID 或模型名无效")
            else state.manualConvert(source.trim(), bones.trim(), charId, model, mapping.trim(), variants)
        }
    }) {
        ClientRow(state, switchable = false)
        Group {
            EditableRow("PC MOD 文件夹", source) { source = it }
            Divider()
            EditableRow("骨骼文件夹（含 skeleton.json）", bones) { bones = it }
            Divider()
            EditableRow("角色 ID", id, Characters.byId(id.trim().toIntOrNull() ?: 0)?.name ?: "不在角色表里") { id = it }
            Divider()
            EditableRow("新模型名", name) { name = it }
            Divider()
            EditableRow("部件=渲染器;…（可留空）", mapping) { mapping = it }
            Divider()
            ToggleRow("转换全部款式（最多 32 种组合）", variants) { variants = it }
        }
    }
}
