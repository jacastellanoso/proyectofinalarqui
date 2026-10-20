/*
 ==============================================================
             ARQUITECTURA DEL COMPUTADOR
        PROYECTO: SISTEMA DE CONTROL DE ACCESO

                 PROGRAMA BASE
 ==============================================================

 Este programa contiene la arquitectura general del proyecto.

 El código está INCOMPLETO intencionalmente.

 Durante los siguientes laboratorios se implementarán
 progresivamente las funciones marcadas como:

                         TODO

 Al finalizar los laboratorios, este programa se habrá
 convertido en el sistema completo de control de acceso.
 ==============================================================
*/


#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <stddef.h>
#include <string.h>


// ==============================================================
// 1. MAPA DE PINES
// ==============================================================

/*
 ARDUINO UNO
 --------------------------------------------------------------

 D0  -> Serial RX
 D1  -> Serial TX

 D2  -> Buzzer pasivo
 D3  -> Botón de diagnóstico
 D4  -> Servo

 D5  -> Comunicación de estados con Arduino secundario (D2)

 D6  -> RFID RST

 D7  -> 74HC595 DATA
 D8  -> 74HC595 CLOCK
 D9  -> 74HC595 LATCH

 D10 -> RFID SDA / SS
 D11 -> RFID MOSI
 D12 -> RFID MISO
 D13 -> RFID SCK

 A0  -> Keypad C1
 A1  -> Keypad C2
 A2  -> Keypad C3
 A3  -> Keypad C4

 A4  -> LCD SDA
 A5  -> LCD SCL
*/


const byte PIN_BUZZER = 2;
const byte PIN_DIAG   = 3;
const byte PIN_SERVO  = 4;

// Arduino principal D5 -> Arduino secundario D2 (GND compartida).
const byte PIN_LED_SECUNDARIO = 5;

const byte RFID_RST = 6;

const byte DATA  = 7;
const byte CLOCK = 8;
const byte LATCH = 9;

const byte RFID_SS = 10;

const byte columnas[4] = {
  A0, A1, A2, A3
};


// ==============================================================
// 2. DISPOSITIVOS
// ==============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo puerta;

MFRC522 rfid(RFID_SS, RFID_RST);


// ==============================================================
// 3. REGISTRO 74HC595 - CABLEADO REAL FASE 0
// ==============================================================
/*
   Q0-Q3: filas del teclado, en HIGH cuando no se estan escaneando.
   Q4: LED 1 VERDE    / INICIO
   Q5: LED 2 ROJO     / ESPERANDO RFID
   Q6: LED 3 AMARILLO / VALIDANDO RFID
   Q7: LED 4 AZUL     / ESPERANDO PIN
   Los LED 5-8 estan en el Arduino secundario.
   Nota: el escaneo del teclado continua pendiente (TODO).
*/
byte registro = B00010000;


// ==============================================================
// 4. MAPA DEL KEYPAD
// ==============================================================

char teclas[4][4] = {

  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}

};


// ==============================================================
// 5. VARIABLES DEL SISTEMA
// ==============================================================

// UID obtenido del RC522

String UIDLeido = "";


// PIN introducido mediante el keypad

char pinIngresado[5] = "";

byte posicionPIN = 0;


// Número de intentos incorrectos

byte intentosFallidos = 0;

// Desactivar cuando se complete la autenticacion por PIN.
const bool modoPruebaRfid = true;
bool pruebaLedsActiva = false;
unsigned long inicioPruebaLeds = 0;
bool resultadoPruebaVisible = false;
unsigned long inicioResultadoPrueba = 0;

// EEPROM v1: cabecera de cuatro bytes y dos registros de 16 bytes.
const uint32_t firmaUsuarios = 0x55494401UL;
const byte cantidadUsuarios = 2;
const byte marcaUsuarioValido = 0xA5;

struct UsuarioEEPROM
{
  byte longitudUid;
  byte uid[10];
  char pin[4];
  byte valido;
};

static_assert(sizeof(UsuarioEEPROM) == 16, "Revisar formato EEPROM de usuarios");

