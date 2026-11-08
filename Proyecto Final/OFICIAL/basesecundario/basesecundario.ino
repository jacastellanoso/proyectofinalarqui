/*
 ARQUITECTURA DEL COMPUTADOR - ARDUINO SECUNDARIO
 Cuatro LED de estado, receptor recuperable.

 Principal D5 -> Secundario D2 (ENTRADA DE CONTROL)
 Principal GND -> Secundario GND
 Secundario D3 -> resistencia 330 ohm -> anodo LED AMARILLO
 Secundario D4 -> resistencia 330 ohm -> anodo LED VERDE
 Secundario D5 -> resistencia 330 ohm -> anodo LED ROJO
 Secundario D6 -> resistencia 330 ohm -> anodo LED AZUL
 Catodos (patas cortas) -> GND

 PULSO LOW RECIBIDO: 100 ms amarillo, 200 ms verde,
 300 ms rojo, 400 ms azul, 500 ms apagar todos.
 El emisor repite el estado. Al perder comunicacion prolongadamente
 todos los LED se apagan para evitar indicadores falsos.
*/

const byte PIN_SENAL = 2;
const byte LED_AMARILLO = 3;
const byte LED_VERDE = 4;
const byte LED_ROJO = 5;
const byte LED_AZUL = 6;

bool lecturaAnterior = HIGH;
unsigned long inicioNivelBajo = 0;
unsigned long ultimaTramaValida = 0;
bool huboTramaValida = false;
byte ledActivo = 0;
unsigned long ultimoParpadeoAzul = 0;
bool azulEncendido = false;

void apagarTodos() {
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_AZUL, LOW);
}

void elegirLed(byte led) {
  // Las retransmisiones del mismo estado no reinician el parpadeo.
  if (led == ledActivo) return;
  ledActivo = led;
  apagarTodos();
  azulEncendido = false;
  ultimoParpadeoAzul = millis();
  switch (led) {
    case 5: digitalWrite(LED_AMARILLO, HIGH); break;
    case 6: digitalWrite(LED_VERDE, HIGH); break;
    case 7: digitalWrite(LED_ROJO, HIGH); break;
    case 8:
      azulEncendido = true;
      digitalWrite(LED_AZUL, HIGH);
      break;
    default: break;
  }
}

void setup() {
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  apagarTodos();
  pinMode(PIN_SENAL, INPUT_PULLUP);
  lecturaAnterior = digitalRead(PIN_SENAL);
  // Si inicia con la linea LOW, se espera al proximo pulso completo.
  if (lecturaAnterior == LOW) inicioNivelBajo = micros();
}

void loop() {
  bool lectura = digitalRead(PIN_SENAL);
  unsigned long ahora = micros();

  if (lecturaAnterior == HIGH && lectura == LOW) {
    inicioNivelBajo = ahora;
  }
  else if (lecturaAnterior == LOW && lectura == HIGH) {
    unsigned long ancho = ahora - inicioNivelBajo;
    byte codigo = 255;
    if      (ancho >= 75000UL  && ancho <= 125000UL) codigo = 5;
    else if (ancho >= 175000UL && ancho <= 225000UL) codigo = 6;
    else if (ancho >= 275000UL && ancho <= 325000UL) codigo = 7;
    else if (ancho >= 375000UL && ancho <= 425000UL) codigo = 8;
    else if (ancho >= 475000UL && ancho <= 525000UL) codigo = 0;

    if (codigo != 255) {
      elegirLed(codigo);
      huboTramaValida = true;
      ultimaTramaValida = millis();
    }
  }
  lecturaAnterior = lectura;

  // LED azul intermitente SOLO durante la alarma (250 ms encendido,
  // 250 ms apagado). No bloquea la recepcion de comandos.
  if (ledActivo == 8 && millis() - ultimoParpadeoAzul >= 250UL) {
    ultimoParpadeoAzul = millis();
    azulEncendido = !azulEncendido;
    digitalWrite(LED_AZUL, azulEncendido ? HIGH : LOW);
  }

  // Se conservan los LED durante las pausas normales y animaciones.
  // Si no hay ninguna trama valida por diez segundos, apagar indicadores.
  if (huboTramaValida && millis() - ultimaTramaValida > 10000UL) {
    elegirLed(0);
    huboTramaValida = false;
  }
}
