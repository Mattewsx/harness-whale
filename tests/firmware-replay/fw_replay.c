#include "whale_app.c"
// This executable runs the embedded message dispatcher and key controller on a host.
void *xQueueCreate(int x,int y){return (void*)1;}int xQueueReceive(void*a,void*b,int t){return 0;}
int xQueueSend(void*a,const void*b,int t){return 1;}int bsp_battery_init(void){return 0;}int bsp_battery_soc(void){return 64;}
int bsp_display_init(void){return 0;}void *bsp_lvgl_init(void){return (void*)1;}bool bsp_lvgl_lock(int t){return true;}
void bsp_lvgl_unlock(void){}void bsp_display_backlight(int p){}
int bsp_button_init(void(*f)(int,int,void*),void*p){return 0;}
void xTaskCreate(void(*f)(void*),const char*c,int n,void*p,int i,void*q){}void vTaskDelay(int n){}
int nvs_flash_init(void){return 0;}int64_t esp_timer_get_time(void){return 0;}
void whale_pair(bool y){}bool whale_unbind(void){return true;}void whale_ble_start(void){}
void whale_ui_init(void){}void whale_ui_draw(const whale_app_t *app){}
bool whale_displayable(const char *s){return true;}bool whale_text_wide(const char *s){return false;}int whale_scroll_max(const char *s){return 0;}
// Fixture lines are separated by newlines and fit the screen width.
int whale_home_scroll_max(const char *s){
    int lines=*s?1:0;while(*s){if(*s++=='\n')lines++;}
    return lines>HOME_REPLY_LINES?lines-HOME_REPLY_LINES:0;
}
bool whale_send(uint8_t type,const uint8_t *b,size_t n){
    printf("tx %02x ",type);for(size_t i=0;i<n;i++)printf("%02x",b[i]);puts("");return true;
}
int main(void){
    char line[4096];unsigned val;
    while(fgets(line,sizeof(line),stdin)){
        if(!strncmp(line,"rx ",3)){whale_event_t e={.kind=WE_FRAME};
            char *p=line+3;while(e.len<20 && sscanf(p,"%2x",&val)==1){e.bytes[e.len++]=val;p+=2;}
            a.now++;service(e);
        }else if(sscanf(line,"key %u",&val)==1){key(val);}
        else if(sscanf(line,"scroll %u",&val)==1){assert(a.scroll==(int)val);}
        else if(!strncmp(line,"down",4)){service((whale_event_t){.kind=WE_DOWN});}
        else if(!strncmp(line,"assert ",7)){
            int view,count,selected;assert(sscanf(line+7,"%d%d%d",&view,&count,&selected)==3);
            if(a.view!=view || a.count!=count || a.selected!=selected){fprintf(stderr,"expected %d %d %d got %d %d %d\n",view,count,selected,a.view,a.count,a.selected);return 1;}
        }
    }return 0;
}