int usuarioIdentificado = -1;
bool eepromConfigurada = false;
int usuarioPendiente = -1;
char pinPendiente[4];
char comandoSerial[24];
byte longitudComando = 0;
bool comandoDesbordado = false;


// ==============================================================
// 6. ESTADOS DEL SISTEMA
// ==============================================================

/*
 La CPU solamente puede encontrarse en uno de estos
 estados en un momento determinado.

 El estado actual determina qué operación realizará
 el programa.
*/

enum EstadoSistema
{
  INICIO,

  VALIDANDO_RFID,

  ESPERANDO_RFID,

  ESPERANDO_PIN,

  VALIDANDO_PIN,

  ACCESO_AUTORIZADO,

  ACCESO_DENEGADO,

  PUERTA_ABIERTA,

  ALARMA
};


EstadoSistema estado = INICIO;

void cambiarEstado(EstadoSistema nuevoEstado);


// ==============================================================
// 6A. COMUNICACION CON ARDUINO SECUNDARIO - LED 5 A LED 8
// ==============================================================
/*
  Compatible con arduino_secundario_receptor_4_leds.txt.
  Protocolo: un pulso LOW en D5, con reposo HIGH:
    100 ms = LED 5 amarillo (validando PIN)
    200 ms = LED 6 verde    (autorizado)
    300 ms = LED 7 rojo     (denegado)
    400 ms = LED 8 azul     (alarma)
    500 ms = apagar LED 5 a 8.

  Se envia un comando solo cuando cambia el LED requerido.
  Los LED 1 a 4 siguen en Q4-Q7 del 74HC595 original.
  Q0-Q3 se conservan para las filas del teclado.
  En estados 5-8, los cuatro LED del principal se apagan.
  NOTA: La lectura del teclado y la autenticacion PIN todavia estan TODO
  en el programa base y no se implementan en esta modificacion.
*/
void actualizarLedsSecundarios()
{
  static byte ultimoComando = 255; // Obliga a apagar en la inicializacion.
  byte comando = 0;

  if      (registro & B00000001) comando = 5;
  else if (registro & B00000010) comando = 6;
  else if (registro & B00000100) comando = 7;
  else if (registro & B00001000) comando = 8;

  if (comando == ultimoComando) return;
  ultimoComando = comando;

  unsigned int duracionMs = (comando == 0) ? 500 : (comando - 4) * 100;
  digitalWrite(PIN_LED_SECUNDARIO, LOW);
  delay(duracionMs);
  digitalWrite(PIN_LED_SECUNDARIO, HIGH);
  // Pausa necesaria para distinguir comandos consecutivos.
  delay(160);
}

// ==============================================================
// 7. ACTUALIZAR 74HC595
// ==============================================================

void actualizarRegistro()
{
  digitalWrite(LATCH, LOW);
  // Q0-Q3 son las filas del teclado: en reposo deben estar HIGH.
  // Q4-Q7 corresponden a los cuatro LEDs ya conectados al 74HC595.
  // Escribimos los cuatro bits superiores del estado y mantenemos
  // las filas del teclado inactivas, sin activar LEDs incorrectos.
  shiftOut(DATA, CLOCK, MSBFIRST, byte((registro & B11110000) | B00001111));
  digitalWrite(LATCH, HIGH);

  // LED 5-8 por D5; tambien se manda apagar al volver a LED 1-4.
  actualizarLedsSecundarios();
}

void actualizarLedsEstado()
{
  switch (estado)
  {
    case INICIO: registro = B00010000; break;
    case ESPERANDO_RFID: registro = B00100000; break;
    case VALIDANDO_RFID: registro = B01000000; break;
    case ESPERANDO_PIN: registro = B10000000; break;
    case VALIDANDO_PIN: registro = B00000001; break;
    case ACCESO_AUTORIZADO:
    case PUERTA_ABIERTA: registro = B00000010; break;
    case ACCESO_DENEGADO: registro = B00000100; break;
    case ALARMA: registro = B00001000; break;
  }
  actualizarRegistro();
}

void cambiarEstado(EstadoSistema nuevoEstado)
{
  estado = nuevoEstado;
  actualizarLedsEstado();
}


// ==============================================================
// 8. CONTROL DE LEDs
// ==============================================================

