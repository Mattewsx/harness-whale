#include "whale_assets.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "whale_asset_pack.h"
#include "whale_asset_pack_id.h"
#include <string.h>

#define WHALE_ASSET_SUBTYPE 0x40
static const char *TAG = "whale_assets";
const uint8_t *whale_poses, *whale_companion, *whale_pcm;
static bool ready;
/* Searched by the OTA tools to learn which pack an application image needs. */
__attribute__((used)) const char whale_asset_marker[] = WHALE_ASSET_PACK_MARKER;

bool whale_assets_ready(void) { return ready; }

bool whale_assets_init(bool full) {
    static esp_partition_mmap_handle_t handle;
    static const void *mapped;
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, WHALE_ASSET_SUBTYPE, "assets");
    if (!part) {
        ESP_LOGE(TAG, "assets partition missing; full USB flash required");
        return false;
    }
    if (!mapped && esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &mapped,
                                      &handle) != ESP_OK) {
        ESP_LOGE(TAG, "assets mmap failed");
        mapped = NULL;
        return false;
    }
    const uint8_t *base = mapped;
    whale_pack_t pack;
    if (!whale_pack_parse(base, part->size, &pack)) {
        ESP_LOGE(TAG, "assets header invalid");
        return false;
    }
    if (memcmp(pack.sha256, whale_asset_pack_sha256, 32)) {
        ESP_LOGE(TAG, "asset pack mismatch; this firmware needs %s",
                 WHALE_ASSET_PACK_SHA256_HEX);
        return false;
    }
    const uint8_t *body = base + pack.body_offset;
    if (full) {
        uint8_t digest[32];
        if (mbedtls_sha256(body, pack.body_size, digest, 0) ||
            memcmp(digest, pack.sha256, 32)) {
            ESP_LOGE(TAG, "asset pack body hash mismatch");
            return false;
        }
    }
    const whale_pack_entry_t *poses = whale_pack_find(&pack, "poses.rgb", WHALE_POSES_BYTES);
    const whale_pack_entry_t *companion =
        whale_pack_find(&pack, "companion.rgb", WHALE_COMPANION_BYTES);
    const whale_pack_entry_t *pcm = whale_pack_find(&pack, "cues.pcm", WHALE_PCM_BYTES);
    if (!poses || !companion || !pcm) {
        ESP_LOGE(TAG, "asset pack entries do not match this firmware");
        return false;
    }
    whale_poses = body + poses->offset;
    whale_companion = body + companion->offset;
    whale_pcm = body + pcm->offset;
    ready = true;
    /* Referencing the marker also keeps it through --gc-sections. */
    ESP_LOGI(TAG, "%.33s ok (%s)", whale_asset_marker, full ? "verified" : "header");
    return true;
}
