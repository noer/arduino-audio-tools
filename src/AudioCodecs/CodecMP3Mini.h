#pragma once

#define MINIMP3_NO_STDIO
//#define MINIMP3_NO_SIMD
//#define MINIMP3_IMPLEMENTATION
//#define MINIMP3_ONLY_MP3  
//#define MINIMP3_FLOAT_OUTPUT

#ifndef MINIMP3_MAX_SAMPLE_RATE
#define MINIMP3_MAX_SAMPLE_RATE 44100
#endif

#include "AudioCodecs/AudioEncoded.h"
#include "minimp3.h"

namespace audio_tools {

/**
 * @brief MP3 Decoder using https://github.com/pschatzmann/minimp3.
 * This decoder does not provide any good results and it is not suited to decode any audio above 32000 on an ESP32. So the
 * sample rate is limited by the MINIMP3_MAX_SAMPLE_RATE variable.
 * 
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class MP3DecoderMini : public AudioDecoder {
 public:
  MP3DecoderMini() = default;

  /// Destroy the MP3DecoderMini object
  ~MP3DecoderMini() {
    if (active) {
      end();
    }
  }

  void setBufferLength(int len) { buffer_size = len; }

  void setNotifyAudioChange(AudioBaseInfoDependent &bi) {
    audioBaseInfoSupport = &bi;
  }

  /// Starts the processing
  void begin() {
    LOGD(LOG_METHOD);
    esp_task_wdt_delete(nullptr);
    mp3dec_init(&mp3d);
    buffer.resize(buffer_size);
    pcm.resize(MINIMP3_MAX_SAMPLES_PER_FRAME);
    buffer_pos = 0;
    active = true;
  }

  /// Releases the reserved memory
  void end() {
    LOGD(LOG_METHOD);
    flush();
    active = false;
  }

  /// Defines the output Stream
  void setOutputStream(Print &outStream) { this->out = &outStream; }

  /// Provides the last available MP3FrameInfo
  AudioBaseInfo audioInfo() { return audio_info; }

  /// Write mp3 data to decoder
  size_t write(const void *data, size_t len) {
    LOGD("write: %zu", len);
    if (active) {
      if (buffer_pos+len>=buffer.size()){
        decode(len);
      }
      assert(buffer_pos+len<buffer.size());   
      memcpy(buffer.data()+buffer_pos, data, len);
      buffer_pos += len;   
    }
    return len;
  }

  /// Decodes the last outstanding data
  void flush() {
    // decode the full buffer
    decode(0);
    buffer_pos = 0;
  }

  /// checks if the class is active
  virtual operator boolean() { return active; }

  void setSampleRateLimit(int limit){
    sample_rate_limit = limit;
  }

 protected:
  AudioBaseInfo audio_info;
  AudioBaseInfoDependent *audioBaseInfoSupport = nullptr;
  Print *out = nullptr;
  mp3dec_t mp3d;
  mp3dec_frame_info_t mp3dec_info;
  size_t buffer_size = 5 * 1024;
  size_t buffer_pos = 0;
  Vector<uint8_t> buffer;
  Vector<mp3d_sample_t> pcm;
  #ifdef MINIMP3_FLOAT_OUTPUT
  Vector<int16_t> pcm16;
  #endif
  bool active;
  int sample_rate_limit = MINIMP3_MAX_SAMPLE_RATE; //32000;

  /// Process single bytes so that we can decode a full frame when it is available
  void decode(int write_len) {
    LOGD("decode: %d ", buffer_pos);
    int open = buffer_pos;
    int processed = 0;
    int samples;
    do {
      // decode data
      samples = mp3dec_decode_frame(&mp3d, buffer.data()+processed, open,
                                        pcm.data(), &mp3dec_info);
      LOGD("frame_offset: %d - frame_bytes: %d -> samples %d", mp3dec_info.frame_offset, mp3dec_info.frame_bytes, samples);
      open -= mp3dec_info.frame_bytes;
      processed += mp3dec_info.frame_bytes;
      // output decoding result
      if (samples > 0) {
        provideResult(samples);
      }            
      // process until we have space for the next write
    } while(processed < write_len);

    // save unprocessed data
    buffer_pos = open;
    memmove(buffer.data(),buffer.data()+processed, open);
  }

  /// Provides Metadata and PCM data
  void provideResult(int samples) {
    LOGD("provideResult: %d samples", samples);
    AudioBaseInfo info;
    info.sample_rate = mp3dec_info.hz>sample_rate_limit ? sample_rate_limit : mp3dec_info.hz;
    info.channels = mp3dec_info.channels;
    info.bits_per_sample = 16;

    // notify about audio changes
    if (audioBaseInfoSupport != nullptr && info != audio_info) {
      info.logInfo();
      audioBaseInfoSupport->setAudioInfo(info);
    }
    // store last audio_info so that we can detect any changes
    audio_info = info;

    // provide result pwm data
    if (out != nullptr) {
      #ifdef MINIMP3_FLOAT_OUTPUT
        pcm16.resize(samples);
        f32_to_s16(pcm.data(), pcm16.data(), samples);
        out->write((uint8_t *)pcm16.data(), samples * sizeof(int16_t));
      #else
        out->write((uint8_t *)pcm.data(), samples * sizeof(mp3d_sample_t));
      #endif
    }
  }

  void f32_to_s16(float *in, int16_t *out, int num_samples) {
      int i = 0;
      for(; i < num_samples; i++){
          float sample = in[i] * 32768.0f;
          if (sample >=  32766.5)
              out[i] = (int16_t) 32767;
          else if (sample <= -32767.5)
              out[i] = (int16_t)-32768;
          else {
              int16_t s = (int16_t)(sample + .5f);
              s -= (s < 0);   /* away from zero, to be compliant */
              out[i] = s;
          }
      }
  }


};

}  // namespace audio_tools
