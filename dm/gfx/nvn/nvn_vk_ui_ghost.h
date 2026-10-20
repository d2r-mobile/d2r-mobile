/* Screen-space recognition of the original game's 20-byte UI quads.
 * MIT-licensed Android compatibility implementation.
 * (MIT); adapted to this tree's Vulkan replay state.
 * Coordinates follow the Vulkan viewport actually used by the replay. */
#ifndef NVN_VK_UI_GHOST_H
#define NVN_VK_UI_GHOST_H
#include <math.h>
#include <stdint.h>
#include <string.h>
static inline int nvn_vk_ui_ghost_rect(const void *vertices,const float m[16],
        const float vp[4],float width,float height,int upper_left,float out[4]) {
    if(width<=0||height<=0||vp[2]<=0||vp[3]<=0)return 0;
    out[0]=out[1]=INFINITY;out[2]=out[3]=-INFINITY;
    for(unsigned i=0;i<4;i++){
        float p[2];memcpy(p,(const uint8_t *)vertices+i*20,8);
        float x=(m[0]*p[0]+m[4]*p[1]+m[12]+1)*.5f;
        float y=(m[1]*p[0]+m[5]*p[1]+m[13]+1)*.5f;
        /* The game's 2-D vertex program negates projection Y before emitting
         * Position. The upper-left Vulkan viewport reverses it once more. */
        if(!upper_left)y=1-y;
        x=(vp[0]+x*vp[2])/width;y=(vp[1]+y*vp[3])/height;
        if(!isfinite(x)||!isfinite(y))return 0;
        out[0]=fminf(out[0],x);out[1]=fminf(out[1],y);
        out[2]=fmaxf(out[2],x);out[3]=fmaxf(out[3],y);
    }
    return out[2]>out[0]&&out[3]>out[1];
}
static inline int nvn_vk_ui_ghost_matches(const float r[4],const float grid[4],float x,float y){
    if(grid[2]<=0||grid[3]<=0||x<r[0]||x>r[2]||y<r[1]||y>r[3])return 0;
    float c=(r[0]-grid[0])/grid[2],row=(r[1]-grid[1])/grid[3];
    float w=(r[2]-r[0])/grid[2],h=(r[3]-r[1])/grid[3];
    return isfinite(c)&&isfinite(row)&&isfinite(w)&&isfinite(h)&&
        fabsf(c-roundf(c))<=.15f&&fabsf(row-roundf(row))<=.15f&&
        fabsf(w-roundf(w))<=.15f&&fabsf(h-roundf(h))<=.15f&&
        w>=.85f&&w<=2.15f&&h>=.85f&&h<=4.15f;
}
#endif
