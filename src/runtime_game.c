/* The game's version, logged first so a bug report's log tells which dump it came from: param.sfo's
 * TITLE_ID and APP_VER, and the eboot's SHA-256 that scripts/prepare.py records in analysis.json
 * beside the image. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
