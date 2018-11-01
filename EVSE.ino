// S:\Software\Ampel\EVSE\EVSE.ino
// First define the library :

#define NOSIM
//#define SERIELL
//#define SERIELL_LOOP

void MyReset(void) {
  asm volatile ("jmp 0 \n");
}


/* Typical pin layout used:
   -----------------------------------------------------------------------------------------
               MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
               Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
   Signal      Pin          Pin           Pin       Pin        Pin              Pin
   -----------------------------------------------------------------------------------------
   RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
   SPI SS      SDA(SS)      10            53        D10        10               10
   SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
   SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
   SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
*/

#ifdef NOSIM
#include <MFRC522.h>
#define SS_PIN 10
#define RST_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);


/**
     Attiny85 PINS (i2c)
     Nano/Atmega328 PINS: connect LCD to A4(SDA)/A5(SDL) (i2c)
     ESP8266: GPIO4(SDA) / GPIO5( SCL )
*/

#include <ssd1306.h>
#endif

// Taster
//int buttonPin = 2;
//bool buttonState;

// Musik
#define tonepin (8) // Lautsprecher
#define ledPin  (6) // LED am RFID Leser
#define EVSELED (7) // READY LED
#define EVSE_AN (3) // Taster zum stoppen
#define Relais1 (4) // Controler
#define Relais2 (5) // CP Leitung
#define I_PP    A0 // Zur Messung des Widerstandswertes im Typ2-Stecker, welcher die Belastbarkeit des Ladekabels kodiert.
#define I_CP    A1 // Zur Messung der Spannung am Control Pilot von 0V bis +12V, d.h. zur Erkennung des Status des Elektroautos.
#define L_Strom A2 // Zur Messung der Spannung am Control Pilot von 0V bis +12V, d.h. zur Erkennung des Status des Elektroautos.

String inputString;   // a String to hold incoming data
boolean inputComplete = true;  // whether the string is complete

int OLED_hight;
int OLED_width;
char output[65];

long Control = 1;
long Wallbox = 2;

int MyTicks = 500;
long anzloop = 0;


void load_stop() {
  delay(250);
  digitalWrite (EVSE_AN, HIGH);
  delay(250);
  digitalWrite (EVSE_AN, LOW);
#ifdef SERIELL
  Serial.println("Ladevorgang abgebrochen");
#endif
  Serial.println("#L0");
  ssd1306_clearScreen();
  ssd1306_printFixedN(0, OLED_hight, "LSTOP", STYLE_NORMAL, FONT_SIZE_4X);
  delay(10000);
}

#define AnzMess 10

// CP-Spannung (positive Messung):
#define PLUS_12V       0
#define PLUS_9V        1
#define PLUS_6V        2
#define CP_ERROR       3
#define CP_OFF         4 // EVSE Controler aus

byte CP_Spannung; // Moegliche Werte siehe Konstanten. 255 -> Spannung konnte nicht korrekt erkannt werden.
int CP_Spannung_Wert;
byte lastSpannung;

void CP() {
  // Variablen fuer die Messung der CP - Spannung:
  int messwert = 0;
  int i = AnzMess;

  while (i) {
    messwert = max (messwert, analogRead(I_CP));
    delay(100);
    i--;
  }
  CP_Spannung_Wert = messwert;

#ifdef SERIELL
  print_volt(messwert);
#endif

  CP_Spannung = CP_OFF;
  if (messwert > 610 && messwert < 840) CP_Spannung = PLUS_12V;
  if (messwert > 400 && messwert < 610) CP_Spannung = PLUS_9V;
  if (messwert > 180 && messwert < 400) CP_Spannung = PLUS_6V;
  if (messwert < 45) CP_Spannung = CP_ERROR;
}

// Stromstaerke:
#define STROM_NO       0
#define STROM_14A      1
#define STROM_16A      2
#define STROM_20A      3
#define STROM_32A      4

byte Kabel;
int Kabel_Wert;
byte lastKabel;

