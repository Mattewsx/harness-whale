#include "../main/whale_settings.c"
#include <assert.h>
#include <stdio.h>
static int failure,closed,stored=1;
int nvs_open(const char *n,int mode,nvs_handle_t *h){(void)n;(void)mode;*h=1;return failure==1?-1:0;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v){(void)h;(void)k;*v=stored;return failure==2?-1:0;}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v){(void)h;(void)k;if(failure==3)return -1;stored=v;return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return failure==4?-1:0;}
void nvs_close(nvs_handle_t h){(void)h;closed++;}
int main(void){
 assert(whale_sound_load()==1);assert(whale_sound_save(0));assert(whale_sound_load()==0);
 assert(whale_sound_save(1));assert(whale_sound_load()==1);
 stored=255;assert(whale_sound_load()==1);assert(!whale_sound_save(2));
 for(failure=1;failure<=2;failure++)assert(whale_sound_load()==1);
 for(failure=3;failure<=4;failure++)assert(!whale_sound_save(0));
 assert(closed>=6);puts("Sound settings default, invalid value, persistence and failures PASS");return 0;
}