void apagarLEDs()
{
  registro = 0;
  actualizarRegistro();
}


void encenderLED(byte numero)
{
  if (numero > 7) return;
  // Pruebas LED 0..7: 0..3 = primeros cuatro LED, 4..7 = secundarios.
  if (numero < 4) registro = byte(1U << (numero + 4));
  else registro = byte(1U << (numero - 4));
  actualizarRegistro();
}


// ==============================================================
// 9. SELECCIÓN DE FILA DEL KEYPAD
// ==============================================================

void seleccionarFila(byte fila)
{
  if (modoPruebaRfid) return; // Keypad no implementado en esta etapa.
  /*
   TODO - LABORATORIO KEYPAD

   Para detectar una tecla:

   1. Colocar Q0-Q3 en HIGH.

   2. Colocar únicamente la fila que se desea
      consultar en LOW.

   3. Enviar el nuevo valor al 74HC595.

   Pista:

       bitClear()
  */
}


// ==============================================================
// 10. LECTURA DEL KEYPAD
// ==============================================================

char leerTeclado()
{
  if (modoPruebaRfid) return 0;
  /*
   TODO - LABORATORIO KEYPAD

   Implementar el barrido de la matriz 4x4.

   El algoritmo deberá:

   1. Seleccionar una fila.
   2. Leer las cuatro columnas.
   3. Determinar qué tecla fue presionada.
   4. Manejar el rebote de la tecla.
   5. Retornar el carácter correspondiente.

   Si ninguna tecla fue presionada:

       return 0;
  */

  return 0;
}


// ==============================================================
// 11. LECTURA DEL RFID
// ==============================================================

bool comprobarLectorRfid(bool informar)
{
  static int versionAnterior = -1;
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  bool comunicado = version != 0x00 && version != 0xFF;
  if (informar || version != versionAnterior)
  {
    Serial.print(F("RFID VersionReg: 0x"));
    Serial.println(version, HEX);
    if (!comunicado)
      Serial.println(F("RFID: sin comunicacion SPI aparente. Revise alimentacion y conexiones."));
    else if (version == 0x90 || version == 0x91 || version == 0x92)
      Serial.println(F("RFID: lector detectado; falta comprobar lectura de tarjeta."));
    else
      Serial.println(F("RFID: registro responde con version no reconocida; posible clon o fallo SPI."));
    versionAnterior = version;
  }
  return comunicado;
}

