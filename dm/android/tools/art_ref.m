// 触屏控件参考图：直接用 iOS 的绘制代码（dm/ios/Sources/DmTouchControlArt.h）离屏画出与安卓 TouchOverlay.dumpArt 同名、同尺寸的 PNG，
// 给 art_compare.sh 逐像素比。几何与 iOS 的 controlArt 相同：点坐标 × 3（安卓测试机 480 dpi = 3 倍），y 轴向下（同 UIKit）。
// 底座图 / 剪影两步照 DmTouchOverlay.m 的 drawButton / skillImage。
// 用法：art_ref <dm_mobile_socket.png> <sil_dm_native_skill_0.png> <输出目录>
// Touch control reference images: the iOS drawing code (dm/ios/Sources/DmTouchControlArt.h) draws offscreen PNGs with the same names and sizes as Android's TouchOverlay.dumpArt,
// for art_compare.sh to compare pixel by pixel. Geometry as iOS controlArt: point coordinates × 3 (the Android test phone is 480 dpi = 3x), y axis pointing down (as UIKit).
// The socket image and silhouette steps follow drawButton / skillImage in DmTouchOverlay.m.
// Usage: art_ref <dm_mobile_socket.png> <sil_dm_native_skill_0.png> <output directory>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "../../ios/Sources/DmTouchControlArt.h"

static const CGFloat S = 3;
static CGImageRef load(const char *path) {
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(path)], NULL);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL; if (src) CFRelease(src); return img;
}
static void save(CGContextRef c, NSString *dir, NSString *name) {
    CGImageRef img = CGBitmapContextCreateImage(c);
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:[dir stringByAppendingPathComponent:[name stringByAppendingString:@".png"]]],
                                                              (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    CGImageDestinationAddImage(d, img, NULL); CGImageDestinationFinalize(d); CFRelease(d); CGImageRelease(img);
}
// 点尺寸 side 的画布：像素 = round(side × 3)，坐标按点、y 向下
// a canvas of side points: pixels = round(side × 3), coordinates in points, y pointing down
static CGContextRef canvas(CGFloat side, CGFloat *pt) {
    size_t px = (size_t)llround(side * S); CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(NULL, px, px, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast); CGColorSpaceRelease(cs);
    CGContextTranslateCTM(c, 0, px); CGContextScaleCTM(c, px / side, -(CGFloat)px / side); *pt = side; return c;
}
// UIImage drawInRect 在 y 向下的画布里不翻转图像：CGContextDrawImage 前把这一块再翻回来
// UIImage drawInRect does not flip the image on a canvas whose y axis points down: flip this area back before CGContextDrawImage
static void draw_image(CGContextRef c, CGImageRef img, CGRect r, CGBlendMode mode) {
    CGContextSaveGState(c); CGContextTranslateCTM(c, 0, CGRectGetMaxY(r) + CGRectGetMinY(r)); CGContextScaleCTM(c, 1, -1);
    CGContextSetBlendMode(c, mode); CGContextDrawImage(c, r, img); CGContextRestoreGState(c);
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "用法：art_ref <dm_mobile_socket.png> <sil_dm_native_skill_0.png> <输出目录>\n"); return 2; }
    CGImageRef socket = load(argv[1]), sil = load(argv[2]); NSString *dir = @(argv[3]);
    if (!socket || !sil) { fprintf(stderr, "读不了输入图\n"); return 1; }
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    CGFloat side;
    for (int h = 0; h < 2; h++) for (int e = 0; e < 2; e++) for (int p = 0; p < 2; p++) {
        CGContextRef c = canvas(80, &side); dm_button_draw(c, CGRectMake(0, 0, side, side), h, e, p);
        save(c, dir, [NSString stringWithFormat:@"button_h%de%dp%d", h, e, p]); CGContextRelease(c);
    }
    for (int h = 0; h < 2; h++) {   // drawButton 有底座图的分支 | the drawButton branch with the socket image
        CGContextRef c = canvas(80, &side); CGRect r = CGRectMake(0, 0, side, side);
        draw_image(c, socket, r, kCGBlendModeNormal); dm_socket_neutralize(c, r); if (h) dm_button_highlight(c, r);
        save(c, dir, [NSString stringWithFormat:@"socket_h%d", h]); CGContextRelease(c);
    }
    for (int a = 0; a < 2; a++) {
        CGContextRef c = canvas(120, &side); dm_joystick_base_draw(c, CGRectMake(0, 0, side, side), a);
        save(c, dir, [NSString stringWithFormat:@"joybase_a%d", a]); CGContextRelease(c);
        c = canvas(46, &side); dm_joystick_thumb_draw(c, CGRectMake(0, 0, side, side), a);
        save(c, dir, [NSString stringWithFormat:@"joythumb_a%d", a]); CGContextRelease(c);
    }
    {   // skillImage 的银灰化：原像素尺寸，画剪影 → 饱和度叠 50% 灰 → 用剪影透明度收边 | skillImage's silvering: at the original pixel size, draw the silhouette → saturation blend with 50% grey → trim the edges with the silhouette's alpha
        size_t w = CGImageGetWidth(sil), hh = CGImageGetHeight(sil); CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef c = CGBitmapContextCreate(NULL, w, hh, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast); CGColorSpaceRelease(cs);
        CGRect r = CGRectMake(0, 0, w, hh);
        CGContextDrawImage(c, r, sil); dm_skill_silver(c, r); CGContextSetBlendMode(c, kCGBlendModeDestinationIn); CGContextDrawImage(c, r, sil);
        save(c, dir, @"silver0"); CGContextRelease(c);
    }
    CGImageRelease(socket); CGImageRelease(sil); return 0;
}
