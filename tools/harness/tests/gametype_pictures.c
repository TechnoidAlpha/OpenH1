#include "harness.h"
/* the widgets as menu_functions.c's gametype pictures use them */
struct widget_instance {
 char const *name; boolean visible;
 struct widget_instance *child,*next,*focused_child;
 struct {short current_frame_index;} animation;
 union {
  struct {struct widget_instance *extended_description;} list;
  struct {short string_list_index;} text_box;
 } parameters;
};
struct game_variant {long game_engine_index;};
static struct game_variant variant;
static boolean editing;
static short focused_index;
static struct game_variant *edit_variant(void){return editing?&variant:NULL;}
short ui_widget_port_list_index(struct widget_instance *list){(void)list;return focused_index;}
static void visible_set(struct widget_instance *w,boolean visible){if(w)w->visible=visible;}
#include "under_test.inc"
/* the PC version's pictures' order (Game Options' list's too): CTF, King, Slayer, Oddball, Race, then the unknown
one's; by engine: none, CTF, Slayer, Oddball, King, Race, terminator, stub */
static short const expected[]={5,0,2,3,1,4,5,5};
int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 CASE("frames") {
  for(long engine=0;engine<number_of_game_engines;engine++)
   CHECK(gametype_picture_frame(engine)==expected[engine],"engine %ld: frame %d, not %d",engine,
    gametype_picture_frame(engine),expected[engine]);
  CHECK(gametype_picture_frame(-1)==5&&gametype_picture_frame(99)==5,"an engine out of range is the unknown one's");
 } else CASE("editor") {
  static struct widget_instance list,row,description,words,picture;
  list.parameters.list.extended_description=&description;list.focused_child=&row;
  description.child=&words;words.next=&picture;
  editing=TRUE;
  for(long engine=0;engine<number_of_game_engines;engine++)for(short index=0;index<10;index++){
   variant.game_engine_index=engine;focused_index=index;
   /* (the PC version's update first: the row's words and picture) */
   words.parameters.text_box.string_list_index=index;picture.animation.current_frame_index=index;picture.visible=TRUE;
   playlist_edit_description(&list);
   boolean known=expected[engine]!=5;
   CHECK(picture.visible==known,"engine %ld row %d: picture shown %d",engine,index,picture.visible);
   if(known)CHECK(picture.animation.current_frame_index==expected[engine],"engine %ld row %d: frame %d",engine,index,
    picture.animation.current_frame_index);
   short words_index=index<7?index:index==7?8:(short)(index-1);
   CHECK(words.parameters.text_box.string_list_index==words_index,"row %d: words %d",index,
    words.parameters.text_box.string_list_index);
  }
  /* (no gametype being edited: no picture) */
  editing=FALSE;picture.visible=TRUE;focused_index=0;
  playlist_edit_description(&list);
  CHECK(!picture.visible,"a picture with no gametype");
 }
 return 0;
}
