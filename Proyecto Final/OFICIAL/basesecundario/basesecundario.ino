/*
  ARQUITECTURA DEL COMPUTADOR - VARIANTE A
  Arduino UNO SECUNDARIO - receptor de 4 LED

  Conexiones:
    Arduino principal D5  -> Arduino secundario D2
    Arduino principal GND -> Arduino secundario GND

    D3 -> resistencia 330 ohm -> anodo LED amarillo (5)
    D4 -> resistencia 330 ohm -> anodo LED verde    (6)
    D5 -> resistencia 330 ohm -> anodo LED rojo     (7)
    D6 -> resistencia 330 ohm -> anodo LED azul     (8)
    Catodos de LED -> GND secundario.

  Protocolo de prueba previsto para el Arduino PRINCIPAL:
    Linea inactiva = HIGH.
    Pulso LOW de aprox. 100 ms -> LED amarillo (estado 5)
    Pulso LOW de aprox. 200 ms -> LED verde    (estado 6)
    Pulso LOW de aprox. 300 ms -> LED rojo     (estado 7)
    Pulso LOW de aprox. 400 ms -> LED azul     (estado 8)
    Pulso LOW de aprox. 500 ms -> apagar los cuatro LED.
    Esperar al menos 150 ms en HIGH entre comandos.

  ATENCION:
  Este es SOLO el receptor. El principal requiere el codigo emisor
  correspondiente; cargar este sketch NO prueba aun la comunicacion.
  El LED seleccionado permanece encendido hasta recibir otro comando.
*/

const byte PIN_SENAL = 2;
const byte LED_AMARILLO = 3;
const byte LED_VERDE = 4;
const byte LED_ROJO = 5;
const byte LED_AZUL = 6;

void apagarTodos() {
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_AZUL, LOW);
}

void elegirLed(byte led) {
  apagarTodos();
  switch (led) {
    case 5: digitalWrite(LED_AMARILLO, HIGH); break;
    case 6: digitalWrite(LED_VERDE, HIGH); break;
    case 7: digitalWrite(LED_ROJO, HIGH); break;
    case 8: digitalWrite(LED_AZUL, HIGH); break;
    // 0: todos apagados; valores desconocidos se ignoran.
  }
}

void setup() {
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  apagarTodos();

  // Pull-up mantiene HIGH la linea cuando el emisor no esta conectado.
  pinMode(PIN_SENAL, INPUT_PULLUP);
}

void loop() {
  // Captura la duracion de un pulso LOW. Timeout: 1 segundo.
  unsigned long ancho = pulseIn(PIN_SENAL, LOW, 1000000UL);
  if (ancho == 0) return;

  // Rangos de tolerancia amplios, pero sin solaparse.
  if (ancho >= 75000UL && ancho <= 125000UL) {
    elegirLed(5);
  } else if (ancho >= 175000UL && ancho <= 225000UL) {
    elegirLed(6);
  } else if (ancho >= 275000UL && ancho <= 325000UL) {
    elegirLed(7);
  } else if (ancho >= 375000UL && ancho <= 425000UL) {
    elegirLed(8);
  } else if (ancho >= 475000UL && ancho <= 525000UL) {
    elegirLed(0);
  }
}
