#include "uac.h"

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// Audio controls
// Current states
uint8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];   // +1 for master channel 0
int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];// +1 for master channel 0
uint32_t current_sample_rate = 44100;


// Buffer for speaker data
uint16_t i2s_dummy_buffer[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2];
volatile bool freq_changed = false;

#if CFG_AUDIO_DEBUG
uint8_t current_alt_settings;
volatile uint16_t fifo_count;
volatile uint32_t fifo_count_avg;
#endif

uint32_t get_current_sample_rate(){
  return current_sample_rate;
}


//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// UAC1 Helper Functions
//--------------------------------------------------------------------+

static bool audio10_set_req_ep(tusb_control_request_t const *p_request, uint8_t *pBuff) {
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);

  switch (ctrlSel) {
    case AUDIO10_EP_CTRL_SAMPLING_FREQ:
      if (p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
        // Request uses 3 bytes
        TU_VERIFY(p_request->wLength == 3);

        current_sample_rate = tu_unaligned_read32(pBuff) & 0x00FFFFFF;
        freq_changed = true;
        
        // i2s_setup(current_sample_rate);
        TU_LOG2("EP set current freq: %" PRIu32 "\r\n", current_sample_rate);

        return true;
      }
      break;

    // Unknown/Unsupported control
    default:
      TU_BREAKPOINT();
      return false;
  }

  return false;
}

static bool audio10_get_req_ep(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);

  switch (ctrlSel) {
    case AUDIO10_EP_CTRL_SAMPLING_FREQ:
      if (p_request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
        TU_LOG2("EP get current freq\r\n");

        uint8_t freq[3];
        freq[0] = (uint8_t) (current_sample_rate & 0xFF);
        freq[1] = (uint8_t) ((current_sample_rate >> 8) & 0xFF);
        freq[2] = (uint8_t) ((current_sample_rate >> 16) & 0xFF);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
      }
      break;

    // Unknown/Unsupported control
    default:
      TU_BREAKPOINT();
      return false;
  }

  return false;
}

static bool audio10_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // If request is for our feature unit
  if (entityID == UAC1_ENTITY_FEATURE_UNIT) {
    switch (ctrlSel) {
      case AUDIO10_FU_CTRL_MUTE:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_SET_CUR:
            // Only 1st form is supported
            TU_VERIFY(p_request->wLength == 1);

            mute[channelNum] = pBuff[0];

            TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);
            return true;

          default:
            return false; // not supported
        }

      case AUDIO10_FU_CTRL_VOLUME:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_SET_CUR:
            // Only 1st form is supported
            TU_VERIFY(p_request->wLength == 2);

            volume[channelNum] = (int16_t)tu_unaligned_read16(pBuff);

            TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", volume[channelNum], channelNum);
            return true;

          default:
            return false; // not supported
        }

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // If request is for our feature unit
  if (entityID == UAC1_ENTITY_FEATURE_UNIT) {
    switch (ctrlSel) {
      case AUDIO10_FU_CTRL_MUTE:
        // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
        // There does not exist a range parameter block for mute
        TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute[channelNum], 1);

      case AUDIO10_FU_CTRL_VOLUME:
        switch (p_request->bRequest) {
          case AUDIO10_CS_REQ_GET_CUR:
            TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
            {
              int16_t vol = (int16_t) volume[channelNum];
              vol = vol * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol, sizeof(vol));
            }

          case AUDIO10_CS_REQ_GET_MIN:
            TU_LOG2("    Get Volume min of channel: %u\r\n", channelNum);
            {
              int16_t min = -90; // -90 dB
              min = min * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &min, sizeof(min));
            }

          case AUDIO10_CS_REQ_GET_MAX:
            TU_LOG2("    Get Volume max of channel: %u\r\n", channelNum);
            {
              int16_t max = 30; // +30 dB
              max = max * 256; // convert to 1/256 dB units
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &max, sizeof(max));
            }

          case AUDIO10_CS_REQ_GET_RES:
            TU_LOG2("    Get Volume res of channel: %u\r\n", channelNum);
            {
              int16_t res = 128; // 0.5 dB
              return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &res, sizeof(res));
            }
            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
        break;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  return false;
}

//--------------------------------------------------------------------+
// UAC2 Helper Functions
//--------------------------------------------------------------------+

#if CFG_TUD_AUDIO_UAC2
// List of supported sample rates for UAC2
const uint32_t sample_rates[] = {44100, 48000, 88200, 96000};