String leerTarjeta()
{
  /*
   TODO - LABORATORIO RFID

   Utilizar el módulo RC522.

   El algoritmo deberá:

   1. Detectar si existe una nueva tarjeta.
   2. Leer el identificador UID.
   3. Recorrer los bytes del UID.
   4. Convertirlos a una representación que pueda
      utilizar el programa.
   5. Retornar el UID.

   Si no existe una tarjeta:

       return "";
  */

  static unsigned long ultimaConsulta = 0;
  static unsigned long ultimoDiagnostico = 0;
  static unsigned long ultimoEvento = 0;
  static bool lectorComunicado = false;
  static bool primeraConsulta = true;
  unsigned long ahora = millis();
  if (!primeraConsulta && ahora - ultimaConsulta < 100) return "";
  ultimaConsulta = ahora;

  if (primeraConsulta || ahora - ultimoDiagnostico >= 5000)
  {
    lectorComunicado = comprobarLectorRfid(false);
    ultimoDiagnostico = ahora;
    primeraConsulta = false;
  }
  if (!lectorComunicado) return "";

  // Esta funcion devuelve true para REQA correcto o colision; no garantiza un UID.
  bool tarjetaDetectada = rfid.PICC_IsNewCardPresent();
  if (!tarjetaDetectada)
  {
    if (ahora - ultimoEvento >= 5000)
    {
      Serial.println(F("RFID: tarjeta no detectada (REQA sin respuesta valida)."));
      ultimoEvento = ahora;
    }
    return "";
  }

  // PICC_ReadCardSerial() oculta el StatusCode de PICC_Select().
  // Usar la misma operacion directamente permite diagnosticar el fallo real.
  MFRC522::StatusCode resultado = rfid.PICC_Select(&rfid.uid);
  if (resultado != MFRC522::STATUS_OK)
  {
    if (ahora - ultimoEvento >= 2000)
    {
      Serial.print(F("RFID: tarjeta detectada, UID ilegible. Estado: "));
      Serial.println(rfid.GetStatusCodeName(resultado));
      Serial.println(F("Retire la tarjeta y acerque solo una tarjeta compatible."));
      ultimoEvento = ahora;
    }
    // No usar un UID incompleto ni enviarlo a EEPROM. Restablecer el lector.
    rfid.PCD_StopCrypto1();
    rfid.PCD_Init();
    delay(4);
    lectorComunicado = comprobarLectorRfid(false);
    return "";
  }

  if (rfid.uid.size != 4 && rfid.uid.size != 7 && rfid.uid.size != 10)
  {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    if (ahora - ultimoEvento >= 2000)
    {
      Serial.println(F("RFID: UID con longitud invalida; lectura descartada."));
      ultimoEvento = ahora;
    }
    return "";
  }

  // Dos caracteres hexadecimales por byte, en mayusculas y sin separadores.
  // Conservar ceros iniciales y longitud permite comparar el UID con EEPROM.
  const char digitosHex[] = "0123456789ABCDEF";
  char uidHex[sizeof(rfid.uid.uidByte) * 2 + 1];

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    uidHex[i * 2] = digitosHex[rfid.uid.uidByte[i] >> 4];
    uidHex[i * 2 + 1] = digitosHex[rfid.uid.uidByte[i] & 0x0F];
  }

  uidHex[rfid.uid.size * 2] = '\0';

  MFRC522::StatusCode cierre = rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  Serial.println(F("RFID: UID leido correctamente."));
  Serial.print(F("RFID UID: "));
  Serial.println(uidHex);
  Serial.print(F("RFID tipo indicado por SAK: "));
  Serial.println(rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak)));
  if (cierre != MFRC522::STATUS_OK)
  {
    Serial.print(F("RFID: aviso al finalizar tarjeta: "));
    Serial.println(rfid.GetStatusCodeName(cierre));
  }
  ultimoEvento = ahora;

  return String(uidHex);
}


// ==============================================================
// 12. VALIDACIÓN DE TARJETA
// ==============================================================

int direccionUsuario(byte indice)
{
  return sizeof(firmaUsuarios) + indice * sizeof(UsuarioEEPROM);
}

bool leerUsuario(byte indice, UsuarioEEPROM &usuario)
{
  if (!eepromConfigurada || indice >= cantidadUsuarios)
  {
    return false;
  }

  EEPROM.get(direccionUsuario(indice), usuario);
  if (usuario.valido != marcaUsuarioValido ||
      (usuario.longitudUid != 4 && usuario.longitudUid != 7 && usuario.longitudUid != 10))
  {
    return false;
  }

  for (byte i = 0; i < 4; i++)
  {
    if (usuario.pin[i] < '0' || usuario.pin[i] > '9') return false;
  }
  return true;
}

bool coincideUid(const String &uid, const UsuarioEEPROM &usuario)
{
  if (uid.length() != usuario.longitudUid * 2) return false;

  const char digitosHex[] = "0123456789ABCDEF";
  for (byte i = 0; i < usuario.longitudUid; i++)
  {
    if (uid[i * 2] != digitosHex[usuario.uid[i] >> 4] ||
        uid[i * 2 + 1] != digitosHex[usuario.uid[i] & 0x0F]) return false;
  }
  return true;
}

void iniciarUsuarios()
{
  uint32_t firmaLeida;
  EEPROM.get(0, firmaLeida);
  eepromConfigurada = firmaLeida == firmaUsuarios;
  Serial.println(eepromConfigurada ? F("EEPROM: formato de usuarios configurado.") :
                                    F("EEPROM: sin configurar o formato incompatible; no se escribio nada."));
  for (byte i = 0; i < cantidadUsuarios; i++)
  {
    UsuarioEEPROM usuario;
    Serial.print(F("Usuario "));
    Serial.print(i + 1);
    Serial.println(leerUsuario(i, usuario) ? F(": registrado.") : F(": no registrado o invalido."));
  }
  Serial.println(F("Registro: envie REG 1 1234 o REG 2 5678 y acerque la tarjeta real."));
  Serial.println(F("Use su PIN de cuatro digitos. CANCELAR cancela el registro pendiente."));
  Serial.println(F("LED <0..7>: prueba visual de 2 segundos, solo en modo prueba RFID."));
}

