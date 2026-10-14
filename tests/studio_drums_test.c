/* SPDX-License-Identifier: GPL-3.0-only */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
int main(int argc,char **argv)
{
    uint32_t i,j,k,v,total=0; uint64_t energy[DRUM_KITS]={0};
    int32_t out[CTL*2]; dstep_t step={{0}};
    host_tracks_init();
    for(i=0;i<DRUM_LANES;i++)dstep_set(&step,i,i&3u,(i>>2)&3u);   /* SLOOP 2.0: all 16 lanes in one step */
    assert(dstep_mask(&step)==0xFFFFu);
    for(i=0;i<DRUM_LANES;i++)assert(dstep_has(&step,i) && dstep_lvl(&step,i)==(i&3u) && dstep_rat(&step,i)==((i>>2)&3u));
    dstep_clr(&step,5);assert(!dstep_has(&step,5) && dstep_has(&step,4) && dstep_has(&step,6) && dstep_lvl(&step,6)==2u);
    for(i=0;i<27u;i++)assert(lane_of_key(i)<DRUM_LANES);
    assert(lane_of_key(0)==0 && lane_of_key(1)==0 && lane_of_key(26)==15 && key_of_lane(15)==26);
    for(i=35;i<=81;i++)assert(lane_of_note(i)<DRUM_LANES);
    for(i=0;i<DRUM_LANES;i++)assert(lane_of_note(LANE_NOTE[i])==i);
    /* Render the same hits through all five kits; each must be distinct,
     * finite and silent after its one-shots have finished. */
    for(i=0;i<DRUM_USR;i++)   /* (the built-in kits: USR1..3 are the user slots) */ {
        memset(&drums,0,sizeof drums);drums.set=-2;
        TDRUM->p[P_E0]=(int16_t)i;
        drum_on(36,110);drum_on(38,100);drum_on(46,80);
        for(j=0;j<FS*2u/CTL;j++) {
            int32_t l[CTL]={0},r[CTL]={0},rev[CTL]={0};
            drums_render(l,r,rev,rev,CTL);
            for(k=0;k<CTL;k++){assert(l[k]>-131072 && l[k]<131072);energy[i]+=l[k]<0?-l[k]:l[k];}
        }
        assert(energy[i]>10000);
        for(k=0;k<NDRUM;k++)assert(!drums.v[k].active);
        for(k=0;k<i;k++)assert(energy[k]!=energy[i]);
    }
    /* LIVE metronome (seq.c click_tick): 120 BPM, 2 s = 4 beats; ON always while playing, OFF never, REC never
     * (it clicked while a track recorded: live recording is gone); the first beat of the bar is louder */
    for(v=0;v<4;v++) {
        uint32_t hits=0,loud=0,age0;
        host_tracks_init();memset(&drums,0,sizeof drums);drums.set=-2;
        song.g[G_BPM]=120;song.g[G_CLOCK]=v==3?0:v==2?2:1;   /* OFF / ON / REC */
        transport_req=1;age0=drums.age;
        for(j=0;j<(FS*2u-FS/4u)/CTL;j++) {               /* up to just before beat 4 */
            uint32_t c=click_n;                          /* (2.4.1: the click is its own voice, drums.c click_on) */
            mix_block(out,CTL);
            if(click_n!=c){hits++;loud+=click_acc;}
        }
        transport_req=2;events_block(CTL);
        if(v==2)assert(hits==4 && loud==1);
        else assert(hits==0 && drums.age==age0);
    }
    total=(FS*4u/CTL)*CTL;
    FILE *f=argc>1?fopen(argv[1],"wb"):0;
    if(f)wav_hdr(f,total);
    host_tracks_init();song.g[G_BPM]=120;transport_req=1;
    for(j=0;j<FS*4u/CTL;j++) {
        mix_block(out,CTL);
        for(k=0;k<CTL;k++)if(f)wav_put(f,out[k*2],out[k*2+1]);
    }
    transport_req=2;events_block(CTL);
    if(f)fclose(f);
    puts("drums: 16 lanes a step (levels, ratchets), the key / GM maps, every kit distinct, bounded audio and finite tails, metronome (OFF/ON, accent) PASS");
    return 0;
}
