"""Host validation. This does not claim an Android build or game-device test."""
from pathlib import Path
import os,shutil,subprocess
import host_stubs
root=Path(__file__).resolve().parents[1]
out=root/'build'/'host'
out.mkdir(parents=True,exist_ok=True)
def run(cmd):
    subprocess.run([str(x) for x in cmd],cwd=root,check=True)
flags=['-std=c++17','-Wall','-Wextra','-Werror','-Iapp/src/main/cpp','-Ithird_party/bcdec']
run(['g++',*flags,'tests/core_test.cpp','app/src/main/cpp/runtime.cpp','app/src/main/cpp/dds.cpp','-o',out/'core_test'])
run([out/'core_test'])
run(['g++',*flags,'-pthread','tests/glow_test.cpp','-o',out/'glow_test'])
run(['g++',*flags,'tests/texture_diag_test.cpp','-o',out/'texture_diag_test'])
run([out/'texture_diag_test'])
run(['g++',*flags,'tests/vk_capture_test.cpp','-o',out/'vk_capture_test'])
run([out/'vk_capture_test'])
run(['g++',*flags,'-Ithird_party/dobby-api','tests/vk_hook_test.cpp','-o',out/'vk_hook_test'])
run([out/'vk_hook_test'])
run(['g++',*flags,'tests/migoto_test.cpp','-o',out/'migoto_test'])
run([out/'migoto_test'])
run(['g++',*flags,'tests/migoto_parity.cpp','-o',out/'migoto_parity'])
run([out/'glow_test'])
if (root/'tests/runtime_test.cpp').exists():
    run(['g++',*flags,'-pthread','tests/runtime_test.cpp','app/src/main/cpp/runtime.cpp','-o',out/'runtime_test'])
    run([out/'runtime_test'])
# A module targeting Xposed API 102 cannot load de.robv.android.xposed classes: whatever the modern entry reaches
# (ModernEntry -> Loader -> Control, NativeBridge) must not refer to them, or loading it fails (0.7.66 / 0.7.67).
for name in ['ModernEntry','Loader','Control','NativeBridge','ModuleStatus']:
    code=''.join(l for l in (root/'app/src/main/java/com/gibox/tool'/(name+'.java')).read_text(encoding='utf-8').splitlines(True) if not l.lstrip().startswith(('*','/*','//')))
    assert 'de.robv' not in code and 'Entry.' not in code.replace('ModernEntry',''), name+' refers to the legacy Xposed API or to Entry'
print('Modern entry classes: PASS (no legacy Xposed API reachable)')
java=shutil.which('java')
if not java:raise RuntimeError('JDK 17 is required')
stubsrc=out/'stub-src';stubclasses=out/'stub-classes';classes=out/'classes'
for path in [stubsrc,stubclasses,classes]:
    if path.exists():shutil.rmtree(path)
    path.mkdir(parents=True)
stubs=host_stubs.write(stubsrc)
run([java,'com.sun.tools.javac.Main','--release','8','-d',stubclasses,*stubs])
sources=sorted((root/'app/src/main/java').rglob('*.java'))
run([java,'com.sun.tools.javac.Main','--release','8','-cp',stubclasses,'-d',classes,*sources,*sorted((root/'tests').glob('*.java'))])
run([java,'-cp',classes,'com.gibox.tool.ConverterTest',out/'core_test'])
run([java,'-cp',classes,'com.gibox.tool.DocumentPathsTest'])
patterns=out/'gimi-patterns'
if patterns.exists():shutil.rmtree(patterns)
patterns.mkdir(parents=True)
run([java,'-cp',classes,'com.gibox.tool.GimiPatternsTest',patterns])
run([java,'-cp',classes,'com.gibox.tool.CompatibilityTest',out/'core_test'])
# The 3DMigoto logic core next to the old converter and FrameLoop: the committed fixture always; a real MOD
# (not in the repository) when GIBOX_PARITY_MOD=<folder>::<main ini> is set.
parity=[(root/'tests/fixtures/present-loop','Anim.ini',''),(root/'tests/fixtures/logic-signature','Hero.ini','driver-only')]
if os.environ.get('GIBOX_PARITY_MOD'):folder,_,main=os.environ['GIBOX_PARITY_MOD'].partition('::');parity.append((Path(folder),main,''))
for folder,main,mode in parity:
    dump=out/('legacy-'+folder.name+'.txt')
    logic=out/('logic-'+folder.name)
    if logic.exists():shutil.rmtree(logic)
    run([java,'-cp',classes,'com.gibox.tool.LegacyAnimationDump',folder,main,dump,logic])
    run([out/'migoto_parity',folder,main,dump,'300',logic,*([mode] if mode else [])])
importer=out/'importer'
if importer.exists():shutil.rmtree(importer)
importer.mkdir(parents=True)
run([java,'-cp',os.pathsep.join([str(classes),str(stubclasses)]),'com.gibox.tool.ImporterTest',importer,root/'tests/avatar_ids.txt'])
print('Java app source: PASS (host API-signature compilation; Android SDK build still required)')
run(['python3',root/'tools/test_handoff.py'])