void procesarRegistroSerial()
{
  while (Serial.available() > 0)
  {
    char caracter = Serial.read();
    if (caracter == '\r' || caracter == '\n')
    {
      if (comandoDesbordado)
      {
        Serial.println(F("Registro: comando demasiado largo."));
      }
      else if (longitudComando > 0)
      {
        comandoSerial[longitudComando] = '\0';
        if (strcmp(comandoSerial, "CANCELAR") == 0)
        {
          usuarioPendiente = -1;
          memset(pinPendiente, 0, sizeof(pinPendiente));
          reiniciarSistema();
          Serial.println(F("Registro cancelado."));
        }
        else if (longitudComando == 5 && strncmp(comandoSerial, "LED ", 4) == 0 &&
                 comandoSerial[4] >= '0' && comandoSerial[4] <= '7')
        {
          if (!modoPruebaRfid || usuarioPendiente >= 0)
            Serial.println(F("LED: requiere modo prueba RFID sin registro pendiente. Use CANCELAR."));
          else
          {
            pruebaLedsActiva = true;
            inicioPruebaLeds = millis();
            encenderLED(comandoSerial[4] - '0');
            Serial.print(F("Prueba visual Q"));
            Serial.print(comandoSerial[4]);
            Serial.println(F(": 2 s, sin servo, buzzer ni autenticacion."));
          }
        }
        else
        {
          bool correcto = longitudComando == 10 && strncmp(comandoSerial, "REG ", 4) == 0 &&
                          comandoSerial[4] >= '1' && comandoSerial[4] <= '2' && comandoSerial[5] == ' ';
          for (byte i = 6; correcto && i < 10; i++)
          {
            correcto = comandoSerial[i] >= '0' && comandoSerial[i] <= '9';
          }
          if (correcto)
          {
            reiniciarSistema();
            usuarioPendiente = comandoSerial[4] - '1';
            memcpy(pinPendiente, comandoSerial + 6, 4);
            usuarioIdentificado = -1;
            Serial.print(F("Registro preparado para usuario "));
            Serial.print(usuarioPendiente + 1);
            Serial.println(F(". Acerque su tarjeta; reemplazara ese registro."));
          }
          else Serial.println(F("Comando invalido. Use REG <1|2> <PIN de 4 digitos>, CANCELAR o LED <0..7>."));
        }
      }
      memset(comandoSerial, 0, sizeof(comandoSerial));
      longitudComando = 0;
      comandoDesbordado = false;
    }
    else if (!comandoDesbordado)
    {
      if (longitudComando < sizeof(comandoSerial) - 1) comandoSerial[longitudComando++] = caracter;
      else comandoDesbordado = true;
    }
  }
}