// Diese Funktion misst den PP-Kontakt, wie stark das Ladekabel belastet werden darf und gibt die entsprechende max. Stromst�rke aus:
void Ladekabel() {
  uint16_t messwert = 0;
  int i = AnzMess;

  while (i) {
    messwert = max (messwert, analogRead(I_PP));
    delay(100);
    i--;
  }

  Kabel_Wert = messwert;
#ifdef SERIELL
  print_volt(messwert);
#endif

  if (messwert < 660) Kabel = STROM_32A;       // Widerstand 220 Ohm oder 100 Ohm --> Kabel darf mit 32A belastet werden.
  else if (messwert < 1350) Kabel = STROM_20A; // Widerstand 680 Ohm --> Kabel darf mit 20A belastet werden.
  else if (messwert < 1650) Kabel = STROM_16A; // Widerstand 1  kOhm --> Kabel darf mit 16A belastet werden.
  else if (messwert < 1950) Kabel = STROM_14A; // Widerstand 1,5 kOhm --> Kabel darf mit 14A belastet werden (eigentlich nur 13A, aber das passt).
  else Kabel = STROM_NO;                       // Kein Ladekabel angeschlossen.
}

#ifdef STROM_MESSEN
// Diese Funktion misst den Strom über L1
void Ladestrom() {
  uint16_t messwert = 0;
  float amps, kwh;
  int i = AnzMess;

  while (i) {
    messwert = max (messwert, analogRead(L_Strom));
    delay(100);
    i--;
  }

  // NULL A == ca. 560
  // 0,060 V / Ampere
  // 0,030 V / kleinster Stepp
  amps = ((float)(messwert / 3) - 558.0) * 60.0 / 512.0;
  // P = U * I
  kwh = (230.0 * amps) / 1000.0;
#ifdef SERIELL
  Serial.print("Ladestrom: ");
  Serial.print(amps);
  Serial.print(" A; Power: ");
  Serial.print(kwh);
  Serial.println(" Wh");
  Serial.print(messwert, DEC);
#endif
}
#endif

void rfid_read()
{
  long code = 0;
  char code_s[12];
  int Wait = 1000;

#ifdef NOSIM
  if ( ! mfrc522.PICC_IsNewCardPresent()) return;
  if ( ! mfrc522.PICC_ReadCardSerial()) return;
#endif

  beep();
  anzloop = 0;

#ifdef NOSIM
  screen_init();

  for (byte i = 0; i < mfrc522.uid.size; i++)
  {
    code = ((code + mfrc522.uid.uidByte[i]) * 10 );
  }
#else
  if ((Control > 1) && (Wallbox > 2)) return;
  code = random(735602, 735606);
  code = 735600;
#endif
  sprintf(code_s, "%10.10ld", code);

#ifdef NOSIM
#ifdef SERIELL
  Serial.print(F("Card UID:"));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10)
      Serial.print(F("0"));
    else
      Serial.print(F(""));
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();
  Serial.print("Die Kartennummer lautet:");
  Serial.println(code);
#endif
  sprintf(output, "Card:");
  ssd1306_printFixed2x(2, 0, output, STYLE_NORMAL);
  sprintf(output, "%ld", code);
  ssd1306_printFixed2x(2, OLED_hight, output, STYLE_NORMAL);
