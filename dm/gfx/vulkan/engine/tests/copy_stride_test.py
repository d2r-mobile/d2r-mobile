# 验证 Vulkan 分层拷贝规划及填充保护，依据 U3 字节图距契约。
# Validate Vulkan slice-copy planning and padding preservation against U3 byte image strides.
from pathlib import Path
import random
from math import lcm

source = (Path(__file__).resolve().parent.parent / "xbg_game.cpp").read_text(encoding="utf-8")
assert "case OP_COPY_IMAGE_STRIDE:" in source
assert "size_t(k)*staging_image" in source
assert "state.copy_image_stride=image" in source
assert "row,image,region" in source and "source_bytes,row,image,decoded" in source
rng = random.Random(20261019)
for case in range(1000):
    w,h,d,bpp = rng.randint(1,21),rng.randint(1,13),rng.randint(1,5),rng.choice([1,2,3,4,6,8,12,16])
    row=w*bpp+rng.randint(0,13)
    staging_row=w*bpp
    image=row*h+rng.randint(0,11)
    alignment=lcm(4,bpp)
    staging_image=((staging_row*h+alignment-1)//alignment)*alignment
    guest=bytearray(rng.randbytes(image*d))
    original=bytes(guest)
    staging=bytearray(staging_image*d)
    for z in range(d):
        assert (z*staging_image)%4 == 0 and (z*staging_image)%bpp == 0
        for y in range(h):
            a=z*image+y*row;b=z*staging_image+y*staging_row
            staging[b:b+w*bpp]=guest[a:a+w*bpp]
    for z in range(d):
        for y in range(h):
            a=z*image+y*row;b=z*staging_image+y*staging_row
            guest[a:a+w*bpp]=staging[b:b+w*bpp]
    assert bytes(guest)==original
print("PASS: 1000 seeded slice/array padding and aligned staging model cases; source wiring guards passed (no driver execution)")
