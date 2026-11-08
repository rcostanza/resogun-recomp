/* The game's version, logged first so a bug report's log tells which dump it came from: param.sfo's
 * TITLE_ID and APP_VER, and the eboot's SHA-256 that scripts/prepare.py records in analysis.json
 * beside the image. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include "gpu/resogun_gpu.h"

/* eboot.bin builds the runner has been run with. */
static const struct { const char *sha256, *version; } known_builds[]={
    {"c8de3071d96b57a4ccc9b94abaadbe68a133933b34f7396d4a00fb1c6c5a5612","01.10"},
};

/* "source_sha256" from analysis.json in the folder of `image_path`. */
static int eboot_sha256(const char *image_path, char *out) {
    char path[4096];
    const char *slash=strrchr(image_path,'/'), *back=strrchr(image_path,'\\');
    if (back>slash) slash=back;
    const int dir=slash ? (int)(slash-image_path)+1 : 0;
    snprintf(path,sizeof(path),"%.*sanalysis.json",dir,image_path);
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    char head[4096];
    const size_t n=fread(head,1,sizeof(head)-1,f);
    fclose(f);
    head[n]=0;
    const char *sha=strstr(head,"\"source_sha256\": \"");
    if (!sha || strspn(sha+18,"0123456789abcdef")<64) return 0;
    memcpy(out,sha+18,64); out[64]=0;
    return 1;
}

void runtime_game_identify(const char *title_id, const char *app_version, const char *app0, const char *image_path) {
    (void)app0;
    char sha[65]="";
    const char *build=NULL;
    if (eboot_sha256(image_path,sha))
        for (size_t i=0;i<sizeof(known_builds)/sizeof(*known_builds);++i)
            if (!strcmp(sha,known_builds[i].sha256)) build=known_builds[i].version;
    printf("Game: %s version %s, eboot.bin sha256 %s (%s%s)\n",title_id[0] ? title_id : "?",
           app_version[0] ? app_version : "unknown",sha[0] ? sha : "unknown",
           build ? "known build " : "a build the runner was not checked with",build ? build : "");
}

/* settings.ini frame_rate onto the engine's frame clock (eboot 01.10). The clock (a global at
 * 0x152cd28, vtable 0xc6c2c8) holds the target frame rate at +8; its set_fps (vtable slot 3, the
 * engine_set_fps script command's) also sets the step 1/fps. The game sets 59.94 at boot, runs one
 * update per frame without frame skip, and waits for one vblank a frame; the vblank follows
 * frame_rate (emulator_settings.h), so the target becomes frame_rate*0.999 like the game's 59.94.
 * resogun_frame_rate() is the rate the window's display can show (ResogunSettings::
 * EffectiveFrameRate). Called on the game's thread at each flip, it also restores the rate after
 * the game resets it. */
static unsigned char *game_image;
static int clock_unknown;
void runtime_game_set_image(unsigned char *image) { game_image=image; }
void runtime_game_frame_rate(void) {
    if (!game_image || clock_unknown) return;
    float *clock=*(float **)(game_image+0x152cd28);
    if (!clock) return;
    void **vtable=*(void ***)clock;
    if ((unsigned char *)vtable!=game_image+0xc6c2c8) {
        clock_unknown=1;
        resogun_set_frame_rate(60); /* the vblank back to the game's own rate */
        puts("Frame rate: the engine's frame clock is not where eboot 01.10 has it; frame_rate stays 60");
        return;
    }
    const int rate=resogun_frame_rate();
    const float want=(float)rate*0.999f;
    if (fabsf(clock[2]-want)<0.001f) return;
    typedef void (ABI *SetFps)(void *clock, float fps);
    ((SetFps)vtable[3])(clock,want);
    printf("Frame rate: %d FPS (settings.ini frame_rate; engine target %.2f)\n",rate,(double)want);
}

/* A 4:3, 21:9 or 32:9 output (eboot 01.10). The engine sizes its display buffers from a table at
 * 0xa5d120 (1920x1080 on PS4, 3840x2160 on PS4 Pro) and takes its aspect ratio from the video
 * out's size (emulator_settings.h, resogun_width_4k).
 * Its render scripts create the scene targets with fixed sizes (mrt_albedo 1920x1080 or 1920x2160
 * checkerboard, SSAO at half, menu_overlay for the pause menu...) through 0x476aa0 (render targets:
 * this, name, format, width, height, options) and 0x478840 and 0x478910 (depth buffers: this,
 * name, width, height, ...); each starts with a jmp to a stub that widens the size before the
 * function's own prologue runs (narrows it, for 4:3). The main menu's panels (menu_slide_*, dummy_depth*) are drawn on
 * 3D cards and keep their size.
 * The engine's perspective takes a vertical half-angle tan(fov/2)/sqrt(1+aspect) (measured: the
 * main menu's and the gameplay camera's matrices at 16:9, 21:9 and 32:9), so a wider picture would
 * also see less above and below: 10% at 21:9, 28% at 32:9, with the HUD and the menus, drawn
 * in 3D before the camera, pushed past the top and bottom. Every camera goes through 0x420bd0
 * (this, view, projection, fov, near, far, aspect) with its projection matrix; for the screen's
 * cameras (the aspect of the picture) a copy with 16:9's vertical scale is passed instead, and a
 * fov that gives it, so the picture is the PS4's with more at the sides; at 4:3, 16:9's
 * horizontal scale, the PS4's with more above and below. */