void registrarTarjeta(const String &uid)
{
  UsuarioEEPROM nuevo = {};
  nuevo.longitudUid = uid.length() / 2;
  if ((nuevo.longitudUid != 4 && nuevo.longitudUid != 7 && nuevo.longitudUid != 10) ||
      uid.length() != nuevo.longitudUid * 2)
  {
    Serial.println(F("Registro: longitud UID invalida."));
    return;
  }

  const char digitosHex[] = "0123456789ABCDEF";
  for (byte i = 0; i < nuevo.longitudUid; i++)
  {
    const char *alto = strchr(digitosHex, uid[i * 2]);
    const char *bajo = strchr(digitosHex, uid[i * 2 + 1]);
    if (!alto || !bajo || uid[i * 2] == '\0' || uid[i * 2 + 1] == '\0')
    {
      Serial.println(F("Registro: UID hexadecimal invalido."));
      return;
    }
    nuevo.uid[i] = ((alto - digitosHex) << 4) | (bajo - digitosHex);
  }

  for (byte i = 0; i < cantidadUsuarios; i++)
  {
    UsuarioEEPROM existente;
    if (i != usuarioPendiente && leerUsuario(i, existente) && coincideUid(uid, existente))
    {
      Serial.println(F("Registro: tarjeta ya asignada al otro usuario. Acerque otra tarjeta."));
      return;
    }
  }

  // Inicializar solamente ante una solicitud explicita de registro.
  if (!eepromConfigurada)
  {
    uint32_t firmaInvalida = 0;
    EEPROM.put(0, firmaInvalida);
    UsuarioEEPROM vacio = {};
    for (byte i = 0; i < cantidadUsuarios; i++) EEPROM.put(direccionUsuario(i), vacio);
    EEPROM.put(0, firmaUsuarios);
    eepromConfigurada = true;
  }

  memcpy(nuevo.pin, pinPendiente, 4);
  int direccion = direccionUsuario(usuarioPendiente);
  // Invalidar primero y confirmar al final evita aceptar una escritura parcial.
  EEPROM.update(direccion + offsetof(UsuarioEEPROM, valido), 0);
  EEPROM.put(direccion, nuevo);
  EEPROM.update(direccion + offsetof(UsuarioEEPROM, valido), marcaUsuarioValido);

  UsuarioEEPROM verificado;
  bool guardado = leerUsuario(usuarioPendiente, verificado) && coincideUid(uid, verificado) &&
                  memcmp(verificado.pin, pinPendiente, 4) == 0;
  Serial.print(F("Usuario "));
  Serial.print(usuarioPendiente + 1);
  Serial.println(guardado ? F(": guardado y verificado en EEPROM.") : F(": error al verificar EEPROM."));
  usuarioPendiente = -1;
  memset(pinPendiente, 0, sizeof(pinPendiente));
  reiniciarSistema();
}

bool tarjetaAutorizada(String uid)
{
  /*
   TODO - LABORATORIO RFID

   Determinar si el UID recibido pertenece
   a una tarjeta autorizada.

   Retornar:

       true  -> tarjeta autorizada
       false -> tarjeta no autorizada
  */

  usuarioIdentificado = -1;
  for (byte i = 0; i < cantidadUsuarios; i++)
  {
    UsuarioEEPROM usuario;
    if (leerUsuario(i, usuario) && coincideUid(uid, usuario))
    {
      usuarioIdentificado = i;
      Serial.print(F("RFID autorizado: usuario "));
      Serial.println(i + 1);
      return true;
    }
  }

  Serial.println(F("RFID denegado: UID no registrado o EEPROM sin configurar."));
  return false;
}


// ==============================================================
// 13. CAPTURA DEL PIN
// ==============================================================

bool leerPIN()
{
  /*
   TODO - LABORATORIO KEYPAD / INTEGRACIÓN

   Utilizar:

       leerTeclado()

   para capturar un PIN de cuatro números.

   Cada número deberá almacenarse en:

       pinIngresado

   Cuando existan cuatro números:

       return true;

   Mientras el usuario continúe escribiendo:

       return false;


   SUGERENCIA:

   En el LCD no mostrar los números.

   Mostrar:

       *
       **
       ***
       ****
  */

  return false;
}


// ==============================================================
// 14. VALIDACIÓN DEL PIN
// ==============================================================

bool validarPIN()
{
  /*
   TODO - LABORATORIO EEPROM

   El PIN autorizado se encuentra almacenado
   en la memoria EEPROM.

   El algoritmo deberá:

   1. Leer el PIN almacenado.
   2. Compararlo con pinIngresado.
   3. Determinar si son iguales.

   Retornar:

       true  -> PIN correcto
       false -> PIN incorrecto
  */

  return false;
}


// ==============================================================
// 15. ABRIR PUERTA
// ==============================================================

void abrirPuerta()
{
  /*
   TODO - LABORATORIO SERVO

   Mover el servo a la posición correspondiente
   a la puerta abierta.
  */
}


// ==============================================================
// 16. CERRAR PUERTA
// ==============================================================

void cerrarPuerta()
{
  /*
   TODO - LABORATORIO SERVO

   Mover el servo a la posición correspondiente
   a la puerta cerrada.
  */
}


// ==============================================================
// 17. SONIDO DE ACCESO CORRECTO
// ==============================================================