#endif

  Serial.print("#N");
  Serial.println(code_s);

  if (code == Wallbox)
  {
#ifdef SERIELL
    Serial.println("Relais2 OFF schalten ");
#endif
    load_stop();
    digitalWrite (Relais2, HIGH);
    delay(100);
    digitalWrite (EVSELED, LOW);
    Serial.println("#W0");
    Wallbox = 1;
    Wait = 10000;
  } else if (code && (Control > 1) && (Wallbox == 1))
  {
#ifdef SERIELL
    Serial.println("Relais2 ON ");
#endif
    digitalWrite (Relais2, LOW);
    delay(100);
    digitalWrite (EVSELED, HIGH);
    Serial.println("#W1");
    Wallbox = code;
    Wait = 10000;
    Typ2_Status();
  } else if (code == Control)
  {
#ifdef SERIELL
    Serial.println("Relais1 OFF schalten ");
#endif
    load_stop();
    digitalWrite (Relais2, HIGH);
    delay(100);
    digitalWrite (EVSELED, LOW);
    Serial.println("#W0");
    delay(10000);
    digitalWrite (Relais1, HIGH);
    Serial.println("#C0");
    Control = 1;
    Wallbox = 1;
    Wait = 10000;
  } else if (code && (Control == 1))
  {
#ifdef SERIELL
    Serial.println("Relais1 ON ");
#endif
    digitalWrite (Relais1, LOW);
    Serial.println("#C1");
    Control = code;
    Wallbox = 1;
    Wait = 1000;
  } else
  {
#ifdef NOSIM
    ssd1306_clearScreen();
#endif
    if ( code == 735600 ) {
#ifdef NOSIM
      sprintf(output, "MasterCard:");
      ssd1306_printFixed2x(2, 0, output, STYLE_NORMAL);
      sprintf(output, "%ld", code);
      ssd1306_printFixed2x(2, OLED_hight, output, STYLE_NORMAL);
      sprintf(output, "RESET INI", code);
      ssd1306_printFixed2x(2, OLED_hight * 2, output, STYLE_NORMAL);
#endif
      load_stop();
      digitalWrite (Relais2, HIGH);
      delay(100);
      digitalWrite (EVSELED, LOW);
      Serial.println("#W0");
      delay(10000);
      digitalWrite (Relais1, HIGH);
      Serial.println("#C0");
      Control = 1;
      Wallbox = 1;
      lacocaratscha();
    } else {
#ifdef NOSIM
      sprintf(output, "Card:");
      ssd1306_printFixed2x(2, 0, output, STYLE_NORMAL);
      sprintf(output, "%ld", code);
      ssd1306_printFixed2x(2, OLED_hight, output, STYLE_NORMAL);
      sprintf(output, "Cart not   match", code);
      ssd1306_printFixed2x(2, OLED_hight * 2, output, STYLE_NORMAL);
#endif
      sirene();
      Serial.println("#SR");
      delay(5000);
#ifdef NOSIM
      ssd1306_clearScreen();
#endif
      Wait = 1000;
    }
  }

#ifdef NOSIM
  ssd1306_clearScreen();
  sprintf(output, "Card:");
  ssd1306_printFixed2x(2, 0, output, STYLE_NORMAL);
  sprintf(output, "%ld", code);
  ssd1306_printFixed2x(2, OLED_hight, output, STYLE_NORMAL);
  if (Control > 1)
  {
    sprintf(output, "Contr. ON");
    ssd1306_printFixed2x(2, OLED_hight * 2, output, STYLE_NORMAL);
  } else
  {
    sprintf(output, "Contr. OFF", code);
    ssd1306_printFixed2x(2, OLED_hight * 2, output, STYLE_NORMAL);
  }
  if (Wallbox > 1)
  {
    sprintf(output, "W-box ON");
    ssd1306_printFixed2x(2, OLED_hight * 3, output, STYLE_NORMAL);
  } else
  {
    sprintf(output, "W-box OFF", code);
    ssd1306_printFixed2x(2, OLED_hight * 3, output, STYLE_NORMAL);
  }
  //  delay(Wait);
  mfrc522.PCD_AntennaOff();
#endif

}

#ifdef NOSIM
void screen_init() {
  ssd1306_clearScreen();
  ssd1306_normalMode();
  ssd1306_positiveMode();
  ssd1306_displayOn();
  anzloop = 0;
}
#endif