#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

static bool audio20_clock_get_request(uint8_t rhport, audio20_control_request_t const *request) {
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

  if (request->bControlSelector == AUDIO20_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      TU_LOG1("Clock get current freq %" PRIu32 "\r\n", current_sample_rate);

      audio20_control_cur_4_t curf = {(int32_t) tu_htole32(current_sample_rate)};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &curf, sizeof(curf));
    } else if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
      audio20_control_range_4_n_t(N_SAMPLE_RATES) rangef =
          {
              .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)};
      TU_LOG1("Clock get %d freq ranges\r\n", N_SAMPLE_RATES);
      for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
        rangef.subrange[i].bMin = (int32_t) sample_rates[i];
        rangef.subrange[i].bMax = (int32_t) sample_rates[i];
        rangef.subrange[i].bRes = 0;
        TU_LOG1("Range %d (%d, %d, %d)\r\n", i, (int) rangef.subrange[i].bMin, (int) rangef.subrange[i].bMax, (int) rangef.subrange[i].bRes);
      }

      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &rangef, sizeof(rangef));
    }
  } else if (request->bControlSelector == AUDIO20_CS_CTRL_CLK_VALID &&
             request->bRequest == AUDIO20_CS_REQ_CUR) {
    audio20_control_cur_1_t cur_valid = {.bCur = 1};
    TU_LOG1("Clock get is valid %u\r\n", cur_valid.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_valid, sizeof(cur_valid));
  }
  TU_LOG1("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
}

static bool audio20_clock_set_request(audio20_control_request_t const *request, uint8_t const *buf) {
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
  TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO20_CS_CTRL_SAM_FREQ) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_4_t));

    current_sample_rate = (uint32_t) ((audio20_control_cur_4_t const *) buf)->bCur;

    TU_LOG1("Clock set current freq: %" PRIu32 "\r\n", current_sample_rate);

    return true;
  } else {
    TU_LOG1("Clock set request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

static bool audio20_feature_unit_get_request(uint8_t rhport, audio20_control_request_t const *request) {
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_FEATURE_UNIT);

  if (request->bControlSelector == AUDIO20_FU_CTRL_MUTE && request->bRequest == AUDIO20_CS_REQ_CUR) {
    audio20_control_cur_1_t mute1 = {.bCur = mute[request->bChannelNumber]};
    TU_LOG1("Get channel %u mute %d\r\n", request->bChannelNumber, mute1.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &mute1, sizeof(mute1));
  } else if (request->bControlSelector == AUDIO20_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
      audio20_control_range_2_n_t(1) range_vol = {
          .wNumSubRanges = tu_htole16(1),
          .subrange[0] = {.bMin = tu_htole16(-VOLUME_CTRL_50_DB), tu_htole16(VOLUME_CTRL_0_DB), tu_htole16(256)}};
      TU_LOG1("Get channel %u volume range (%d, %d, %u) dB\r\n", request->bChannelNumber,
              range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256, range_vol.subrange[0].bRes / 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &range_vol, sizeof(range_vol));
    } else if (request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_2_t cur_vol = {.bCur = tu_htole16(volume[request->bChannelNumber])};
      TU_LOG1("Get channel %u volume %d dB\r\n", request->bChannelNumber, cur_vol.bCur / 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_vol, sizeof(cur_vol));
    }
  }
  TU_LOG1("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

static bool audio20_feature_unit_set_request(audio20_control_request_t const *request, uint8_t const *buf) {
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_FEATURE_UNIT);
  TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO20_FU_CTRL_MUTE) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_1_t));

    mute[request->bChannelNumber] = ((audio20_control_cur_1_t const *) buf)->bCur;

    TU_LOG1("Set channel %d Mute: %d\r\n", request->bChannelNumber, mute[request->bChannelNumber]);

    return true;
  } else if (request->bControlSelector == AUDIO20_FU_CTRL_VOLUME) {
    TU_VERIFY(request->wLength == sizeof(audio20_control_cur_2_t));

    volume[request->bChannelNumber] = ((audio20_control_cur_2_t const *) buf)->bCur;

    TU_LOG1("Set channel %d volume: %d dB\r\n", request->bChannelNumber, volume[request->bChannelNumber] / 256);

    return true;
  } else {
    TU_LOG1("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

static bool audio20_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio20_control_request_t const *request = (audio20_control_request_t const *) p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return audio20_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    return audio20_feature_unit_get_request(rhport, request);
  else {
    TU_LOG1("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
  }
  return false;
}

static bool audio20_set_req_entity(tusb_control_request_t const *p_request, uint8_t *buf) {
  audio20_control_request_t const *request = (audio20_control_request_t const *) p_request;

  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    return audio20_feature_unit_set_request(request, buf);
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return audio20_clock_set_request(request, buf);
  TU_LOG1("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

#endif // CFG_TUD_AUDIO_UAC2

//--------------------------------------------------------------------+
// Main Callback Functions
//--------------------------------------------------------------------+

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  TU_LOG2("Set interface %d alt %d\r\n", itf, alt);
  if (ITF_NUM_AUDIO_STREAMING == itf && alt != 0)
    blink_interval_ms = BLINK_STREAMING;
  
  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) {
    freq_changed = true; 
  }

#if CFG_AUDIO_DEBUG
  current_alt_settings = alt;
#endif

  return true;
}

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
  (void) rhport;
  (void) pBuff;

  if (tud_audio_version() == 1) {
    return audio10_set_req_ep(p_request, pBuff);
  } else if (tud_audio_version() == 2) {
    // We do not support any requests here
  }

  return false;// Yet not implemented
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;

  if (tud_audio_version() == 1) {
    return audio10_get_req_ep(rhport, p_request);
  } else if (tud_audio_version() == 2) {
    // We do not support any requests here
  }

  return false;// Yet not implemented
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void) rhport;

  #if CFG_TUD_AUDIO_UAC2
    return audio20_set_req_entity(p_request, buf);
  #else
    return audio10_set_req_entity(p_request, buf);  
  #endif

  return false;
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;

  #if CFG_TUD_AUDIO_UAC2
    return audio20_get_req_entity(rhport, p_request);
  #else
    return audio10_get_req_entity(rhport, p_request);
  #endif

  return false;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;

  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (ITF_NUM_AUDIO_STREAMING == itf && alt == 0)
    blink_interval_ms = BLINK_MOUNTED;

  return true;
}