void sonidoCorrecto()
{
  /*
   TODO - LABORATORIO BUZZER

   Utilizar el buzzer pasivo para generar
   un sonido que indique acceso autorizado.
  */
}


// ==============================================================
// 18. SONIDO DE ERROR
// ==============================================================

void sonidoError()
{
  /*
   TODO - LABORATORIO BUZZER

   Generar un sonido diferente que permita
   reconocer un acceso incorrecto.
  */
}


// ==============================================================
// 19. ALARMA
// ==============================================================

void activarAlarma()
{
  /*
   TODO - PROYECTO FINAL

   Diseñar el comportamiento de la alarma.

   Puede utilizar:

       - LED rojo
       - Buzzer
       - LCD

   La implementación queda a criterio
   de cada equipo.
  */
}


// ==============================================================
// 20. REINICIAR CICLO DE AUTENTICACIÓN
// ==============================================================

void reiniciarSistema()
{
  pruebaLedsActiva = false;
  resultadoPruebaVisible = false;
  usuarioIdentificado = -1;
  UIDLeido = "";

  posicionPIN = 0;

  pinIngresado[0] = '\0';


  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(modoPruebaRfid ? "CONTROL ACCESO" : "SISTEMA URL");

  lcd.setCursor(0, 1);
  lcd.print(modoPruebaRfid ? "PRESENTE TARJETA" : "ACERQUE TARJETA");


  cambiarEstado(ESPERANDO_RFID);
}


// ==============================================================
// 21. SETUP
// ==============================================================

void setup()
{

  // ----------------------------------------------------------
  // SERIAL
  // ----------------------------------------------------------

  Serial.begin(9600);

  iniciarUsuarios();


  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  lcd.init();

  lcd.backlight();


  // ----------------------------------------------------------
  // 74HC595
  // ----------------------------------------------------------

  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  // Linea de control al Arduino secundario: HIGH en reposo.
  digitalWrite(PIN_LED_SECUNDARIO, HIGH);
  pinMode(PIN_LED_SECUNDARIO, OUTPUT);

  cambiarEstado(INICIO);


  // ----------------------------------------------------------
  // KEYPAD
  // ----------------------------------------------------------

  for (byte i = 0; i < 4; i++)
  {
    pinMode(columnas[i], INPUT_PULLUP);
  }


  // ----------------------------------------------------------
  // BUZZER
  // ----------------------------------------------------------

  pinMode(PIN_BUZZER, OUTPUT);


  // ----------------------------------------------------------
  // BOTÓN DIAGNÓSTICO
  // ----------------------------------------------------------

  pinMode(PIN_DIAG, INPUT_PULLUP);


  // ----------------------------------------------------------
  // SERVO
  // ----------------------------------------------------------

  if (!modoPruebaRfid) puerta.attach(PIN_SERVO);


  // ----------------------------------------------------------
  // RFID
  // ----------------------------------------------------------

  SPI.begin();

  rfid.PCD_Init();

  // Algunos modulos requieren un breve tiempo de estabilizacion tras el reset.
  delay(4);
  comprobarLectorRfid(true);


  // ----------------------------------------------------------
  // INICIO
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SISTEMA ACCESO");

  lcd.setCursor(0, 1);
  lcd.print("INICIANDO...");

  delay(1500);

  reiniciarSistema();
}


// ==============================================================
// 22. LOOP
// ==============================================================