void Typ2_Status() {
      Ladekabel();
      Serial.print("#KW");
      Serial.println(Kabel_Wert, DEC);
      CP();
      Serial.print("#PW");
      Serial.println(CP_Spannung_Wert, DEC);
#ifdef STROM_MESSEN
      Ladestrom();
#endif
}
void read_command() {
  char code_s[12];
  long mqtt_contr = 0;

  if ( inputComplete ) {
    Serial.print("WBoxcmd: ");
    Serial.println(inputString);
    if ( inputString.indexOf("Stop") == 0 ) load_stop();
    if ( inputString.indexOf("Start") == 0 ) {
      if ((Control > 1) && (Wallbox == 1)) {
        digitalWrite (Relais2, LOW);
        delay(100);
        digitalWrite (EVSELED, HIGH);
        Serial.println("#W1");
        Wallbox = Control;
        delay(1000);
        Typ2_Status();
      } else Serial.println("#W0");
    }
    if ( inputString.indexOf("On ") == 0 ) {
      inputString.replace("On ", "");
      mqtt_contr = inputString.toFloat();
      if ( mqtt_contr < 10L ) {
        Serial.print("#N");
        if ( Control > 1 ) {
          sprintf(code_s, "%10.10ld", Control);
        } else if ( Wallbox > 2 ) {
          sprintf(code_s, "%10.10ld", Wallbox);
        } else sprintf(code_s, "%10.10ld", mqtt_contr);
        Serial.println(code_s);
        delay(50);
      } else {
        digitalWrite (Relais1, LOW);
        Serial.println("#C1");
        Control = mqtt_contr;
        Wallbox = 1;
      }
      delay(500);
    }
    if ( inputString.indexOf("Off") == 0 ) {
      load_stop();
      digitalWrite (Relais2, HIGH);
      delay(100);
      digitalWrite (EVSELED, LOW);
      Serial.println("#W0");
      Wallbox = 1;
      delay(500);
    }
    if ( inputString.indexOf("Sleep") == 0 ) {
      if ((Control > 1) && (Wallbox <= 1)) {
        digitalWrite (Relais2, HIGH);
        delay(100);
        digitalWrite (EVSELED, LOW);
        Serial.println("#W0");
        delay(500);
        digitalWrite (Relais1, HIGH);
        Serial.println("#C0");
        delay(500);
        beep();
        beep();
        beep();
        Control = 1;
        Wallbox = 2;
        inputString = "";
        inputComplete = false;
        anzloop = 1680;
        return;
      } else {
        Serial.println("#C1");
        delay(500);
      }
    }
    if ( inputString.indexOf("Card") == 0 ) {
      Serial.print("#N");
      if ( Control > 1 ) {
        sprintf(code_s, "%10.10ld", Control);
      } else if ( Wallbox > 2 ) {
        sprintf(code_s, "%10.10ld", Wallbox);
      } else sprintf(code_s, "%10.10ld", 0L);
      Serial.println(code_s);
      delay(500);
    }
    if ( inputString.indexOf("Init") == 0 ) {
      Serial.print("#N");
      if ( Control > 1 ) {
        sprintf(code_s, "%10.10ld", Control);
      } else if ( Wallbox > 2 ) {
        sprintf(code_s, "%10.10ld", Wallbox);
      } else sprintf(code_s, "%10.10ld", 0L);
      Serial.println(code_s);
      delay(500);
      if ( Control > 1 ) {
        Serial.println("#C1");
        delay(500);
      }
      else {
        Serial.println("#C0");
        delay(500);
      }
      if ( Wallbox > 2 ) {
        Serial.println("#W1");
        Serial.print("#K");
        Serial.println(Kabel, DEC);
        delay(500);
        Serial.print("#P");
        Serial.println(CP_Spannung, DEC);
        delay(500);
      }
      else {
        Serial.println("#W0");
        delay(500);
      }
    }
    if ( inputString.indexOf("Musik") == 0 ) stillenacht();
    if ( inputString.indexOf("Query") == 0 ) Typ2_Status();
    if ( inputString.indexOf("Help") == 0 ) {
      Serial.print("Help; ");
      Serial.print("Reset; ");
      Serial.print("Init; ");
      Serial.print("Query; ");
      Serial.print("On Nr; ");
      Serial.print("Card; ");
      Serial.print("Start; ");
      Serial.print("Stop; ");
      Serial.print("Off; ");
      Serial.print("Sleep; ");
      Serial.println("Musik;");
    }
    inputString = "";
    inputComplete = false;
    anzloop = 0;
#ifdef NOSIM
    screen_init();
#endif
  }
}

#ifdef SERIELL
void print_volt(float sensorValue) {
  float sensor_volt;

  Serial.print("value= ");
  Serial.print(sensorValue);
  Serial.print("; ");

  sensor_volt = (sensorValue / 1024) * 22.0;

  Serial.print("sensor_volt = ");
  Serial.print(sensor_volt);
  Serial.println("V; ");
  delay(1);
}
#endif

