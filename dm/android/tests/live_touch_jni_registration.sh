#!/usr/bin/env bash
# NativeActivity uses dlopen: exported JNI symbols alone are not registered.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source=${1:-"$here/../native/frontend_activity.c"}
table=$(awk '/JNINativeMethod methods\[\]=\{/{copy=1;next} copy && /^[[:space:]]*};/{exit} copy{print}' "$source")
for method in getLiveTouchGlyphSnapshot getLiveTouchAtlasInfo; do
    if ! printf '%s\n' "$table" | grep -Fq "{\"$method\",\"([I)I\",(void*)Java_org_d2r_android_port_D2RActivity_$method}"; then
        printf 'FAIL: NativeActivity does not register %s ([I)I\n' "$method" >&2
        exit 1
    fi
done
if ! printf '%s\n' "$table" | grep -Fq '{"setVirtualControllerEnabled","(Z)V",(void*)Java_org_d2r_android_port_D2RActivity_setVirtualControllerEnabled}'; then
    printf 'FAIL: NativeActivity does not register setVirtualControllerEnabled (Z)V\n' >&2
    exit 1
fi
printf 'PASS: NativeActivity registers skill glyph, atlas and virtual controller methods\n'
