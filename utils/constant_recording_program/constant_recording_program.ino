#include <driver/adc.h>

#define SAMPLE_RATE_HZ      6000
#define MIC_ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define SAMPLE_PERIOD_US    (1000000UL / SAMPLE_RATE_HZ)

void setup() {
  // Baud rate alto para flujo constante de 16kHz
  Serial.begin(921600); 
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_11);
}

void loop() {
  uint32_t t_inicio = micros();

  // 1. Lectura inmediata del ADC
  int raw = adc1_get_raw(MIC_ADC_CHANNEL);
  
  // 2. Inversión y centrado optimizado en una sola línea (Súper rápido)
  // Reemplaza tu línea anterior por esta:
  int16_t sample = (int16_t)((2047 - raw) * 16);

  // 3. Envío directo al Data Forwarder
  Serial.println(sample);

  // 4. Control de tiempo estricto
  while ((uint32_t)(micros() - t_inicio) < SAMPLE_PERIOD_US) {
    // espera activa
  }
}