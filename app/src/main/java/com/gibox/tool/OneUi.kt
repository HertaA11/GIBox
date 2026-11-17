package com.gibox.tool

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

internal val LocalServerState = staticCompositionLocalOf<AppState?> { null }
data class OneUiRowShape(val index: Int = 0, val count: Int = 1)
object OneUiList {
    val SegmentedGap = 0.dp
    fun segmentedShapes(index: Int, count: Int) = OneUiRowShape(index, count)
}
enum class OneGlyph { Parcel, Grid, Wrench, Info, Folder, Search, Arrow, Refresh, Person, Share, Pencil, Trash, Check }
@Composable
fun OneIcon(glyph: OneGlyph, modifier: Modifier = Modifier, badge: Boolean = false) {
    val color = when(glyph) { OneGlyph.Parcel -> Color(0xFF387AFF); OneGlyph.Grid, OneGlyph.Check -> Color(0xFF35A778); OneGlyph.Wrench -> Color(0xFF8264CF); else -> Color(0xFF6E86A7) }
    val ink = if(badge) Color.White else LocalContentColor.current
    Box(modifier.then(if(badge) Modifier.size(38.dp).background(color, androidx.compose.foundation.shape.CircleShape).padding(8.dp) else Modifier.size(24.dp))) {
        Canvas(Modifier.fillMaxSize()) {
            val scale = size.width / 24f
            fun line(x: Float,y: Float,a: Float,b: Float) = drawLine(ink, androidx.compose.ui.geometry.Offset(x*scale,y*scale), androidx.compose.ui.geometry.Offset(a*scale,b*scale),1.7f*scale,StrokeCap.Round)
            fun path(vararg points: Float) { for(i in 0 until points.size-2 step 2) line(points[i],points[i+1],points[i+2],points[i+3]) }
            when(glyph) {
                OneGlyph.Parcel -> { path(3f,7f,12f,3f,21f,7f,21f,18f,12f,22f,3f,18f,3f,7f,12f,11f,21f,7f);line(12f,11f,12f,22f);line(7f,5f,16f,9f) }
                OneGlyph.Grid -> for(x in listOf(3f,14f)) for(y in listOf(3f,14f)) drawRoundRect(ink, androidx.compose.ui.geometry.Offset(x*scale,y*scale),androidx.compose.ui.geometry.Size(7f*scale,7f*scale),androidx.compose.ui.geometry.CornerRadius(scale))
                OneGlyph.Wrench -> { path(14f,3f,12f,7f,16f,11f,20f,9f);path(14f,3f,10f,4f,8f,8f,9f,12f,3f,18f,6f,21f,12f,15f,16f,16f,20f,13f,20f,9f) }
                OneGlyph.Info -> { drawCircle(ink,10*scale,style=Stroke(1.7f*scale));line(12f,11f,12f,17f);drawCircle(ink,scale,androidx.compose.ui.geometry.Offset(12*scale,7*scale)) }
                OneGlyph.Search -> { drawCircle(ink,7*scale,androidx.compose.ui.geometry.Offset(10*scale,10*scale),style=Stroke(1.8f*scale));line(15f,15f,22f,22f) }
                OneGlyph.Arrow -> path(9f,5f,16f,12f,9f,19f)
                OneGlyph.Folder -> path(2f,6f,9f,6f,11f,9f,22f,9f,22f,20f,2f,20f,2f,6f)
                OneGlyph.Refresh -> { drawArc(ink,30f,290f,false,style=Stroke(1.8f*scale));path(20f,3f,20f,9f,14f,9f) }
                OneGlyph.Person -> { drawCircle(ink,4*scale,androidx.compose.ui.geometry.Offset(12*scale,6*scale),style=Stroke(1.7f*scale));path(3f,22f,4f,17f,8f,14f,16f,14f,20f,17f,21f,22f) }
                OneGlyph.Check -> path(4f,12f,10f,18f,21f,5f)
                OneGlyph.Pencil -> { path(4f,16f,16f,4f,20f,8f,8f,20f,3f,21f,4f,16f);line(13f,7f,17f,11f) }
                OneGlyph.Trash -> {path(4f,6f,20f,6f);path(8f,6f,8f,3f,16f,3f,16f,6f);path(6f,6f,7f,21f,17f,21f,18f,6f);line(10f,10f,10f,17f);line(14f,10f,14f,17f)}
                OneGlyph.Share -> { path(5f,9f,5f,21f,20f,21f,20f,9f);path(8f,6f,12f,2f,16f,6f);line(12f,2f,12f,15f) }
            }
        }
    }
}
@Composable
fun OneUiSwitch(checked: Boolean, onCheckedChange: ((Boolean)->Unit)?, modifier: Modifier = Modifier, enabled: Boolean = true) {
    val progress by animateFloatAsState(if(checked) 1f else 0f, label="switch")
    val color by animateColorAsState(if(checked) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline, label="track")
    Box(modifier.size(52.dp,48.dp).then(if(onCheckedChange!=null) Modifier.toggleable(value=checked,enabled=enabled,role=Role.Switch,onValueChange=onCheckedChange) else Modifier),contentAlignment=Alignment.Center) {
        Canvas(Modifier.size(46.dp,28.dp)) {
            drawRoundRect(color.copy(alpha=if(enabled) 1f else .35f),cornerRadius=androidx.compose.ui.geometry.CornerRadius(size.height/2))
            drawCircle(Color.White, size.height*.37f, androidx.compose.ui.geometry.Offset(size.height/2+progress*(size.width-size.height),size.height/2))
        }
    }
}
@Composable
fun OneUiRow(modifier: Modifier = Modifier, shapes: OneUiRowShape = OneUiRowShape(), onClick: (()->Unit)? = null, selected: Boolean = false, leadingContent: (@Composable ()->Unit)? = null, trailingContent: (@Composable ()->Unit)? = null, supportingContent: (@Composable ()->Unit)? = null, content: @Composable ()->Unit) {
    val shape = RoundedCornerShape(topStart=if(shapes.index==0) 24.dp else 0.dp,topEnd=if(shapes.index==0) 24.dp else 0.dp,bottomStart=if(shapes.index==shapes.count-1) 24.dp else 0.dp,bottomEnd=if(shapes.index==shapes.count-1) 24.dp else 0.dp)
    Column(modifier.fillMaxWidth().clip(shape).background(if(selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface).then(if(onClick!=null) Modifier.clickable(onClick=onClick) else Modifier)) {
        Row(Modifier.heightIn(min=68.dp).padding(horizontal=20.dp,vertical=13.dp),verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(16.dp)) {
            leadingContent?.invoke()
            Column(Modifier.weight(1f),verticalArrangement=Arrangement.spacedBy(4.dp)) {
                ProvideTextStyle(MaterialTheme.typography.bodyLarge) { content() }
                if(supportingContent!=null) CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurfaceVariant) { ProvideTextStyle(MaterialTheme.typography.bodySmall) { supportingContent() } }
            }
            trailingContent?.invoke()
        }
        if(shapes.index < shapes.count-1) HorizontalDivider(Modifier.padding(start=20.dp,end=20.dp),color=MaterialTheme.colorScheme.outlineVariant,thickness=.5.dp)
    }
}
@Composable
fun SettingsGroup(title: @Composable ()->Unit = {}, verticalArrangement: Arrangement.Vertical = Arrangement.spacedBy(0.dp), content: @Composable ColumnScope.()->Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical=8.dp)) {
        Box(Modifier.padding(start=20.dp,bottom=8.dp,top=8.dp)) { title() }
        Column(verticalArrangement=verticalArrangement,content=content)
    }
}
@Composable
fun SettingsMenuLink(title: @Composable ()->Unit, subtitle: (@Composable ()->Unit)?=null, icon: (@Composable ()->Unit)?=null, shapes: OneUiRowShape=OneUiRowShape(), onClick: ()->Unit) {
    OneUiRow(shapes=shapes,onClick=onClick,leadingContent=icon,trailingContent={ OneIcon(OneGlyph.Arrow,Modifier.size(16.dp)) },supportingContent=subtitle,content=title)
}
@Composable
fun SettingsSwitch(state: Boolean, title: @Composable ()->Unit, subtitle: (@Composable ()->Unit)?=null, enabled: Boolean=true, shapes: OneUiRowShape=OneUiRowShape(), onCheckedChange: (Boolean)->Unit) {
    OneUiRow(modifier=Modifier.toggleable(value=state,enabled=enabled,role=Role.Switch,onValueChange=onCheckedChange),shapes=shapes,trailingContent={OneUiSwitch(state,null,enabled=enabled)},supportingContent=subtitle,content=title)
}
@Composable
fun OneUiChoice(selected: Boolean, onClick: ()->Unit, label: @Composable ()->Unit, modifier: Modifier=Modifier, enabled: Boolean=true) {
    Surface(modifier=modifier.heightIn(min=40.dp).clip(RoundedCornerShape(12.dp)).semantics { this.selected=selected }.clickable(enabled=enabled,role=Role.Button,onClick=onClick),color=if(selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHigh,contentColor=if(selected) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface,shape=RoundedCornerShape(12.dp)) {
        Box(Modifier.padding(horizontal=16.dp,vertical=10.dp),contentAlignment=Alignment.Center) { ProvideTextStyle(MaterialTheme.typography.labelLarge){label()} }
    }
}
@Composable
fun OneUiActionChip(onClick: ()->Unit, label: @Composable ()->Unit) = OneUiChoice(false,onClick,label)
@Composable
fun OneUiButton(onClick: ()->Unit, modifier: Modifier=Modifier, enabled: Boolean=true, contentPadding: PaddingValues=PaddingValues(horizontal=24.dp,vertical=12.dp), content: @Composable RowScope.()->Unit) {
    Button(onClick,modifier,enabled,shape=RoundedCornerShape(14.dp),contentPadding=contentPadding,content=content)
}
@Composable
fun OneUiOutlineButton(onClick: ()->Unit, modifier: Modifier=Modifier, enabled: Boolean=true, content: @Composable RowScope.()->Unit) {
    OutlinedButton(onClick,modifier,enabled,shape=RoundedCornerShape(14.dp),content=content)
}
@Composable
fun OneUiTextField(value: String,onValueChange:(String)->Unit,modifier:Modifier=Modifier,singleLine:Boolean=false,label:(@Composable ()->Unit)?=null,placeholder:(@Composable ()->Unit)?=null,leadingIcon:(@Composable ()->Unit)?=null,trailingIcon:(@Composable ()->Unit)?=null,supportingText:(@Composable ()->Unit)?=null,shape:Shape=RoundedCornerShape(14.dp)) {
    Column(modifier) {
        if(label!=null) Box(Modifier.padding(start=4.dp,bottom=6.dp)) { ProvideTextStyle(MaterialTheme.typography.bodySmall){label()} }
        BasicTextField(value,onValueChange,Modifier.fillMaxWidth().clip(shape).background(MaterialTheme.colorScheme.surfaceContainerHigh).padding(16.dp),singleLine=singleLine,textStyle=MaterialTheme.typography.bodyLarge.copy(color=MaterialTheme.colorScheme.onSurface),cursorBrush=SolidColor(MaterialTheme.colorScheme.primary),decorationBox={ input ->
            Row(verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(12.dp)) {
                leadingIcon?.invoke()
                Box(Modifier.weight(1f)) { if(value.isEmpty()&&placeholder!=null) CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurfaceVariant){placeholder()};input() }
                trailingIcon?.invoke()
            }
        })
        if(supportingText!=null) Box(Modifier.padding(start=4.dp,top=6.dp)){ProvideTextStyle(MaterialTheme.typography.bodySmall){supportingText()}}
    }
}
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OneUiLargeTopBar(title:@Composable ()->Unit,subtitle:(@Composable ()->Unit)?=null,navigationIcon:@Composable ()->Unit={},actions:@Composable RowScope.()->Unit={},colors:TopAppBarColors=TopAppBarDefaults.topAppBarColors(),scrollBehavior:TopAppBarScrollBehavior?=null) {
    LargeTopAppBar(title={Column {ProvideTextStyle(MaterialTheme.typography.headlineLarge){title()};if(subtitle!=null) ProvideTextStyle(MaterialTheme.typography.bodySmall){subtitle()}}},navigationIcon=navigationIcon,actions=actions,colors=colors,scrollBehavior=scrollBehavior,windowInsets=WindowInsets(0,0,0,0))
}
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OneUiTopBar(title:@Composable ()->Unit,subtitle:(@Composable ()->Unit)?=null,navigationIcon:@Composable ()->Unit={},actions:@Composable RowScope.()->Unit={},colors:TopAppBarColors=TopAppBarDefaults.topAppBarColors()) {
    TopAppBar(title={Column {title();if(subtitle!=null) ProvideTextStyle(MaterialTheme.typography.bodySmall){subtitle()}}},navigationIcon=navigationIcon,actions=actions,colors=colors,windowInsets=WindowInsets(0,0,0,0))
}
@Composable
fun OneUiScaffold(modifier:Modifier=Modifier,topBar:@Composable ()->Unit={},bottomBar:@Composable ()->Unit={},content:@Composable (PaddingValues)->Unit) {
    val state=LocalServerState.current
    Scaffold(modifier,topBar={Column(Modifier.statusBarsPadding()){topBar();if(state!=null) ServerSelector(state)}},bottomBar=bottomBar,content=content)
}
@Composable
fun ServerSelector(state:AppState) {
    Row(Modifier.fillMaxWidth().horizontalScroll(androidx.compose.foundation.rememberScrollState()).padding(horizontal=20.dp,vertical=8.dp),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
        for(pkg in state.clientChoices()) OneUiChoice(state.game==pkg,{state.chooseClient(pkg)},{Text(when(pkg){"com.miHoYo.Yuanshen"->"国服";"com.miHoYo.ys.bilibili"->"B服";else->"国际服"})})
    }
}

