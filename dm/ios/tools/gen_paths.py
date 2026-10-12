"""构建时从游戏文件生成的资源放哪（仓库里不放原版衍生的图和字）：环境变量 DM_GEN_ASSETS，缺省 dm/ios/Gen/assets（Gen/ 不进 git）。
本模块不 import 别的工具模块（生成脚本与读取方互相 import，放这里免得循环）。
Where resources generated at build time from the game files go (the repo holds no images or text derived from the original game): environment variable DM_GEN_ASSETS, default dm/ios/Gen/assets (Gen/ is not in git).
This module does not import other tool modules (generator scripts and readers import each other; putting it here avoids cycles)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def gen_dir(*sub):
    root = os.environ.get('DM_GEN_ASSETS') or os.path.join(HERE, '..', 'Gen', 'assets')
    p = os.path.normpath(os.path.join(root, *sub)); os.makedirs(p, exist_ok=True); return p