uint32_t runtime_game_wide_width(const char *name, uint32_t width, uint32_t height, uint32_t width_4k) {
    if (width_4k==3840 || !width_4k || !height) return width;
    if (name && (!strncmp(name,"menu_slide",10) || !strncmp(name,"dummy_depth",11))) return width;
    if ((uint64_t)width*9!=(uint64_t)height*16 && (uint64_t)width*9!=(uint64_t)height*8) return width;
    uint32_t wide=(uint32_t)(((uint64_t)width*width_4k+3839)/3840);
    return wide+(wide&1);
}

static uint32_t wide_4k;
static int wide_verbose;
enum { WIDE_TARGET=0, WIDE_DEPTH=1 };
/* Called by the stubs with the guest's argument registers as pushed: r11 r10 r9 r8 rdi rsi rdx
 * rcx rax. Render targets have their width in ecx and height in r8d, depth buffers in edx, ecx. */
static ABI void wide_create(uint64_t *regs, uint64_t kind) {
    enum { R8=3, RSI=5, RDX=6, RCX=7 };
    const char *name=(const char *)regs[RSI];
    uint64_t *width=&regs[kind==WIDE_TARGET ? RCX : RDX];
    const uint32_t height=(uint32_t)regs[kind==WIDE_TARGET ? R8 : RCX], before=(uint32_t)*width;
    const uint32_t after=runtime_game_wide_width(name,before,height,wide_4k);
    if (after==before) return;
    *width=after;
    if (wide_verbose) printf("Wide: %s %ux%u -> %ux%u\n",name ? name : "?",before,height,after,height);
}

/* Each hooked function starts with this prologue (push rbp; mov rbp,rsp; push r15..rbx; sub
 * rsp,frame): 17 bytes, none rip-relative, so the stub runs them in place and jumps back. */
static int wide_prologue(const unsigned char *at, unsigned char frame) {
    static const unsigned char prologue[16]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
    return !memcmp(at,prologue,sizeof(prologue)) && at[16]==frame;
}
static unsigned char *wide_hook(unsigned char *image, unsigned char *stub, uint32_t offset, unsigned kind) {
    static const unsigned char save[]={0x50,0x51,0x52,0x56,0x57,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53, /* push rax..r11 */
                                       0x48,0x89,0xe7};                                                   /* mov rdi, rsp */
    static const unsigned char restore[]={0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5f,0x5e,0x5a,0x59,0x58};
    unsigned char *site=image+offset, *p=stub;
    const uint64_t call=(uint64_t)(uintptr_t)wide_create, back=(uint64_t)(uintptr_t)(site+17), to=(uint64_t)(uintptr_t)stub;
    /* At entry rsp is 8 mod 16; nine pushes make it 16-aligned for the call. */
    memcpy(p,save,sizeof(save)); p+=sizeof(save);
    *p++=0xbe; memcpy(p,&kind,4); p+=4;                     /* mov esi, kind */
    *p++=0x48; *p++=0xb8; memcpy(p,&call,8); p+=8;          /* mov rax, wide_create */
    *p++=0xff; *p++=0xd0;                                   /* call rax */
    memcpy(p,restore,sizeof(restore)); p+=sizeof(restore);
    memcpy(p,site,17); p+=17;                               /* the prologue */
    *p++=0xff; *p++=0x25; memset(p,0,4); p+=4;              /* jmp [rip]: back */
    memcpy(p,&back,8); p+=8;
    site[0]=0xff; site[1]=0x25; memset(site+2,0,4);         /* jmp [rip]: the stub */
    memcpy(site+6,&to,8);
    memset(site+14,0x90,3);
    return p;
}
/* 0x420bd0's projection on the guest's thread: the caller's matrix stays as it is. */
static _Thread_local float wide_projection[16];
/* The stub passes xmm0..xmm3 (fov, near, far, aspect; 16 bytes each) and the argument registers
 * as pushed (r11 r10 r9 r8 rdi rsi rdx rcx rax); changes to either reach the function. */
