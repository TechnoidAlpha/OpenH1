/* Both sides of the cancellation boundary use their actual structures/functions: the game's
completion and Flush (sound_dsound_xbox.c over dsound_sdl.c), and the mixer's hand-off, which lets
go of the packets a flush cancels and starts the voice over. */
#include "harness.h"
#include "types.inc"
typedef unsigned long long Uint64;
static struct sdl_stream stream;
static struct sdl_stream *streams=&stream;
static struct dsound_globals dsound_globals;
static int refills,unlocks,events,resets,lock_depth,freed,hand_off_in_callback;
static unsigned long status[8],completed[8];
#define voices_lock 0
#define pthread_mutex_lock(...) ((void)lock_depth++)
#define pthread_mutex_unlock(...) ((void)lock_depth--)
#define channel_get(i) (&dsound_globals.channels[i])
#define sound_cache_sound_hardware_unlock(...) ((void)unlocks++)
#define interrupt_time_error(...) CHECK(FALSE,"invalid completion")
#define SetEvent(...) ((void)events++)
#define resampler_reset(...) ((void)resets++)
/* (the fade of what the voice was playing when it is cut off: the mixer's, test_mixer.py) */
#define voice_cut(...) ((void)0)
static void counted_free(void *memory) { if (memory) freed++; free(memory); }
#define free(memory) counted_free(memory)
static void dsound_channel_fill(short i) {
 (void)i; refills++;
 CHECK(refills<10,"a flush repeatedly refills itself");
 stream.packet_count++; stream.submitted++; dsound_globals.channels[0].packet_count++;
}
static HRESULT stream_flush(IDirectSoundStream *object);
#define DirectSoundStopStream(s) ((void)stream_flush(s))
/* the call log (debug.dsound_log) is off in the test */
#define DSLOG_ON 0
static void dsound_log(const char *code,const char *format,...) { (void)code; (void)format; }
/* The real callback's Xbox pointer-sized context is cast to a short channel number. */
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#include "state.inc"
#include "under_test.inc"
static void callback(void *context,void *packet,DWORD result) {
 CHECK(lock_depth==0,"callback runs without the mixer lock");
 /* the mixer may take a hand-off while the lock is let go for the callback */
 if(hand_off_in_callback) {
  voice_hand_off(&stream);
  CHECK(stream.mixing_count==0,"the mixer took a packet being flushed (%lu held)",stream.mixing_count);
 }
 dsound_channel_callback(context,packet,result);
}
/* count packets the game submitted, the first finished (finished 1) or all (2), the mixer holding
all but the last (taken over at its last hand-off) and part way into the first not finished */
static void reset(int count,int finished,int wrapped) {
 memset(&stream,0,sizeof(stream)); memset(&dsound_globals,0,sizeof(dsound_globals));
 stream.callback=callback; stream.packet_head=wrapped?MAXIMUM_STREAM_PACKETS-2:0;
 stream.packet_count=count; stream.submitted=100+count;
 dsound_globals.actual_channel_count=1;
 struct sound_channel *channel=channel_get(0);
 channel->stream=&stream.object; channel->state=_sound_channel_playing; channel->packet_count=count;
 channel->playing_permutation=channel->queued_permutation=(struct sound_permutation *)&stream;
 for(int i=0;i<count;i++) {
  struct voice_packet *p=&stream.packets[(stream.packet_head+i)%MAXIMUM_STREAM_PACKETS];
  p->finished=finished==2 || (finished==1 && i==0);
  p->samples=malloc(16); p->frames=8; p->packet.dwMaxSize=512;
  status[i]=XMEDIAPACKET_STATUS_PENDING; completed[i]=999;
  p->packet.pdwStatus=&status[i]; p->packet.pdwCompletedSize=&completed[i];
  p->packet.pContext=(void *)(long)(i+1);
  if(i<count-1) stream.mixing[(5+i)%MAXIMUM_STREAM_PACKETS]=*p;
 }
 stream.mixing_head=5; stream.mixing_count=count?count-1:0; stream.mixing_next=stream.submitted-(count?1:0);
 stream.cursor=71;
 refills=unlocks=events=resets=lock_depth=freed=hand_off_in_callback=0;
 retired_count=0; hand_offs=0;
}
int main(int argc,char **argv) {
 CHECK(argc==2,"case name"); const char *case_name=argv[1];
 int count=!strcmp(case_name,"empty")?0:3;
 int finished=!strcmp(case_name,"unfinished") || !strcmp(case_name,"taken-during-flush")?0:
  (!strcmp(case_name,"all-finished")?2:1);
 int finished_count=finished==2?count:(finished==1 && count?1:0);
 reset(count,finished,!strcmp(case_name,"wrapped"));
 CASE("ordinary") {
  streams_complete_finished();
  CHECK(status[0]==XMEDIAPACKET_STATUS_SUCCESS && completed[0]==512,"ordinary completion succeeds");
  CHECK(refills==1 && unlocks==1 && stream.packet_count==3,"ordinary completion refills");
  CHECK(freed==1 && retired_count==0,"a finished packet's samples are freed at once");
  voice_hand_off(&stream);
  CHECK(stream.mixing_count==stream.packet_count && stream.mixing_next==stream.submitted,
   "the mixer lets go of the completed packet and takes the rest (%lu of %lu)",stream.mixing_count,stream.packet_count);
  stream_flush(&stream.object); return 0;
 }
 CASE("paused") { dsound_globals.paused=TRUE; }
 CASE("no-callback") { stream.callback=NULL; }
 CASE("taken-during-flush") { hand_off_in_callback=1; }
 channel_stop(0);
 CHECK(stream.packet_count==0 && !lock_depth,"flush empties the stream");
 CHECK(refills==0,"cancellation never refills");
 for(int i=0;i<count;i++) {
  CHECK(status[i]==XMEDIAPACKET_STATUS_FLUSHED,"even finished packets are cancelled");
  CHECK(completed[i]==((finished==2 || (finished==1 && i==0))?512:0),"preserve completed byte count");
 }
 /* the mixer may hold the unfinished packets until its next hand-off: their samples wait */
 CHECK(freed==finished_count && (int)retired_count==count-finished_count,
  "finished packets freed (%d), the others kept for the mixer (%lu)",freed,retired_count);
 retired_free();
 CHECK(freed==finished_count,"samples the mixer may hold were freed before its hand-off");
 voice_hand_off(&stream);
 hand_offs++;
 CHECK(stream.mixing_count==0 && stream.cursor==0 && resets>=1,"the mixer lets go of the flushed packets and starts over");
 CHECK(stream.flush==0,"the flush is taken once");
 retired_free();
 CHECK(freed==count && retired_count==0,"the flushed packets' samples freed after the mixer's hand-off (%d)",freed);
 CASE("no-callback") { CHECK(events==count,"event completions fire"); return 0; }
 CHECK(unlocks==count && dsound_globals.channels[0].packet_count==0,"cache ownership released once");
 CHECK(!dsound_globals.channels[0].playing_permutation && !dsound_globals.channels[0].queued_permutation,"producers cleared");
 CHECK(dsound_globals.channels[0].state==_sound_channel_idle,"channel idle");
 CASE("repeat") {
  channel_stop(0); voice_hand_off(&stream);
  CHECK(stream.packet_count==0 && stream.mixing_count==0 && refills==0 && !lock_depth,"repeated stop is harmless");
 }
 return 0;
}
