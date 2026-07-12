#include <iot-sound-sensor_inferencing.h> // EDGE IMPULSE LIBRARY

#include <driver/adc.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ---------- HARDWARE CONFIG ----------
#define ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define BUTTON_PIN          18
#define LED_PIN             2
#define RED_LED             12
#define GREEN_LED           14


// Configuration of sample period based on Hz received in EI library
const uint32_t SAMPLE_PERIOD_US = 1000000UL / EI_CLASSIFIER_FREQUENCY;

// Local buffer to store recorded audio
static int16_t audio_buffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];

// Button variables
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 50;

// --- CREDENTIAL VARIABLES (WiFi, MQTT) ---
// Credential buffers 
char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];

const char* WIFISSID     = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER   = mqttsv_buffer;
const int MQTTPORT = 1883;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// MQTT Topic
const char* TOPIC_ACCESS = "iot/access";

// --- FUNCTION DECLARATIONS ---
void capturarAudio();
void ejecutarInferencia();
bool loadCredentials();
void reconnect();


void setup() {
    Serial.begin(115200);
    delay(500);

    // --- PIN CONFIG ---
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);

    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);

    // --- WiFi Config ---
    if(loadCredentials()){
        Serial.print("{[ENV] Connecting to WiFi: ");
        Serial.println(WIFISSID);
        
        WiFi.begin(WIFISSID, WIFIPASSWORD);
        
        unsigned long lastBlinkTime = 0;
        bool ledState = LOW;
        
        while (WiFi.status() != WL_CONNECTED) {
            unsigned long currentMillis = millis();

            // LED twinkle every 500ms
            if (currentMillis - lastBlinkTime >= 500) {
                lastBlinkTime = currentMillis;
                ledState = !ledState;
                digitalWrite(LED_PIN, ledState);
                Serial.print("."); 
            }
            yield(); 
        }
        digitalWrite(LED_PIN, LOW);
    }

    Serial.printf("\n[WiFi OK] IP: %s\n", WiFi.localIP().toString().c_str());
    WiFi.setSleep(false);

    Serial.println("[ENV] Configuring Mosquitto...");
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(MQTTSERVER, MQTTPORT);

    Serial.println("\n=== Audio Control READY ===");

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

    Serial.println("[ENV OK] Credentials loaded successfully.");
    return true;
}



void reconnect() {
    /* Function to reconnect to Mosquitto */

    unsigned long lastBlinkTime = 0;
    unsigned long lastRetryTime = 0;
    bool ledState = LOW;
    bool firstAttempt = true;

    while (!mqttClient.connected()) {
        unsigned long currentMillis = millis();

        // LED twinkles every 500ms
        if (currentMillis - lastBlinkTime >= 500) {
        lastBlinkTime = currentMillis;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
        }

        // Connect directly if first time, every 5s if not
        if (firstAttempt || (currentMillis - lastRetryTime >= 5000)) {
            firstAttempt = false;
            lastRetryTime = currentMillis;
            
            Serial.print("Connecting MQTT... ");
            if (mqttClient.connect("ESP32Client")) {
                Serial.println("[ENV OK] MQTT Connected");
                digitalWrite(LED_PIN, LOW); 
            } else {
                Serial.printf("[FAILED] state=%d, retrying in 5s...\n", mqttClient.state());
            }
        }
        
        mqttClient.loop(); 
        yield();
    }
}



void captureAudio() {
    /* Captures and converts the audio to fit the model*/

    for (uint32_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
        uint32_t startTime = micros();

        int raw = adc1_get_raw(ADC_CHANNEL);
        
        // Conversion to 16 PCM
        audio_buffer[i] = (int16_t)((raw - 2047) * 16);

        while ((uint32_t)(micros() - startTime) < SAMPLE_PERIOD_US) {
            // Microsecond active waiting
        }
    }
}


int fetchAudioSamples(size_t offset, size_t length, float *out_ptr) {
    /* Converts int16_t buffer into float buffer*/
    
    for (size_t i = 0; i < length; i++) {
        // Converts every sample into float 
        out_ptr[i] = (float)audio_buffer[offset + i]; 
    }
    return 0;
}



void runInference() {
    /* Runs the model, classificates and decides */
    
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &fetchAudioSamples; // Bridge function


    ei_impulse_result_t result = { 0 };
    int err = run_classifier(&signal, &result, false);
    if (err != 0) {
        Serial.printf("Error al ejecutar el clasificador (%d)\n", err);
        return;
    }

    bool patternDetected = false;
    float maxConfidence = -1.0;
    float patternConfidence = 0.0;
    const char* detectedClass;


    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {

        const char* label = result.classification[i].label;
        float confidenceLevel = result.classification[i].value;
        Serial.print(label);
        Serial.printf(" = %.2f ", confidenceLevel);

        if (strcmp(label, "knock_knock") == 0) {
            patternConfidence = confidenceLevel;
        }
        if(confidenceLevel > maxConfidence){
            detectedClass = label;
            maxConfidence = confidenceLevel;
        }
    }

    if(patternConfidence < maxConfidence) patternDetected = false;
    else patternDetected = true;

    Serial.print("DETECTED CLASS = ");
    Serial.print(detectedClass);
    Serial.printf(" with %.2f%%\n", maxConfidence * 100);
    Serial.printf("Knock Knock with %.2f%%\n", patternConfidence * 100);
    

    printTelemetry(detectedClass, maxConfidence, patternDetected);
}

void printTelemetry(const char* detectedClass, float maxConfidence, bool patternDetected){

    Serial.println("--------------- RESULT ---------------");
    Serial.print("DETECTED CLASS: ");
    Serial.println(detectedClass);
    char msg[100];
    if (patternDetected && maxConfidence > 0.65) {
        Serial.printf(" Confirmed with (Certeza: %.2f%%)\n", maxConfidence * 100.0);
        Serial.println(" --- CONCEDED ACCESS ---");
        
        digitalWrite(GREEN_LED, HIGH);
        
    } else {
        Serial.printf(" NOT RECOGNIZED (Pattern confidence: %.2f%%)\n", maxConfidence * 100.0);
        Serial.println(" --- DENIED ACCESS --- ");

        digitalWrite(RED_LED, HIGH);
    }

    sprintf(msg, "{\"access\": %d}", patternDetected);
    if(mqttClient.publish("iot/access", msg)){
        Serial.println("[ÉXITO] Message sent successfully");
    } else {
        Serial.println("[ERROR] Couldn't send the message");
    }

    Serial.println("----------------------------------------");

    delay(2000);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
}


void loop() {

    if (!mqttClient.connected()) {
        reconnect();
    }
    mqttClient.loop();

    bool currentButtonState = digitalRead(BUTTON_PIN);

    // Button pressing
    if(
        currentButtonState == LOW && lastButtonState == HIGH &&        
        (millis() - lastDebounceTime) > DEBOUNCE_MS) {

        lastDebounceTime = millis();

        Serial.println("\n[LISTENING] Produce the sound pattern NOW");
        digitalWrite(LED_PIN, HIGH);   

        /* Audio recording */
        captureAudio();               
        digitalWrite(LED_PIN, LOW);    
        Serial.println("[PROCESSING] Analyzing sound pattern...");
        runInference();        
        Serial.println("\n[WAITING] Ready for a new pattern (press the button)...");
    }

    lastButtonState = currentButtonState;
}



