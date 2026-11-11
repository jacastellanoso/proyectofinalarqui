/*
 ARQUITECTURA DEL COMPUTADOR - ARDUINO SECUNDARIO
 4 LED + servo SG90 en D7.

 PRINCIPAL D5 -> SECUNDARIO D2 (senal de control)
 GND principal -> GND secundario
 LED amarillo D3, verde D4, rojo D5, azul D6 (cada uno con 330 ohm)
 SERVO SG90 naranja -> D7 del secundario
 SERVO SG90 rojo -> +4.8 V de fuente AA (4 x 1.2 V)
 SERVO SG90 cafe -> GND de baterias
 GND baterias -> GND de ambos Arduinos (TIERRA COMUN)

 Comandos por ancho del pulso LOW:
 100 ms = LED amarillo; 200 ms = verde; 300 ms = rojo
 400 ms = azul intermitente; 500 ms = apagar todos
 650 ms = ABRIR servo; 800 ms = CERRAR servo

 IMPORTANTE: los LED se conservan durante movimientos del servo.
 El emisor retransmite los LED; las ordenes de servo solo se ejecutan
 cuando se recibe un comando distinto del ultimo comando de servo.
 Tras reiniciar ambas placas se asume posicion CERRADA (sin sensor).
*/
#include <Servo.h>

const byte PIN_SENAL = 2;
const byte LED_AMARILLO = 3;
const byte LED_VERDE = 4;
const byte LED_ROJO = 5;
const byte LED_AZUL = 6;
const byte PIN_SERVO = 7;

// AJUSTAR AQUI LOS ANGULOS (mantiene ultima configuracion 30 / 130)
const byte ANGULO_CERRADO = 30;
const byte ANGULO_ABIERTO = 130;
const unsigned long PASO_SERVO_MS = 12;

Servo puerta;
int posicionServo = ANGULO_CERRADO;
int destinoServo = ANGULO_CERRADO;
bool servoMoviendo = false;
bool servoEstabilizando = false;
unsigned long ultimoPasoServo = 0;
unsigned long momentoLlegada = 0;
byte ultimoComandoServo = 0; // 0 ninguno, 1 abrir, 2 cerrar

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

void ordenarServo(byte comando) {
  // Ignora retransmisiones: evita movimientos repetidos por tramas duplicadas.
  if (comando == ultimoComandoServo) return;
  ultimoComandoServo = comando;
  destinoServo = (comando == 1) ? ANGULO_ABIERTO : ANGULO_CERRADO;

  if (!puerta.attached()) {
    puerta.attach(PIN_SERVO);
    puerta.write(posicionServo);
  }
  servoEstabilizando = false;
  servoMoviendo = (posicionServo != destinoServo);
  ultimoPasoServo = millis();
  if (!servoMoviendo) {
    momentoLlegada = millis();
    servoEstabilizando = true;
  }
}

void actualizarServo() {
  unsigned long ahora = millis();
  if (servoMoviendo && ahora - ultimoPasoServo >= PASO_SERVO_MS) {
    ultimoPasoServo = ahora;
    posicionServo += (destinoServo > posicionServo) ? 1 : -1;
    puerta.write(posicionServo);
    if (posicionServo == destinoServo) {
      servoMoviendo = false;
      servoEstabilizando = true;
      momentoLlegada = ahora;
    }
  }
  if (servoEstabilizando && ahora - momentoLlegada >= 300UL) {
    puerta.detach();
    servoEstabilizando = false;
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
  if (lecturaAnterior == LOW) inicioNivelBajo = micros();
  // No conectar servo automaticamente al arrancar: podria mover la cerradura
  // si el Arduino se reinicia mientras la puerta esta abierta.
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
    else if (ancho >= 600000UL && ancho <= 700000UL) codigo = 10;
    else if (ancho >= 750000UL && ancho <= 850000UL) codigo = 11;

    if (codigo != 255) {
      if (codigo <= 8) {
        elegirLed(codigo);
      } else if (codigo == 10) {
        ordenarServo(1); // abrir
      } else if (codigo == 11) {
        ordenarServo(2); // cerrar
      }
      huboTramaValida = true;
      ultimaTramaValida = millis();
    }
  }
  lecturaAnterior = lectura;

  actualizarServo(); // Sin delay: sigue leyendo pulsos durante el giro.

  if (ledActivo == 8 && millis() - ultimoParpadeoAzul >= 250UL) {
    ultimoParpadeoAzul = millis();
    azulEncendido = !azulEncendido;
    digitalWrite(LED_AZUL, azulEncendido ? HIGH : LOW);
  }

  if (huboTramaValida && millis() - ultimaTramaValida > 10000UL) {
    elegirLed(0);
    huboTramaValida = false;
  }
}
