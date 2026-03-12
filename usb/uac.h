#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"
#include "common_types.h"
#include "tusb.h"
#include "usb_descriptors.h"

// i2s
#include "i2s.h"

extern volatile bool freq_changed;

enum {
  BLINK_STREAMING = 25,
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

enum {
  VOLUME_CTRL_0_DB = 0,
  VOLUME_CTRL_10_DB = 2560,
  VOLUME_CTRL_20_DB = 5120,
  VOLUME_CTRL_30_DB = 7680,
  VOLUME_CTRL_40_DB = 10240,
  VOLUME_CTRL_50_DB = 12800,
  VOLUME_CTRL_60_DB = 15360,
  VOLUME_CTRL_70_DB = 17920,
  VOLUME_CTRL_80_DB = 20480,
  VOLUME_CTRL_90_DB = 23040,
  VOLUME_CTRL_100_DB = 25600,
  VOLUME_CTRL_SILENCE = 0x8000,
};

uint32_t get_current_sample_rate();


void tud_mount_cb(void);

// Invoked when device is unmounted
void tud_umount_cb(void);

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en);

// Invoked when usb bus is resumed
void tud_resume_cb(void);
void audio_task(void);

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// UAC1 Helper Functions
//--------------------------------------------------------------------+

static bool audio10_set_req_ep(tusb_control_request_t const *p_request, uint8_t *pBuff);

static bool audio10_get_req_ep(uint8_t rhport, tusb_control_request_t const *p_request);

static bool audio10_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff);

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request);

//--------------------------------------------------------------------+
// UAC2 Helper Functions
//--------------------------------------------------------------------+

#if TUD_OPT_HIGH_SPEED
// List of supported sample rates for UAC2

#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

static bool audio20_clock_get_request(uint8_t rhport, audio20_control_request_t const *request);

static bool audio20_clock_set_request(audio20_control_request_t const *request, uint8_t const *buf);

static bool audio20_feature_unit_get_request(uint8_t rhport, audio20_control_request_t const *request);

static bool audio20_feature_unit_set_request(audio20_control_request_t const *request, uint8_t const *buf);

static bool audio20_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request);

static bool audio20_set_req_entity(tusb_control_request_t const *p_request, uint8_t *buf);

#endif // TUD_OPT_HIGH_SPEED

//--------------------------------------------------------------------+
// Main Callback Functions
//--------------------------------------------------------------------+

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request);

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff);

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request);

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf);

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request);

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request);

#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
  void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param);
#endif

#if CFG_AUDIO_DEBUG
bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting);
#endif

//--------------------------------------------------------------------+
// AUDIO Task
//--------------------------------------------------------------------+

// This task simulates an audio transmit callback, one frame is sent every 1ms.
// In a real application, this would be replaced with actual I2S transmit callback.

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) ;

#if CFG_AUDIO_DEBUG
//--------------------------------------------------------------------+
// HID interface for audio debug
//--------------------------------------------------------------------+
// Every 1ms, we will sent 1 debug information report
void audio_debug_task(void);

// Invoked when received GET_REPORT control request
// Unused here
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);

// Invoked when received SET_REPORT control request or
// Unused here
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
#endif