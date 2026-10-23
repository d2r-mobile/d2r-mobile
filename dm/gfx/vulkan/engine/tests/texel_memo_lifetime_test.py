"""Execute production memo eligibility in a CPU slot-retirement model.

Run with --compiler <NDK clang++.exe> [--node node]. WASM is used only as a
portable CPU runner; this test does not create Vulkan objects or use a device.
It also tests the previous guard as a negative control: stale-view reuse must
be detected, while sampled-image and ordinary-UBO memo reuse stays enabled.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from texture_cache_regression import balanced


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    source = (pathlib.Path(__file__).resolve().parent.parent / 'xbg_game.cpp').read_text(encoding='utf-8')
    start = re.search(r'^\s*if\(b.type==VK_DESCRIPTOR_TYPE_(?:UNIFORM|STORAGE)_TEXEL_BUFFER', source, re.MULTILINE).start()
    guard = balanced(source, start)
    prefix = r'''
enum {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER=1,
      VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER=4,
      VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER=5,
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6};
constexpr bool texel_memo_enabled=false;
bool eligible(int type){struct {int type;} b{type};bool ok=true;unsigned desc_memo_why[6]={};do {
'''
    suffix = r'''
}while(false);return ok;}
struct View {bool alive=true;};
struct Memo {View *view=nullptr;bool ready=false;};
// Non-texel resources are retained; slot retirement destroys only its texel view.
int scenario(int type){
    const bool texel=type==4 || type==5;
    View first,second;Memo memo;
    int hits=0,builds=0;
    auto draw=[&](View &fresh){
        View *selected;
        if(eligible(type) && memo.ready){selected=memo.view;++hits;}
        else{selected=&fresh;++builds;if(eligible(type)){memo.view=selected;memo.ready=true;}}
        return selected->alive;
    };
    if(!draw(first))return 1;
    if(texel)first.alive=false; // retire_slot: vkDestroyBufferView
    if(!draw(second))return 2; // a cached descriptor must not select the retired view
    if(texel && (hits!=0 || builds!=2))return 3;
    if(!texel && (hits!=1 || builds!=1))return 4;
    return 0;
}
extern "C" int run_tests(){
    const int types[]={4,5,1,6};
    for(int type:types){int result=scenario(type);if(result)return 100*type+result;}
    return 0;
}
'''
    old_guard = 'if(b.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){ok=false;++desc_memo_why[0];break;}'
    with tempfile.TemporaryDirectory(prefix='xbg-texel-lifetime-') as temp:
        for name, actual_guard, expected in [('production', guard, 0), ('old_guard', old_guard, 402)]:
            cpp, wasm = pathlib.Path(temp) / (name + '.cpp'), pathlib.Path(temp) / (name + '.wasm')
            cpp.write_text(prefix + actual_guard + suffix, encoding='utf-8')
            subprocess.run([args.compiler, '--target=wasm32', '-std=c++17', '-ffreestanding',
                            '-fno-exceptions', '-fno-rtti', '-nostdlib', '-Wl,--no-entry',
                            '-Wl,--export=run_tests', str(cpp), '-o', str(wasm)], check=True)
            js = ('const fs=require("fs");WebAssembly.instantiate(fs.readFileSync(process.argv[1]))'
                  '.then(({instance})=>{let actual=instance.exports.run_tests();'
                  'let expected=Number(process.argv[2]);if(actual!==expected)'
                  'throw Error("unexpected CPU result "+actual+", expected "+expected);'
                  'console.log(process.argv[3]+": "+actual);});')
            subprocess.run([args.node, '-e', js, str(wasm), str(expected), name], check=True)
    print('PASS: type4/5 rebuild after slot retirement; sampled-image/UBO memo survives; old guard detects stale type4 view. No Vulkan executed.')


if __name__ == '__main__':
    main()
