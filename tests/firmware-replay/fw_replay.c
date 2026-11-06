#include "whale_app.c"
static unsigned queued_cues;
// This executable runs the embedded message dispatcher and key controller on a host.
void *xQueueCreate(int x,int y){return (void*)1;}int xQueueReceive(void*a,void*b,int t){return 0;}
int xQueueSend(void*q,const void*b,int t){if(q==cues)queued_cues++;return 1;}int bsp_battery_init(void){return 0;}int bsp_battery_soc(void){return 64;}
int bsp_display_init(void){return 0;}void *bsp_lvgl_init(void){return (void*)1;}bool bsp_lvgl_lock(int t){return true;}
void bsp_lvgl_unlock(void){}void bsp_display_backlight(int p){}
int bsp_button_init(void(*f)(int,int,void*),void*p){return 0;}
void xTaskCreate(void(*f)(void*),const char*c,int n,void*p,int i,void*q){}void vTaskDelay(int n){}
TaskHandle_t xTaskGetHandle(const char*n){return 0;}unsigned uxTaskGetStackHighWaterMark(void*t){return 4096;}
int nvs_flash_init(void){return 0;}int64_t esp_timer_get_time(void){return 0;}
void whale_pair(bool y){}bool whale_unbind(void){return true;}void whale_ble_start(void){}
void whale_ui_init(void){}void whale_ui_draw(const whale_app_t *app){}
bool whale_audio_done(void){return true;}
bool whale_displayable(const char *s){return true;}bool whale_text_wide(const char *s){return false;}int whale_scroll_max(const char *s){return 0;}
// Fixture lines are separated by newlines and fit the screen width.
int whale_home_scroll_max(const char *s){
    int lines=*s?1:0;while(*s){if(*s++=='\n')lines++;}
    return lines>HOME_REPLY_LINES?lines-HOME_REPLY_LINES:0;
}
bool whale_send(uint8_t type,const uint8_t *b,size_t n){
    printf("tx %02x ",type);for(size_t i=0;i<n;i++)printf("%02x",b[i]);puts("");return true;
}
size_t whale_line_offset(const char *s,int line){const char *start=s;while(*s && line>0)if(*s++=='\n')line--;return s-start;}
int whale_scroll_at(const char *s,size_t offset){int line=0;for(size_t i=0;s[i] && i<offset;i++)if(s[i]=='\n')line++;return line;}
int main(void){
    cues=(void*)2;
    char line[4096];unsigned val;
    while(fgets(line,sizeof(line),stdin)){
        if(!strncmp(line,"rx ",3)){whale_event_t e={.kind=WE_FRAME};
            char *p=line+3;while(e.len<20 && sscanf(p,"%2x",&val)==1){e.bytes[e.len++]=val;p+=2;}
            a.now++;service(e);
        }else if(sscanf(line,"key %u",&val)==1){key(val);}
        else if(sscanf(line,"advance %u",&val)==1){a.now+=val;companion_tick();}
        else if(sscanf(line,"cues %u",&val)==1){assert(queued_cues==val);}
        else if(sscanf(line,"view %u",&val)==1){assert(a.view==val);}
        else if(sscanf(line,"scroll %u",&val)==1){assert(a.scroll==(int)val);}
        else if(!strncmp(line,"down",4)){service((whale_event_t){.kind=WE_DOWN});}
        else if(!strncmp(line,"assert ",7)){
            int view,count,selected;assert(sscanf(line+7,"%d%d%d",&view,&count,&selected)==3);
            if(a.view!=view || a.count!=count || a.selected!=selected){fprintf(stderr,"expected %d %d %d got %d %d %d\n",view,count,selected,a.view,a.count,a.selected);return 1;}
        }
    }return 0;
}

void esp_fill_random(void *p, size_t n) { memset(p, 7, n); }
bool whale_ble_audio_ready(void) { return true; }
bool whale_ble_audio_send(const uint8_t *p, size_t n) { (void)p; (void)n; return true; }
bool whale_voice_begin(const uint8_t id[24]) { (void)id; return true; }
void whale_voice_stop(bool discard) { (void)discard; }
void whale_voice_ack(const uint8_t id[24], uint32_t next) { (void)id; (void)next; }
bool whale_voice_busy(void) { return false; }
bool whale_voice_pending(void) { return false; }
void whale_voice_run(void) {}
uint32_t whale_voice_milliseconds(void) { return 0; }

static uint32_t fake_audio_generation;
void whale_audio_interrupt(void){fake_audio_generation++;}
uint32_t whale_audio_generation(void){return fake_audio_generation;}
bool whale_audio_play(whale_audio_cmd_t c){(void)c;return true;}
uint8_t whale_sound_load(void){return 1;}
bool whale_sound_save(uint8_t mode){return mode<2;}

bool whale_audio_busy(void) { return false; }
