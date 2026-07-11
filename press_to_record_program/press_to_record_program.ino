
// 1. INCLUIR LA LIBRERÍA DE INFERENCIA EXPORTADA DE EDGE IMPULSE
#include <iot-sound-sensor_inferencing.h>

#include <driver/adc.h>

#include <PubSubClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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

// --- Credential buffers ---
char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];

const char* WIFISSID     = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER   = mqttsv_buffer;
const int MQTTPORT = 1883;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// TOPICOS DE MQTT
const char* TOPIC_ACCESS = "iot/access";

// Declaración de funciones
void capturarAudio();
void ejecutarInferencia();

void setup() {
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

    if(loadCredentials()){
        // Conexión WiFi
        Serial.print("Conectando a la red WiFi: ");
        Serial.println(WIFISSID);
        
        WiFi.begin(WIFISSID, WIFIPASSWORD);
        while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print("."); // Efecto visual de carga
        }
    }
    Serial.printf("\n[WiFi OK] IP: %s\n", WiFi.localIP().toString().c_str());
    WiFi.setSleep(false);

    Serial.println("[ENV] Configurando entorno estándar para Mosquitto Local...");
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(MQTTSERVER, MQTTPORT);

    Serial.println("\n=== Sistema de Control de Acceso por Audio Listo ===");
    Serial.println("Estado: [ESPERANDO] Presiona el botón para realizar el patrón de golpes...");

    reconnect();
}



bool loadCredentials() {
    /* Function that loads WiFi and MQTT credentials from a JSON*/

    /* Initialization of LittleFS */
    if (!LittleFS.begin(true)) { 
        Serial.println("[ERROR] LittleFS could not be initialized"); 
        return false; 
    }

    /* Opening credential file */
    File f = LittleFS.open("/config.json", "r");
    if (!f) { 
        Serial.println("[ERROR] config.json not found");
        return false; 
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) { 
        Serial.println("[ERROR] invalid JSON"); 
        return false; 
    }

    /* Parsing of credentials */
    strlcpy(wifissid_buffer,    doc["ssid"]          | "", sizeof(wifissid_buffer));
    strlcpy(wifipass_buffer,    doc["password"]       | "", sizeof(wifipass_buffer));
    strlcpy(mqttsv_buffer, doc["mosquitto_server"] , sizeof(mqttsv_buffer));

    Serial.println("[OK] Credentials loaded successfully.");
    return true;
}


// Función para conectar/reconectar a MQTT
void reconnect() {
  /* Function that connects to MQTT broker */

  while (!mqttClient.connected()) {
    Serial.print("Connecting MQTT... ");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("[OK] MQTT Connected");
    } else {
      Serial.printf("[FAILED] state=%d, retrying...\n", mqttClient.state());
      delay(5000);
    }
  }
}


void loop() {

    if (!mqttClient.connected()) {
        reconnect();
    }
    mqttClient.loop();

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
    float maxCerteza = -1.0;
    float certezaPatronCorrecto = 0.0;
    const char* claseDetectada;


    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {

        const char* label = result.classification[i].label;
        float nivelDeCerteza = result.classification[i].value;
        Serial.print(label);
        Serial.printf(" = %.2f ", nivelDeCerteza);

        if (strcmp(label, "knock_knock") == 0) {
            certezaPatronCorrecto = nivelDeCerteza;
        }
        if(nivelDeCerteza > maxCerteza){
            claseDetectada = label;
            maxCerteza = nivelDeCerteza;
        }
    }

    if(certezaPatronCorrecto < maxCerteza) patronDetectado = false;
    else patronDetectado = true;

    Serial.print("Clase detectada = ");
    Serial.print(claseDetectada);
    Serial.printf(" con %.2f%%\n", maxCerteza * 100);
    Serial.printf("Knock Knock con %.2f%%\n", certezaPatronCorrecto * 100);
    



    // 4. Veredicto final impreso por Serial y enviado por MQTT
    Serial.println("--------------- VERDICTO ---------------");
    Serial.print("CLASE RECONOCIDA: ");
    Serial.println(claseDetectada);
    char msg[100];
    if (patronDetectado) {
        Serial.printf(" CONFIRMADO (Certeza: %.2f%%)\n", maxCerteza * 100.0);
        Serial.println(" ACCESO CONCEDIDO: Permiso Otorgado.");
        
        digitalWrite(GREEN_LED, HIGH);
        
    } else {
        Serial.printf(" NO RECONOCIDO (Certeza del patrón: %.2f%%)\n", maxCerteza * 100.0);
        Serial.println(" ACCESO DENEGADO: Intenta de nuevo.");

        digitalWrite(RED_LED, HIGH);
    }

    sprintf(msg, "{\"access\": %d}", patronDetectado);
    if(mqttClient.publish("iot/access", msg)){
        Serial.println("[ÉXITO] Mensaje enviado correctamente");
    } else {
        Serial.println("[ERROR] No se pudo enviar el mensaje");
    }

    Serial.println("----------------------------------------");

    delay(2000);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
}
