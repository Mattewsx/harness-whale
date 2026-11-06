#include "../main/whale_settings.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char blob[16];static size_t blob_size;
static int failure,closed,stored=1;
int nvs_open(const char *n,int mode,nvs_handle_t *h){(void)n;(void)mode;*h=1;return failure==1?-1:0;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v){(void)h;(void)k;*v=stored;return failure==2?-1:0;}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v){(void)h;(void)k;if(failure==3)return -1;stored=v;return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return failure==4?-1:0;}
void nvs_close(nvs_handle_t h){(void)h;closed++;}
int nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *n) {
    (void)h;
    (void)k;
    if (failure == 2 || !blob_size || *n < blob_size)
        return -1;
    memcpy(v, blob, blob_size);
    *n = blob_size;
    return 0;
}
int nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t n) {
    (void)h;
    (void)k;
    if (failure == 3)
        return -1;
    assert(n <= sizeof blob);
    memcpy(blob, v, n);
    blob_size = n;
    return 0;
}
static void test_blobs(void) {
    unsigned char in[8] = {1, 2, 3, 4, 5, 6, 7, 8}, out[8] = {0}, small[4], big[12];
    failure = 0;
    assert(!whale_blob_load("log", out, sizeof out));
    assert(whale_blob_save("log", in, sizeof in));
    assert(whale_blob_load("log", out, sizeof out) && !memcmp(in, out, sizeof in));
    assert(!whale_blob_load("log", small, sizeof small)); /* size mismatch reads as absent */
    assert(!whale_blob_load("log", big, sizeof big));
    failure = 1;
    assert(!whale_blob_load("log", out, sizeof out) && !whale_blob_save("log", in, sizeof in));
    failure = 2;
    assert(!whale_blob_load("log", out, sizeof out));
    for (failure = 3; failure <= 4; failure++)
        assert(!whale_blob_save("log", in, sizeof in));
    failure = 0;
}
int main(void){
 assert(whale_sound_load()==1);assert(whale_sound_save(0));assert(whale_sound_load()==0);
 assert(whale_sound_save(1));assert(whale_sound_load()==1);
 stored=255;assert(whale_sound_load()==1);assert(!whale_sound_save(2));
 for(failure=1;failure<=2;failure++)assert(whale_sound_load()==1);
 for(failure=3;failure<=4;failure++)assert(!whale_sound_save(0));
 test_blobs();assert(closed>=6);puts("Sound settings and NVS blobs: default, invalid value, persistence and failures PASS");return 0;
}