@Composable
fun OneUiRadio(selected:Boolean,onClick:(()->Unit)?,modifier:Modifier=Modifier) {
    val color=if(selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline
    Box(modifier.size(40.dp).semantics { this.selected=selected }.then(if(onClick!=null) Modifier.clickable(onClick=onClick) else Modifier),contentAlignment=Alignment.Center) {
        Canvas(Modifier.size(22.dp)) {drawCircle(color,style=Stroke(2.dp.toPx()));if(selected)drawCircle(color,6.dp.toPx())}
    }
}
@Composable
fun OneUiDialog(onDismissRequest:()->Unit,confirmButton:@Composable ()->Unit,title:(@Composable ()->Unit)?=null,text:(@Composable ()->Unit)?=null,dismissButton:(@Composable ()->Unit)?=null,icon:(@Composable ()->Unit)?=null,properties:androidx.compose.ui.window.DialogProperties=androidx.compose.ui.window.DialogProperties()) {
    AlertDialog(onDismissRequest=onDismissRequest,confirmButton=confirmButton,dismissButton=dismissButton,title=title,text=text,icon=icon,properties=properties,shape=RoundedCornerShape(28.dp),containerColor=MaterialTheme.colorScheme.surface)
}

@Composable
fun OneUiTonalIconButton(onClick:()->Unit,modifier:Modifier=Modifier,enabled:Boolean=true,content:@Composable ()->Unit) {
    IconButton(onClick=onClick,modifier=modifier.clip(RoundedCornerShape(14.dp)).background(MaterialTheme.colorScheme.primaryContainer),enabled=enabled,content=content)
}
@Composable
fun OneUiTabs(selectedTabIndex:Int,edgePadding:androidx.compose.ui.unit.Dp=0.dp,modifier:Modifier=Modifier,containerColor:Color=Color.Transparent,divider:@Composable ()->Unit={},content:@Composable ()->Unit) {
    Row(modifier.clip(RoundedCornerShape(16.dp)).background(MaterialTheme.colorScheme.surfaceContainerHigh).padding(4.dp).horizontalScroll(androidx.compose.foundation.rememberScrollState()),horizontalArrangement=Arrangement.spacedBy(4.dp)){content()}
}
@Composable
fun OneUiTab(selected:Boolean,onClick:()->Unit,text:@Composable ()->Unit) = OneUiChoice(selected,onClick,text)

@Composable
fun OneUiMenu(expanded:Boolean,onDismissRequest:()->Unit,modifier:Modifier=Modifier,content:@Composable ColumnScope.()->Unit) {
    DropdownMenu(expanded=expanded,onDismissRequest=onDismissRequest,modifier=modifier,shape=RoundedCornerShape(18.dp),containerColor=MaterialTheme.colorScheme.surface,content=content)
}
