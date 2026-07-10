
// 1. INCLUIR LA LIBRERÍA DE INFERENCIA EXPORTADA DE EDGE IMPULSE
#include <iot-sound-sensor_inferencing.h>

#include <driver/adc.h>

// ---------- Configuración de Hardware ----------
#define MIC_ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define BUTTON_PIN          18
#define LED_PIN             2
#define RED_LED             12
#define GREEN_LED           14

// ---------- Configuración del Modelo (Nativo de Edge Impulse) ----------
// EI_CLASSIFIER_RAW_SAMPLE_COUNT y EI_CLASSIFIER_FREQUENCY vienen en la librería (ej. 16000 y 16000Hz)
const uint32_t SAMPLE_PERIOD_US = 1000000UL / EI_CLASSIFIER_FREQUENCY;

// Buffer local donde guardaremos el segundo de audio
static int16_t audio_buffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];

// ---------- Variables de Debounce del Botón ----------
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 50;

// Declaración de funciones
void capturarAudio();
void ejecutarInferencia();

void setup() {
    // Volvemos a 115200 baudios ya que solo imprimiremos texto humano, no ráfagas de datos
    Serial.begin(115200);
    delay(500);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

    // Configuración del ADC (Exactamente igual que en la recolección de datos)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_11);

    Serial.println("\n=== Sistema de Control de Acceso por Audio Listo ===");
    Serial.println("Estado: [ESPERANDO] Presiona el botón para realizar el patrón de golpes...");
}

void loop() {
    bool currentButtonState = digitalRead(BUTTON_PIN);

    // Flanco de bajada (botón presionado)
    if (currentButtonState == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime) > DEBOUNCE_MS) {
        lastDebounceTime = millis();

        Serial.println("\nEstado: [ESCUCHANDO] Realiza el patrón ahora...");
        digitalWrite(LED_PIN, HIGH);   // Indicador visual de grabación activa
        
        capturarAudio();               // Graba 1 segundo de forma bloqueante
        
        digitalWrite(LED_PIN, LOW);    // Finaliza grabación
        Serial.println("Estado: [PROCESANDO] Analizando el patrón de sonido...");

        ejecutarInferencia();          // Corre la red neuronal localmente

        Serial.println("\nEstado: [ESPERANDO] Listo para una nueva validación.");
    }

    lastButtonState = currentButtonState;
}

// Captura el audio manteniendo la MISMA ESCALA con la que entrenaste
void capturarAudio() {
    for (uint32_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
        uint32_t t_inicio = micros();

        int raw = adc1_get_raw(MIC_ADC_CHANNEL);
        
        // Esta conversión a PCM de 16 bits debe ser idéntica a la del código de captura
        audio_buffer[i] = (int16_t)((raw - 2048) * 16);

        while ((uint32_t)(micros() - t_inicio) < SAMPLE_PERIOD_US) {
            // Espera activa para precisión microsegundo a microsegundo
        }
    }
}

    // 1. Definición del Callback (Debe estar arriba o fuera de ejecutarInferencia)
// Esta función le enseña a Edge Impulse cómo leer tu buffer de int16_t y convertirlo a float
int buscar_muestras_audio(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        // Convierte cada muestra int16_t a float (rango -1 a 1 o crudo, según prefiera el modelo)
        // Como ya escalamos en la captura, solo pasamos el valor a float
        out_ptr[i] = (float)audio_buffer[offset + i]; 
    }
    return 0;
}

// ... (El resto de tu loop y setup queda igual) ...

// Función encargada de alimentar al modelo y tomar la decisión
void ejecutarInferencia() {
    
    // CORRECCIÓN AQUÍ: Estructuramos la señal apuntando al Callback
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &buscar_muestras_audio; // Le asignamos la función puente

    // 2. Ejecutar el clasificador TinyML (Igual que antes)
    ei_impulse_result_t result = { 0 };
    int err = run_classifier(&signal, &result, false);
    if (err != 0) {
        Serial.printf("Error al ejecutar el clasificador (%d)\n", err);
        return;
    }

    // 3. Evaluar los resultados obtenidos
    bool patronDetectado = false;
    float nivelDeCerteza = 0.0;


    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (strcmp(result.classification[ix].label, "knock_knock") == 0) {
            nivelDeCerteza = result.classification[ix].value;
            if (nivelDeCerteza > 0.5) {
                patronDetectado = true;
            }
        }
    }

    Serial.print("Clase detectada: ");
    if(!patronDetectado) Serial.println("silence"); else Serial.println("knock");

    // 4. Veredicto final impreso por Serial
    Serial.println("--------------- VERDICTO ---------------");
    if (patronDetectado) {
        Serial.printf(" CONFIRMADO (Certeza: %.2f%%)\n", nivelDeCerteza * 100.0);
        Serial.println(" ACCESO CONCEDIDO: Permiso Otorgado.");
        
        digitalWrite(GREEN_LED, HIGH);
    } else {
        Serial.printf(" NO RECONOCIDO (Certeza del patrón: %.2f%%)\n", nivelDeCerteza * 100.0);
        Serial.println(" ACCESO DENEGADO: Intenta de nuevo.");

        digitalWrite(RED_LED, HIGH);
    }
    Serial.println("----------------------------------------");

    delay(2000);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
}