void setup()
{
  Serial.begin(9600);     // opens serial port, sets data rate to 9600 bps
  Serial.println("Setup: ");

  pinMode(tonepin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(Relais1, OUTPUT);
  pinMode(Relais2, OUTPUT);
  pinMode(EVSELED, OUTPUT);
  pinMode(EVSE_AN, OUTPUT);
  pinMode(I_PP, INPUT);
  pinMode(I_CP, INPUT);
  analogReference(DEFAULT);

  digitalWrite (Relais1, HIGH);
  delay(100);
  digitalWrite (Relais2, HIGH);
  delay(100);
  digitalWrite (EVSELED, LOW);
  delay(100);
  digitalWrite (EVSE_AN, LOW);
  delay(100);

  inputString.reserve(255);
  sirene();
  randomSeed(0L);


#ifdef NOSIM
  ssd1306_128x64_i2c_init();
  /* Select the font to use with menu and all font functions */
  ssd1306_setFixedFont(ssd1306xled_font6x8);
  /* Replace the line below with ssd1306_128x32_i2c_init() if you need to use 128x32 display */
  OLED_hight =  ssd1306_displayHeight() / 4;
  OLED_width =  ssd1306_displayWidth() / 10;
  // ssd1306_fillScreen( 0xFF );

  SPI.begin();
  mfrc522.PCD_Init();
#ifdef SERIELL_INIT
  Serial.println(F("*****************************"));
  Serial.println(F("MFRC522 Digital self test"));
  Serial.println(F("*****************************"));
  mfrc522.PCD_DumpVersionToSerial();  // Show version of PCD - MFRC522 Card Reader
  Serial.println(F("-----------------------------"));
  Serial.println(F("Only known versions supported"));
  Serial.println(F("-----------------------------"));
  Serial.println(F("Performing test..."));
  bool result = mfrc522.PCD_PerformSelfTest(); // perform the test
  Serial.println(F("-----------------------------"));
  Serial.print(F("Result: "));
  if (result) {
    Serial.println(F("OK"));
    ssd1306_printFixedN(0, 0, "OK", STYLE_NORMAL, FONT_SIZE_8X);
  }  else {
    ssd1306_printFixedN(0, 0, "NO", STYLE_NORMAL, FONT_SIZE_8X);
    Serial.println(F("DEFECT or UNKNOWN"));
    Serial.println();
  }
#endif
  inputString = "Init\n";

  ssd1306_fillScreen( 0x00 );
  ssd1306_printFixedN(10, OLED_hight, "INIT", STYLE_NORMAL, FONT_SIZE_4X);
#endif
  Serial.println("OK");
  beep();
  beep();
  delay(2000);

#ifdef NOSIM
  // ssd1306_clearScreen();
#endif
}

void serialEvent() {
  char inChar;
  while (Serial.available()) {
    inChar = Serial.read();
    inputString += inChar;
    if ((inChar == '\n') || (inChar == '\r')) {
      inputComplete = true;
    }
  }
}

void loop() {
  static long previousMillis = 0;
  unsigned long currentMillis = millis();
  char output[255];

  blinken();
  rfid_read();
  delay (MyTicks); // 0,5 Sekunden

#ifdef SERIELL_LOOP
  Serial.print("loop=");
  Serial.print(anzloop);
  Serial.print("; Control=");
  Serial.print(Control);
  Serial.print("; WBox=");
  Serial.print(Wallbox);
  Serial.print("; Delay=");
  Serial.println(MyTicks);
#endif

  read_command();

  if ((Control > 1) && (Wallbox > 1)) {
    if (currentMillis - previousMillis > (1000 * 60)) {
      previousMillis = currentMillis;
      Ladekabel();
      if ( lastKabel != Kabel ) {
        Serial.print("#K");
        Serial.println(Kabel, DEC);
        lastKabel = Kabel;
        /*    if ( Kabel == 0 ) {
                ssd1306_printFixedN(0, OLED_hight * 2, "  0A ", STYLE_NORMAL, FONT_SIZE_4X);
              } else if ( Kabel == 1 ) {
                ssd1306_printFixedN(0, OLED_hight * 2, " 14A ", STYLE_NORMAL, FONT_SIZE_4X);
              } else if ( Kabel == 2 ) {
                ssd1306_printFixedN(0, OLED_hight * 2, " 16A ", STYLE_NORMAL, FONT_SIZE_4X);
              } else if ( Kabel == 3 ) {
                ssd1306_printFixedN(0, OLED_hight * 2, " 20A ", STYLE_NORMAL, FONT_SIZE_4X);
              } else if ( Kabel == 4 ) {
                ssd1306_printFixedN(0, OLED_hight * 2, " 32A ", STYLE_NORMAL, FONT_SIZE_4X);
              }
        */
      }
      Serial.print("#KW");
      Serial.println(Kabel_Wert, DEC);

      CP();
      if ( lastSpannung != CP_Spannung ) {
        Serial.print("#P");
        Serial.println(CP_Spannung, DEC);
        ssd1306_clearScreen();
        if ( CP_Spannung == CP_OFF ) {
          ssd1306_printFixedN(0, OLED_hight, "CPOFF", STYLE_NORMAL, FONT_SIZE_4X);
        } else if ( CP_Spannung == PLUS_12V ) {
          ssd1306_printFixedN(0, OLED_hight, "CP ON", STYLE_NORMAL, FONT_SIZE_4X);
        } else if ( CP_Spannung == PLUS_9V ) {
          ssd1306_printFixedN(0, OLED_hight, " CAR ", STYLE_NORMAL, FONT_SIZE_4X);
        } else if ( CP_Spannung == PLUS_6V ) {
          ssd1306_printFixedN(0, OLED_hight, "LOAD ", STYLE_NORMAL, FONT_SIZE_4X);
        }
      }
      lastSpannung = CP_Spannung;
      Serial.print("#PW");
      Serial.println(CP_Spannung_Wert, DEC);

#ifdef STROM_MESSEN
      Ladestrom();
#endif
    }
  }

  anzloop++;
  if (anzloop < 5) MyTicks = 500;

#ifdef NOSIM
  if (((anzloop % (2 * 3)) == 0) && (anzloop < (2 * 7))) {
    if ((Control == 1) && (Wallbox == 0)) {
      ssd1306_clearScreen();
      ssd1306_printFixedN(10, OLED_hight, "OFF ", STYLE_NORMAL, FONT_SIZE_4X);
    }
    else if ((Control > 1) && (Wallbox > 1)) {
      ssd1306_clearScreen();
      sprintf(output, "C:%ld", Wallbox);
      ssd1306_printFixed2x(1, 0, output, STYLE_NORMAL);
      ssd1306_printFixedN(0, OLED_hight, "READY", STYLE_NORMAL, FONT_SIZE_4X);
    }
    else if (Control > 1) {
      ssd1306_clearScreen();
      sprintf(output, "C:%ld", Control);
      ssd1306_printFixed2x(1, 0, output, STYLE_NORMAL);
      ssd1306_printFixedN(10, OLED_hight, "C-ON ", STYLE_NORMAL, FONT_SIZE_4X);
    }
    else {
      ssd1306_clearScreen();
      ssd1306_printFixedN(10, OLED_hight, "WAIT ", STYLE_NORMAL, FONT_SIZE_4X);
    }
    // ssd1306_printFixedN(0, OLED_hight, "OFF", STYLE_NORMAL, FONT_SIZE_4X);

    mfrc522.PCD_AntennaOn(); // Reader aktivieren
  }

  if ((anzloop % (2 * 60 * 3)) == 0 )
  {
    ssd1306_displayOff();
    MyTicks = 1000;
  }
#endif

  if (((anzloop % (2 * 60 * 15)) == 0) && (Control > 1) && (Wallbox <= 1)) {
#ifdef NOSIM
    screen_init();
    ssd1306_printFixedN(0, 0, "OF", STYLE_NORMAL, FONT_SIZE_8X);
#endif
    delay(500);
    load_stop();
    digitalWrite (Relais2, HIGH);
    delay(100);
    digitalWrite (EVSELED, LOW);
    Serial.println("#W0");
    delay(500);
    digitalWrite (Relais1, HIGH);
    Serial.println("#C0");
    beep();
    beep();
    beep();
    delay(10000);
    //    lacocaratscha();
    Control = 1;
    Wallbox = 2;
#ifdef NOSIM
    ssd1306_printFixedN(10, OLED_hight, "WAIT ", STYLE_NORMAL, FONT_SIZE_4X);
#endif
    MyTicks = 5000;
  }
}
