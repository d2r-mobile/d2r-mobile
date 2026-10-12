# iOS 着色器出包链的唯一编译口径：任意来源的 MSL（PC 链 / 常数补丁 / 今后 Switch 原版经 Ryujinx 翻译）→ iphoneos metallib。
# 编译参数只在这里定义一次；反射 json 的 chain.metal 记录由同一份参数生成，记录 == 实际。
# The single compile standard for the iOS shader packaging chain: MSL from any source (PC chain / constant patches / in future, the Switch original translated via Ryujinx) → iphoneos metallib.
# Compile parameters are defined only once, here; the chain.metal record in the reflection json is generated from the same parameters, record == actual.
import subprocess
METAL = ['xcrun', '-sdk', 'iphoneos', 'metal', '-c', '-mios-version-min=18.0']   # 与既有 iOS 着色器缓存一致 | Consistent with the existing iOS shader cache
METALLIB = ['xcrun', '-sdk', 'iphoneos', 'metallib']
RECORD = ' '.join(METAL) + '; ' + ' '.join(METALLIB)                                # 写进 json chain.metal | Written into json chain.metal

def compile(msl, air, lib):
    subprocess.run([*METAL, msl, '-o', air], check=True)
    subprocess.run([*METALLIB, air, '-o', lib], check=True)