#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
  void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
    (void) func_id;
    (void) alt_itf;
    // Set feedback method to fifo counting
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = current_sample_rate;

    // About FIFO threshold:
    //
    // By default the threshold is set to half FIFO size, which works well in most cases,
    // you can reduce the threshold to have less latency.
    //
    // For example, here we could set the threshold to 2 ms of audio data, as audio_task() read audio data every 1 ms,
    // having 2 ms threshold allows some margin and a quick response:
    //
    // feedback_param->fifo_count.fifo_threshold =
    //    current_sample_rate * CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX * CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX / 1000 * 2;
  }
#endif

#if CFG_AUDIO_DEBUG
bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
  (void) rhport;
  (void) n_bytes_received;
  (void) func_id;
  (void) ep_out;
  (void) cur_alt_setting;

  fifo_count = tud_audio_available();
  // Same averaging method used in UAC2 class
  fifo_count_avg = (uint32_t) (((uint64_t) fifo_count_avg * 63 + ((uint32_t) fifo_count << 16)) >> 6);

  return true;
}

#endif

//--------------------------------------------------------------------+
// AUDIO Task
//--------------------------------------------------------------------+

static uint32_t get_linear_vol_log(int16_t vol_db_raw) {
    // vol_db_raw is in 1/256 dB units from Windows
    if (vol_db_raw <= -23040 || vol_db_raw == 0x8000) return 0; // -90dB or Mute
    if (vol_db_raw >= 0) return 65535; // 0dB is Max

    // Convert to positive attenuation in dB (e.g., -10dB becomes 10)
    int32_t db = (-vol_db_raw) / 256; 
    
    /* Logarithmic Approximation:
       Every -6dB is roughly half the voltage (amplitude).
       We use this to create a smooth curve.
    */
    uint32_t shift = db / 6;       // How many "halves"
    uint32_t remainder = db % 6;   // Fine-tuning between 6dB steps

    // Simple LUT for 1dB steps between the 6dB drops
    static const uint16_t lut[] = {65535, 58430, 52115, 46480, 41455, 36975};
    
    uint32_t vol = lut[remainder] >> shift;
    
    return vol;
}