static ABI void wide_camera(float *xmm, uint64_t *regs) {
    enum { RDX=6 };
    const float aspect=xmm[12], wide=(float)wide_4k/2160.0f;
    const float *m=(const float *)regs[RDX];
    if (!m || fabsf(aspect-wide)>wide*0.001f) return;
    /* The screen's orthographic cameras (w = 1; x within +-aspect, y +-1: menus, backdrops) show
     * more at the sides when wider; narrower, they zoom out to 16:9's width, as the pause menu's
     * layout needs (the menus then keep 16:9 with dark bands above and below). */
    if (m[11]==0.0f && m[15]==1.0f) {
        const float fit=aspect*9.0f/16.0f;
        if (fit>=1.0f) return;
        memcpy(wide_projection,m,sizeof(wide_projection));
        wide_projection[0]*=fit;
        wide_projection[5]*=fit;
        regs[RDX]=(uint64_t)(uintptr_t)wide_projection;
        return;
    }
    /* A perspective (w = z) for the picture's shape: shadow, cube and card cameras differ. */
    if (m[11]!=1.0f || m[15]!=0.0f) return;
    /* Wider than 16:9 keeps its height (more at the sides), narrower its width (more above and
     * below): the smaller zoom keeps both of 16:9's extents in view, the HUD included. */
    const float tall=sqrtf((1.0f+16.0f/9.0f)/(1.0f+aspect)), across=tall*aspect*9.0f/16.0f;
    const float scale=fminf(tall,across);
    memcpy(wide_projection,m,sizeof(wide_projection));
    wide_projection[0]*=scale;
    wide_projection[5]*=scale;
    regs[RDX]=(uint64_t)(uintptr_t)wide_projection;
    xmm[0]=2.0f*atanf(tanf(xmm[0]*0.5f)/scale);
}
/* 0x420bd0 starts with 14 bytes that are not rip-relative: vmovups ymm4,[rdx]; vmovups
 * ymm5,[rdx+0x20]; vxorps xmm10,xmm10,xmm10. */
static const unsigned char camera_head[14]={0xc5,0xfc,0x10,0x22,0xc5,0xfc,0x10,0x6a,0x20,0xc4,0x41,0x28,0x57,0xd2};
static unsigned char *wide_camera_hook(unsigned char *image, unsigned char *stub) {
    static const unsigned char save[]={0x50,0x51,0x52,0x56,0x57,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53, /* push rax..r11 */
        0x48,0x83,0xec,0x40,                                                    /* sub rsp, 0x40 */
        0x0f,0x11,0x04,0x24, 0x0f,0x11,0x4c,0x24,0x10,                         /* movups [rsp..], xmm0..xmm3 */
        0x0f,0x11,0x54,0x24,0x20, 0x0f,0x11,0x5c,0x24,0x30,
        0x48,0x89,0xe7, 0x48,0x8d,0x74,0x24,0x40};                              /* mov rdi, rsp; lea rsi, [rsp+0x40] */
    static const unsigned char restore[]={0x0f,0x10,0x04,0x24, 0x0f,0x10,0x4c,0x24,0x10,
        0x0f,0x10,0x54,0x24,0x20, 0x0f,0x10,0x5c,0x24,0x30, 0x48,0x83,0xc4,0x40,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5f,0x5e,0x5a,0x59,0x58};
    unsigned char *site=image+0x420bd0, *p=stub;
    const uint64_t call=(uint64_t)(uintptr_t)wide_camera, back=(uint64_t)(uintptr_t)(site+sizeof(camera_head)),
                   to=(uint64_t)(uintptr_t)stub;
    /* Nine pushes and 0x40 bytes keep the call 16-aligned, as in wide_hook. */
    memcpy(p,save,sizeof(save)); p+=sizeof(save);
    *p++=0x48; *p++=0xb8; memcpy(p,&call,8); p+=8;          /* mov rax, wide_camera */
    *p++=0xff; *p++=0xd0;                                   /* call rax */
    memcpy(p,restore,sizeof(restore)); p+=sizeof(restore);
    memcpy(p,camera_head,sizeof(camera_head)); p+=sizeof(camera_head);
    *p++=0xff; *p++=0x25; memset(p,0,4); p+=4;              /* jmp [rip]: back */
    memcpy(p,&back,8); p+=8;
    site[0]=0xff; site[1]=0x25; memset(site+2,0,4);         /* jmp [rip]: the stub */
    memcpy(site+6,&to,8);
    return p;
}

int runtime_game_widen(unsigned char *image, uint32_t width_4k) {
    static const uint32_t table[4]={1920,1080,3840,2160};
    uint32_t *sizes=(uint32_t *)(image+0xa5d120);
    if (memcmp(sizes,table,sizeof(table)) || !wide_prologue(image+0x476aa0,0x28) ||
        !wide_prologue(image+0x478840,0x18) || !wide_prologue(image+0x478910,0x18) ||
        memcmp(image+0x420bd0,camera_head,sizeof(camera_head))) {
        puts("Wide: the engine's sizes are not where eboot 01.10 has them; the picture stays 16:9");
        return 0;
    }
    unsigned char *stub=runtime_low_map(4096,PROT_READ|PROT_WRITE|PROT_EXEC);
    if (!stub) return 0;
    wide_4k=width_4k;
    wide_verbose=runtime_verbose();
    sizes[0]=width_4k/2; sizes[2]=width_4k;
    unsigned char *p=wide_hook(image,stub,0x476aa0,WIDE_TARGET);
    p=wide_hook(image,p,0x478840,WIDE_DEPTH);
    p=wide_hook(image,p,0x478910,WIDE_DEPTH);
    wide_camera_hook(image,p);
    printf("Wide: the game's picture is %ux2160 (%ux1080 in 1080p mode), its scene resized to match\n",width_4k,width_4k/2);
    return 1;
}
