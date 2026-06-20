const int pinAO = 34;       // Salida analógica del sensor
const int pinDO = 33;       // Salida digital del sensor (gate)


void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(pinDO, INPUT_PULLDOWN);

  Serial.println("Iniciando monitoreo de sonido...");
}

void loop() {
  int valorAnalogico = analogRead(pinAO);
  int valorDigital = digitalRead(pinDO);

  bool sonidoDetectado = valorAnalogico > umbral;

  Serial.print("AO: ");
  Serial.print(valorAnalogico);
  Serial.print("\tDO: ");
  Serial.print(valorDigital);
  Serial.print("\tLED: ");
  Serial.println(sonidoDetectado ? "ON" : "OFF");

  delay(100);
}