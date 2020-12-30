/*
MIT License

Copyright (c) 2020 SienkLogic, LLC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "BluetoothA2DPSink.h"
#include <arduinoFFT.h> 
#include <FastLED.h>
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"
#include "AudioFileSourcePROGMEM.h"
#include "sounds.h"
#include "icons.h"

// Audio Settings
#define I2S_DOUT      25
#define I2S_BCLK      26
#define I2S_LRC       22
#define MODE_PIN      33

#define NUM_LEDS 64
#define LED_DATA_PIN 14
#define LED_CLOCK_PIN 15

// FFT Settings
#define NUM_BANDS  8
#define SAMPLES 512              
#define SAMPLING_FREQUENCY 44100 

#define BRIGHTNESS 100
#define DEVICE_NAME "Sienk64"

arduinoFFT FFT = arduinoFFT();
BluetoothA2DPSink a2dp_sink;
CRGB leds[NUM_LEDS];

int pushButton = 39;

float amplitude = 200.0;

int32_t peak[] = {0, 0, 0, 0, 0, 0, 0, 0};
double vReal[SAMPLES];
double vImag[SAMPLES];

double brightness = 0.75;

QueueHandle_t queue;

int16_t sample_l_int;
int16_t sample_r_int;

uint32_t animationCounter = 0;

int visualizationCounter = 0;
int32_t lastVisualizationUpdate = 0;

static const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
};

uint8_t hueOffset = 0;

bool hasDevicePlayedAudio = false;

uint8_t getLedIndex(uint8_t x, uint8_t y) {
  //x = 7 - x;
  // if (y % 2 == 0) {
    return y * 8 + x;
  // } else {
    //  return y*8 + (7 - x);
  // }
}

CHSV colorize(int i){
  return CHSV((128+i * 16)%256, 255, 255);
}

EOrder rgb_order = RGB;

CRGB colorize(uint8_t r, uint8_t g, uint8_t b) {
  switch(rgb_order) {
    default:
    case RGB: return CRGB(r,g,b);
    case RBG: return CRGB(r,b,g);
    case BGR: return CRGB(b,g,r);
    case BRG: return CRGB(b,r,g);
    case GBR: return CRGB(g,b,r);
    case GRB: return CRGB(g,r,b);
  }
}

void createBands(int i, int dsize) {
  uint8_t band = 0;
  if (i <= 2) {
    band =  0; // 125Hz
  } else if (i <= 5) {
    band =   1; // 250Hz
  } else if (i <= 7)  {
    band =  2; // 500Hz
  } else if (i <= 15) {
    band =  3; // 1000Hz
  } else if (i <= 30) {
    band =  4; // 2000Hz
  } else if (i <= 53) {
    band =  5; // 4000Hz
  } else if (i <= 106) {
    band =  6;// 8000Hz
  } else {
    band = 7;
  }
  int dmax = amplitude;
  if (dsize > dmax)
    dsize = dmax;
  if (dsize > peak[band])
  {
    peak[band] = dsize;
  }
}

void renderFFT(void * parameter){
  int item = 0;
  for(;;) {
    if (uxQueueMessagesWaiting(queue) > 0) {

      FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
      FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);

      for (uint8_t band = 0; band < NUM_BANDS; band++) {
        peak[band] = 0;
      }

      for (int i = 2; i < (SAMPLES / 2); i++) { // Don't use sample 0 and only first SAMPLES/2 are usable. Each array eleement represents a frequency and its value the amplitude.
        if (vReal[i] > 2000) { // Add a crude noise filter, 10 x amplitude or more
          createBands(i, (int)vReal[i] / amplitude);
        }
      }

      // Release handle
      xQueueReceive(queue, &item, 0);

      uint8_t intensity;
      
      FastLED.clear();
      FastLED.setBrightness(BRIGHTNESS);
      for (byte band = 0; band < NUM_BANDS; band++) {
        intensity = map(peak[band], 1, amplitude, 0, 8);

        for (int i = 0; i < 8; i++) {
          leds[getLedIndex(7 - i, 7 - band)] = (i >= intensity) ? CHSV(0, 0, 0) : colorize(i);
        }
      }

      FastLED.show();

      if ((millis() - lastVisualizationUpdate) > 1000) {
        log_e("Fps: %f", visualizationCounter / ((millis() - lastVisualizationUpdate) / 1000.0));
        visualizationCounter = 0;
        lastVisualizationUpdate = millis();
        hueOffset += 5;
      }
      visualizationCounter++;
    }
  }
}

void drawIcon(const uint32_t *icon) {
  animationCounter++;

  switch(animationCounter % 10000) {
    case 1000: rgb_order = RGB; break;
    case 2000: rgb_order = RBG; break;
    case 3000: rgb_order = BGR; break;
    case 4000: rgb_order = BRG; break;
    case 5000: rgb_order = GRB; break;
    case 6000: rgb_order = GBR; break;
    case 7000: rgb_order = RGB; break;
  }

  uint8_t brightness = 0;
  for (int i = 0; i < 64; i++) {
    uint32_t pixel = pgm_read_dword(icon + i);
    uint8_t red = (pixel >> 16) & 0xFF;
    uint8_t green = (pixel >> 8) & 0xFF;
    uint8_t blue = pixel & 0xFF;
    log_v("%d. %08X, %02X %02X %02X", i, pixel, red, green, blue);
    brightness = 30 + (sin(animationCounter / 100.0) * 30);
    FastLED.setBrightness(  brightness );
    leds[getLedIndex(i % 8, i / 8)] = colorize(red, green, blue);
  }
  delay(1);
  FastLED.show();
}

void audio_data_callback(const uint8_t *data, uint32_t len) {
  int item = 0;
  // Only prepare new samples if the queue is empty
  if (uxQueueMessagesWaiting(queue) == 0) {
    //log_e("Queue is empty, adding new item");
    int byteOffset = 0;
    for (int i = 0; i < SAMPLES; i++) {
      sample_l_int = (int16_t)(((*(data + byteOffset + 1) << 8) | *(data + byteOffset)));
      sample_r_int = (int16_t)(((*(data + byteOffset + 3) << 8) | *(data + byteOffset +2)));
      vReal[i] = (sample_l_int + sample_r_int) / 2.0f;;
      vImag[i] = 0;
      byteOffset = byteOffset + 4;
    }

    // Tell the task in core 1 that the processing can start
    xQueueSend(queue, &item, portMAX_DELAY);
  }
  // pass the data to the i2s sink
  a2dp_sink.audio_data_callback(data, len);
}

void playBootupSound() {
  AudioFileSourcePROGMEM *in = new AudioFileSourcePROGMEM(sound, sizeof(sound));
  AudioGeneratorAAC *aac = new AudioGeneratorAAC();
  AudioOutputI2S *out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT );

  aac->begin(in, out);

  while (aac->isRunning()) {
    drawIcon(HEART2);
    aac->loop();
  }
  aac->stop();
}

void setup() {
    pinMode(MODE_PIN, OUTPUT);
    pinMode(pushButton, INPUT);
    digitalWrite(MODE_PIN, HIGH);

    FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLOCK_PIN>(leds, NUM_LEDS);
    playBootupSound();

    // The queue is used for communication between A2DP callback and the FFT processor
    queue = xQueueCreate( 1, sizeof( int ) );
    if(queue == NULL){
      Serial.println("Error creating the queue");
    }

    // This task will process the data acquired by the 
    // Bluetooth audio stream
    xTaskCreatePinnedToCore(
      renderFFT,          // Function that should be called
      "FFT Renderer",     // Name of the task (for debugging)
      10000,              // Stack size (bytes)
      NULL,               // Parameter to pass
      1,                  // Task priority
      NULL,               // Task handle
      1                   // Core you want to run the task on (0 or 1)
    );

    a2dp_sink.set_pin_config(pin_config);
    a2dp_sink.start((char*) DEVICE_NAME);
    // redirecting audio data to do FFT
    esp_a2d_sink_register_data_callback(audio_data_callback);

}


void loop() {
  esp_a2d_audio_state_t state = a2dp_sink.get_audio_state();
  switch(state) {
      case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND: 
        if (hasDevicePlayedAudio) {
          drawIcon(HEART2);
        } else {
          drawIcon(HEART2);
        }
        break;
      case ESP_A2D_AUDIO_STATE_STOPPED:
        drawIcon(BLE); 
        break;
      case ESP_A2D_AUDIO_STATE_STARTED:
        hasDevicePlayedAudio = true;
        break;
  }

}