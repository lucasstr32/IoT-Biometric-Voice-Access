/*
  Captura de audio con KY038 + ESP32 para Edge Impulse
  ------------------------------------------------------
  - Al presionar el boton, captura 1 segundo de audio (16000 muestras
    a 16000 Hz) desde la salida analogica (AO) del KY038.
  - El resultado queda en un buffer int16_t listo para:
      a) imprimirse por Serial (util ahora para verificar la captura
         y, si queres, usar esos valores como dato de entrenamiento), o
      b) pasarse directamente a la libreria de inferencia de Edge
         Impulse una vez que la tengas exportada (ver seccion
         "INFERENCIA" mas abajo, dentro de loop()).

  Conexiones:
    KY038 AO  -> GPIO34 (ADC1, input-only, recomendado)
    KY038 VCC -> 3.3V
    KY038 GND -> GND
    Boton     -> entre GPIO4 y GND (usa pull-up interno, no necesita
                 resistencia externa)

  Notas importantes:
  - Se usa el driver ADC de bajo nivel (adc1_get_raw) en vez de
    analogRead() porque es mas rapido y permite mantener el muestreo
    a 16 kHz de forma estable.
  - El muestreo es bloqueante (busy-wait) durante 1 segundo. Esto es
    intencional para mantener el periodo de 62.5 us constante; no se
    debe agregar codigo dentro de capturarAudio() que tome tiempo
    variable (Serial.print, delay, etc).
  - Usa el mismo pin, atenuacion y escala de conversion tanto para
    grabar datos de entrenamiento en Edge Impulse como para la
    inferencia final. Si cambias algo aca, el modelo entrenado puede
    dejar de funcionar bien.
*/

#include <driver/adc.h>

// ---------- Configuracion ----------
#define SAMPLE_RATE_HZ      16000
#define CAPTURE_SECONDS     1
#define BUFFER_SIZE         (SAMPLE_RATE_HZ * CAPTURE_SECONDS)  // 16000

#define MIC_ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define BUTTON_PIN          18

const uint32_t SAMPLE_PERIOD_US = 1000000UL / SAMPLE_RATE_HZ; // ~62.5 us

// ---------- Buffer de audio ----------
static int16_t audio_buffer[BUFFER_SIZE];

// ---------- Estado del boton (debounce simple) ----------
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 50;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Configuracion del ADC para lectura rapida y consistente
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_11);

  Serial.println("Listo. Presiona el boton para capturar 1 segundo de audio.");
}

void loop() {
  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Detecta flanco de bajada (boton presionado) con debounce
  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (millis() - lastDebounceTime) > DEBOUNCE_MS) {

    lastDebounceTime = millis();
    Serial.println("Capturando audio...");

    capturarAudio();

    Serial.println("Captura finalizada.");
    enviarPorSerial();   // util ahora, para verificar la captura / debug

    // -------- INFERENCIA (cuando tengas la libreria de Edge Impulse) --------
    // 1. Agrega el include de tu libreria exportada, por ejemplo:
    //      #include <nombre_de_tu_proyecto_inferencing.h>
    // 2. Verifica que EI_CLASSIFIER_RAW_SAMPLE_COUNT y
    //      EI_CLASSIFIER_FREQUENCY coincidan con BUFFER_SIZE y
    //      SAMPLE_RATE_HZ definidos arriba.
    // 3. Despues de capturarAudio(), agrega algo como:
    //
    //      signal_t signal;
    //      signal.total_length = BUFFER_SIZE;
    //      signal.get_data = &get_audio_signal_data;
    //
    //      ei_impulse_result_t result = { 0 };
    //      EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
    //
    //      for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    //          if (result.classification[ix].value > 0.8) {
    //              Serial.print("Detectado: ");
    //              Serial.println(result.classification[ix].label);
    //          }
    //      }
    //
    //    (la funcion get_audio_signal_data ya esta definida mas abajo,
    //    no hace falta tocarla)
    // --------------------------------------------------------------------
  }

  lastButtonState = currentButtonState;
}

// Captura BUFFER_SIZE muestras a SAMPLE_RATE_HZ de forma bloqueante,
// usando micros() para mantener el periodo de muestreo constante.
void capturarAudio() {
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    uint32_t t_inicio = micros();

    int raw = adc1_get_raw(MIC_ADC_CHANNEL);   // 0 - 4095 (12 bits)

    // Centra la señal en 0 y la escala a un rango similar a PCM de 16
    // bits. Importante: usar SIEMPRE esta misma escala al grabar datos
    // de entrenamiento y al correr la inferencia.
    audio_buffer[i] = (int16_t)((raw - 2048) * 16);

    // Espera activa hasta completar el periodo de muestreo (~62.5 us)
    while ((uint32_t)(micros() - t_inicio) < SAMPLE_PERIOD_US) {
      // espera
    }
  }
}

// Imprime el buffer capturado por Serial en formato CSV (una linea).
// Sirve para verificar que la captura funciona y ver la forma de la
// señal (por ejemplo con el Serial Plotter del Arduino IDE).
void enviarPorSerial() {
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    Serial.print(audio_buffer[i]);
    if (i < BUFFER_SIZE - 1) Serial.print(",");
  }
  Serial.println();
}

// -------- Funcion de acceso al buffer para la libreria de Edge Impulse --------
// Edge Impulse espera una funcion con esta firma para leer el buffer de
// audio. Queda lista para usarse en cuanto agregues la libreria exportada.
int get_audio_signal_data(size_t offset, size_t length, float *out_ptr) {
  for (size_t i = 0; i < length; i++) {
    out_ptr[i] = (float)audio_buffer[offset + i];
  }
  return 0;
}