// This task simulates an audio transmit callback, one frame is sent every 1ms.
// In a real application, this would be replaced with actual I2S transmit callback.
static uint8_t usb_rx_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ];
void audio_task(void) {
  if (ap == NULL || is_i2s_init_pending || freq_changed) {
    tud_audio_clear_ep_out_ff();
    return;
  }
  static uint32_t last_audio_ms = 0;
  static bool dac_is_muted = true;
  
  uint32_t now = board_millis();
  uint16_t available_bytes = tud_audio_available();
  if (available_bytes > 0) {
    if (dac_is_muted) {
      gpio_put(3, 1);     
      dac_is_muted = false;
    }
    last_audio_ms = now;
  }else if (now - last_audio_ms > 5) {
    audio_buffer_t *ab = take_audio_buffer(ap, false);
    if (ab) {
        memset(ab->buffer->bytes, 0, ab->buffer->size);
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(ap, ab);
    }
    return;
  }
  
  // if (!dac_is_muted && (now - last_audio_ms) > 2000) {
  //   dac_is_muted = true;
  //   gpio_put(3, 0);
  // }
    
  // if (dac_is_muted) {
  //   return;
  // }
    
  // IMPORTANT: Read only multiples of 6 to stay aligned
  uint32_t bytes_to_read = (available_bytes / 6) * 6;
  uint32_t bytes_read = tud_audio_read(usb_rx_buf, bytes_to_read);
  
  uint32_t offset = 0;
  while (offset + 6 <= bytes_read) {
      audio_buffer_t *buffer = take_audio_buffer(ap, false);
      if (!buffer) break;

      uint32_t samples_to_process = (bytes_read - offset) / 6; 
      if (samples_to_process > buffer->max_sample_count) {
          samples_to_process = buffer->max_sample_count;
      }

      uint8_t *src = usb_rx_buf + offset;
      int32_t *dst = (int32_t *)(buffer->buffer->bytes);

      uint32_t vol_mult = get_linear_vol_log(volume[0]);
      bool is_muted = mute[0];

      for (uint32_t i = 0; i < samples_to_process; i++) {
          // RECONSTRUCTION HACK:
          // Windows sends 24-bit as: [L_low, L_mid, L_high, R_low, R_mid, R_high]
          // We must map these to the TOP of the 32-bit I2S word (bits 31-8)
          // AND ensure the bottom 8 bits are 0 to prevent "bit-shimmer" static.
          
          int32_t l_out = (int32_t)( (src[i*6+0] << 8) | (src[i*6+1] << 16) | (src[i*6+2] << 24) ) >> 8;
          int32_t r_out = (int32_t)( (src[i*6+3] << 8) | (src[i*6+4] << 16) | (src[i*6+5] << 24) ) >> 8;

          if (is_muted) {
              l_out = 0;
              r_out = 0;
          } else if (vol_mult < 65535) {
              l_out = (int32_t)(((int64_t)l_out * vol_mult) >> 16); 
              r_out = (int32_t)(((int64_t)r_out * vol_mult) >> 16);
          }
          // Channel Swap: USB Right -> I2S Left
          dst[i * 2 + 0] = r_out << 8; 
          dst[i * 2 + 1] = l_out << 8;
      }

      buffer->sample_count = samples_to_process;
      give_audio_buffer(ap, buffer);
      offset += (samples_to_process * 6);
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // Blink every interval ms
  if (board_millis() - start_ms < blink_interval_ms) return;
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state;
}

#if CFG_AUDIO_DEBUG
//--------------------------------------------------------------------+
// HID interface for audio debug
//--------------------------------------------------------------------+
// Every 1ms, we will sent 1 debug information report
void audio_debug_task(void) {
  static uint32_t start_ms = 0;
  uint32_t curr_ms = board_millis();
  if (start_ms == curr_ms) return;// not enough time
  start_ms = curr_ms;

  audio_debug_info_t debug_info;
  debug_info.sample_rate = current_sample_rate;
  debug_info.alt_settings = current_alt_settings;
  debug_info.fifo_size = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ;
  debug_info.fifo_count = fifo_count;
  debug_info.fifo_count_avg = (uint16_t) (fifo_count_avg >> 16);
  for (int i = 0; i < CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1; i++) {
    debug_info.mute[i] = mute[i];
    debug_info.volume[i] = volume[i];
  }

  if (tud_hid_ready())
    tud_hid_report(0, &debug_info, sizeof(debug_info));
}

// Invoked when received GET_REPORT control request
// Unused here
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
  // TODO not Implemented
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// Unused here
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  // This example doesn't use multiple report and report ID
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;
}
#endif