void loop()
{
  procesarRegistroSerial();

  // Prueba visual independiente de los estados y los actuadores.
  if (pruebaLedsActiva)
  {
    if (millis() - inicioPruebaLeds >= 2000)
    {
      pruebaLedsActiva = false;
      if (resultadoPruebaVisible && millis() - inicioResultadoPrueba >= 2000)
        reiniciarSistema();
      else actualizarLedsEstado();
    }
    return;
  }

  // Registro voluntario: usa la lectura existente sin cambiar los estados.
  if (usuarioPendiente >= 0)
  {
    String uidRegistro = leerTarjeta();
    if (uidRegistro != "") registrarTarjeta(uidRegistro);
    return;
  }

  // Prueba de identificacion exclusivamente: nunca entra al flujo de apertura.
  // millis() mantiene disponibles REG y CANCELAR mientras se muestra el resultado.
  if (modoPruebaRfid)
  {
    if (resultadoPruebaVisible)
    {
      if (millis() - inicioResultadoPrueba >= 2000) reiniciarSistema();
      return;
    }

    UIDLeido = leerTarjeta();
    if (UIDLeido == "") return;

    cambiarEstado(VALIDANDO_RFID);
    bool registrada = tarjetaAutorizada(UIDLeido);
    cambiarEstado(registrada ? ACCESO_AUTORIZADO : ACCESO_DENEGADO);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(registrada ? "TARJETA VALIDA" : "TARJETA INVALIDA");
    lcd.setCursor(0, 1);
    if (registrada)
    {
      lcd.print("USUARIO ");
      lcd.print(usuarioIdentificado + 1);
    }
    else lcd.print("ACCESO DENEGADO");

    Serial.println(F("MODO PRUEBA RFID: sin autorizacion de acceso fisico."));
    inicioResultadoPrueba = millis();
    resultadoPruebaVisible = true;
    return;
  }

  /*
   ============================================================
                       UNIDAD DE CONTROL
   ============================================================

   La variable "estado" representa el estado actual
   del sistema.

   Dependiendo de ese estado, la CPU ejecutará una
   operación diferente.
  */


  switch (estado)
  {

    // Estados transitorios atendidos en setup y durante la comprobacion del UID.
    case INICIO:
    case VALIDANDO_RFID:
      break;

    // ========================================================
    // ESTADO 0 - ESPERANDO RFID
    // ========================================================

    case ESPERANDO_RFID:

      UIDLeido = leerTarjeta();

      if (UIDLeido != "")
      {
        cambiarEstado(VALIDANDO_RFID);
        if (tarjetaAutorizada(UIDLeido))
        {
          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("TARJETA VALIDA");

          lcd.setCursor(0, 1);
          lcd.print("INGRESE PIN");

          cambiarEstado(ESPERANDO_PIN);
        }
        else
        {
          cambiarEstado(ACCESO_DENEGADO);
        }
      }

      break;


    // ========================================================
    // ESTADO 1 - ESPERANDO PIN
    // ========================================================

    case ESPERANDO_PIN:

      if (leerPIN())
      {
        cambiarEstado(VALIDANDO_PIN);
      }

      break;


    // ========================================================
    // ESTADO 2 - VALIDANDO PIN
    // ========================================================

    case VALIDANDO_PIN:

      if (validarPIN())
      {
        cambiarEstado(ACCESO_AUTORIZADO);
      }
      else
      {
        intentosFallidos++;

        cambiarEstado(ACCESO_DENEGADO);
      }

      break;


    // ========================================================
    // ESTADO 3 - ACCESO AUTORIZADO
    // ========================================================

    case ACCESO_AUTORIZADO:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("ACCESO");

      lcd.setCursor(0, 1);
      lcd.print("AUTORIZADO");


      actualizarLedsEstado();

      sonidoCorrecto();

      abrirPuerta();


      cambiarEstado(PUERTA_ABIERTA);

      break;


    // ========================================================
    // ESTADO 4 - ACCESO DENEGADO
    // ========================================================

    case ACCESO_DENEGADO:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("ACCESO");

      lcd.setCursor(0, 1);
      lcd.print("DENEGADO");


      actualizarLedsEstado();

      sonidoError();


      delay(1500);


      if (intentosFallidos >= 3)
      {
        cambiarEstado(ALARMA);
      }
      else
      {
        apagarLEDs();

        reiniciarSistema();
      }

      break;


    // ========================================================
    // ESTADO 5 - PUERTA ABIERTA
    // ========================================================

    case PUERTA_ABIERTA:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("PUERTA ABIERTA");


      delay(3000);


      cerrarPuerta();

      apagarLEDs();

      intentosFallidos = 0;


      reiniciarSistema();

      break;


    // ========================================================
    // ESTADO 6 - ALARMA
    // ========================================================

    case ALARMA:

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("!!! ALARMA !!!");


      activarAlarma();

      break;
  }
}
