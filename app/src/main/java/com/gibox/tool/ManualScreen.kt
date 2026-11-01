package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/** The old manual conversion: explicit MOD folder path, bone folder, character ID, model name and part mapping. */
@Composable
fun ManualScreen(state: AppState) {
    var source by remember { mutableStateOf("/storage/emulated/0/Download/Mod") }
    var bones by remember { mutableStateOf(state.boneRoot.path) }
    var id by remember { mutableStateOf("10000107") }
    var name by remember { mutableStateOf("我的模型") }
    var mapping by remember { mutableStateOf("") }
    var variants by remember { mutableStateOf(true) }
    Scaffold(
        topBar = {
            TopAppBar(
                colors = pageBarColors(),
                title = { Text("手动转换") },
                subtitle = { Text("旧流程") },
                navigationIcon = { BackButton { state.back() } },
            )
        },
    ) { padding ->
        Column(
            Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            OutlinedTextField(value = source, onValueChange = { source = it }, label = { Text("PC MOD 文件夹") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            OutlinedTextField(value = bones, onValueChange = { bones = it }, label = { Text("骨骼文件夹（含 skeleton.json）") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            OutlinedTextField(value = id, onValueChange = { id = it }, label = { Text("角色 ID") }, supportingText = { Text(Characters.byId(id.trim().toIntOrNull() ?: 0)?.name ?: "不在角色表里") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("新模型名") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            OutlinedTextField(value = mapping, onValueChange = { mapping = it }, label = { Text("部件=渲染器;…（可留空）") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("转换全部款式（最多 32 种组合）", modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
                Switch(checked = variants, onCheckedChange = { variants = it })
            }
            Button(
                onClick = {
                    val charId = id.trim().toIntOrNull() ?: 0
                    val model = name.trim()
                    if (charId !in 10000000..10001999 || model.isEmpty() || model.contains('/') || model == "." || model == "..") state.prompt = Prompt("错误", "角色 ID 或模型名无效")
                    else state.manualConvert(source.trim(), bones.trim(), charId, model, mapping.trim(), variants)
                },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("开始转换") }
        }
    }
}
