/*Versione 20/02/23
  Registra tempo funzionamento apparecchio tramite pinza amperometrica e salva i dati su eprom se non trasmessi. Registra il tempo in minuti
  Invia  temperatura ed umidita di due sensori DHT11 se collegati ai pin 4 e 5 (versione DHt11 con resistenza !)
  Invia dati di 2 ingressi analogici su pin 6 e 7
  
  ATTENZIONE Modificare  il write API KEY per inviare ad altro canale Thingspeak
  ATTENZIONE Se si cambia SIM modificare nella libreria Adafruit_FONA.cpp l'apn (riga : apn = F("TM") oppure apn = F("hologram"))
  inserendo il giusto corrispondende in base al gestore della SIM
  ATTENZIONE : se si cambia luogo, la SIM ci mette un po' a connettersi. Fargli fare più tentativi, alzando il n. tentativi della registrazione su network. 
  Per inviare la SIM in Africa ho messo 90 tentativi (3 minuti di tentativi...). 
  Poi riportarli a 30, in modo che se non c'è sole sufficente per trasmettere non stia o troppo tempo senza registrare (mentre fa i tentativi non registra...).
  ATTENZIONE : se si scarica un aggiornamento della libreria adafruit_FONA, ricordarsi di riaggiornare l' apn in base al gestore della SIM

  Logica di questa versione :
  1. alimentazione da pannello solare con gestione casistiche di non inizializzazione corretta o di spegnimento per assenza di sole
  2. gestione mancata connessione per invio dati per n. motivi.
  3. salvataggio dati su EEPROM sistematica o comunque in caso di mancata connessione.
  4. auto-reset in caso di problemi (in pratica, l'operatore non deve mai intervenire)
  5. invio su Thingspeak di 8 dati : tempo funzionamento apparecchio pinza amperometrica (durataAttiva), tempo misurazione (durataAttiva+durataSpenta)
  (Questo secondo dato serve per capire per quanto tempo durante il giorno il sistema ha ricevuto potenza sufficiente dal pannello per registrare i dati.), 
  tempreatura e umidità sensore 1, temperatura umidità sensore 2, due ingressi analogici
  
  Logica delle impostazioni :
  Al mattino quando comincia ad arrivare il sole prova ad avviarsi. Se la potenza è insufficiente per inizializzare la SIM si ferma accende il led rosso, e riavvia il software dopo 5 minut.
  (a differenza delle versioni precedenti in cui se non riesce ad inizializzarsi si ferma ed occorre reset manuale).
  Durante il giorno salva i dati su EEPROM ad es. ogni ora. Invia i dati ad es. ogni 2 ore.
  Quando riesce ad inviare azzera contatore tempo salvataggio e quindi non salva i dati (risparmiando una scrittura).
  Se non riesce ad inviare va avanti a registrare e riprova dopo il tempo di invio (es: dopo 2 ore) a differenza delle versioni precedenti in cui se non riesce ad inviare si ferma ed occorre reset manuale).
  Nota :  ogni trasmissione sono circa 2 Kb. Con Holograma si ha 1 Mb al mese gratis, quindi max 12 trasmissioni al gg per non pagare. La EEPROM sopporta 100.000 cicli max di scrittura

Legenda colori:
  All'accensione :
    Verde fisso = inizializzazione
    Verde fisso + rosso fisso = errore inizializzazione. Ritenta dopo 5 minuti
  Durante il funzionamento :
    Verde lampeggiante = loop di campionamento sensore
    Lampeggio rosso lento : tentativo invio dati
    Verde fisso = dati inviati
    Rosso fisso = errore di comunicazione con la scheda SIM800, o errore di rete o errore GPRS. Dati non inviati. (l'effetto è led lampeggiante rosso e verde)
*/

#include <secTimer.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include "Adafruit_FONA.h" //Modificare nella libreria l’APN inserendo quello corretto in base al gestore della SIM
//ATTENZIONE Modificare nella libreria Adafruit_FONA.cpp l'apn (riga : apn = F("TM") )inserendo il giusto corrispondende in base al gestore della SIM
//Per Things Mobile : TM
//Per Hologram : hologram
//Istruzioni aggiuntive per salvataggio su Eprom
#include <EEPROM.h> 

int eepromaddress = 0; //indice array EEPROM
void clearEEPROM();
void EEPROMWritelong(); //Per gestire n. > 255
void EEPROMReadlong();  //Per gestire n. > 255
void writeEEPROMdata(); //Definisce Subroutine per scrivere dati eprom lunghi
void readEEPROMdata(); //Definisce Subroutine per scrivere dati eprom lunghi

//-------Sensore temperatura
#include "DHT.h"
#define DHT1PIN 4     //Digital pin connesso al sensore DHT
#define DHT1TYPE DHT11   // DHT 11
#define DHT2PIN 5     //Digital pin connesso al sensore DHT
#define DHT2TYPE DHT11   // DHT 11
DHT dht1(DHT1PIN, DHT1TYPE); // Inizializza sensore DHT.
DHT dht2(DHT2PIN, DHT2TYPE); // Inizializza sensore DHT.
//---------------------------------

//Dichiarazione di funzione che punta all'indirizzo zero
void(* Riavvia)(void) = 0; //Gestisce riavvio software se si blocca all'inizio (es. mattino per mancanza sole)

secTimer myTimer;

#define FONA_RX 9
#define FONA_TX 10
#define FONA_RST 2

SoftwareSerial SIM800ss = SoftwareSerial(FONA_TX, FONA_RX);
Adafruit_FONA SIM800 = Adafruit_FONA(FONA_RST);

char http_cmd[160]; //allungato a 160 per leggere 2 temperatura ed umidita e 2 analogici
char atm_time_string[20];  //Ore funzionamento
char atm_time_string2[20]; //Ore misurazione
char atm_time_string3[20]; //temperarura 1
char atm_time_string4[20]; //umidita 1
char atm_time_string5[20]; //temperarura 2
char atm_time_string6[20]; //umidita 2
char atm_time_string7[20]; //analogico 1
char atm_time_string8[20]; //analogico 2
int net_status;

uint16_t statuscode; //intero 16 bit senza segno = unsigned short
int16_t length; //intero 16 bit positivo
String response = "";
char buffer[512];

boolean connection_on = false; //Variabile per testare la connessione col network
boolean gprs_on = false;
boolean tcp_on = false;

int sensor = 3; //logica del sensore inversa: quando la pompa è attiva il segnale logico sul pin 3 è BASSO!
int greenLED = 11;
int redLED = 12;
int i = 0;

byte state = 1;
byte sensorValue = 0;

//Misurazione tempo pinza amperometrica
unsigned long seconds = 0;
unsigned long durataAttiva = 0; //tempo in secondi di rilevazione corrente pinza amperometrica
unsigned long durataSpenta = 0; //tempo in secondi di inattività della pinza amperometrica
unsigned long tempoAttSend = 0;    //Variabili di backup per invio al sito (cosa servono ?)
unsigned long tempoMis = 0;        //variabile backup tempo accensione per invio al sito
unsigned long INVIOsecond = 0;     //Contatore per invio dati

//Temperatura e umidita-----
int temperatura1 = 0;        //variabile backup temperatura 1 per invio al sito
int umidita1 = 0;
int temperatura2 = 0;        //variabile backup temperatura  2 per invio al sito
int umidita2 = 0;

//sensori anagiloci---------------
int analog1 = 0;
int analog2 = 0;

//Variabili per salvataggio dati su Eprom
unsigned long EEPROMsecond = 0;    //Contatore per salvatggio dati su Eprom
int NFailure = 0; //Conteggia  i 'mancati invii'
long address = 0; //Gestione dati long su EEPROM

/*----------------------------------------------------------------Valori modificabili----------------------------------------------*/
////ATTENZIONE Modificare eventualmente l'API KEY per inviare ad altro canale Thingspeak
//Per test : CH 2007543 - OQRYHKSC1LDPYTSL
//In basso l’indirizzo parziale del grafico Thingspeak che verrà successivamente completato in base alle informazioni proveniente dal sensore di hall.
char url_string[] = "http://api.thingspeak.com/update?api_key=OQRYHKSC1LDPYTSL&field1"; //URL del sito - non usare https ma http!!!
int tempoInvioDati = 3600; //Variabile per modificare il tempo di invio dei dati (in secondi)(es. 3600 = invio dei dati ogni H)

//Variabile per salvataggio su Eprom
int tempoEEPROMwrite = 3600;       //Variabile per modificare il tempo di salvataggio dei dati su EEPROM. (3600 = ogni ora). max 100.000 cicli di scrittura.
//Nota : in generale dovrebbe essere tempoInvioDati > tempoEEPROMwrite.
//Se riesce ad inviare comunque non salva. Se i due tempi sono uguali, il funzionamento è : salva solo se non riesce ad inviare./*---------------------------------------------------------------------------------------------------------------------------------*/

/*-----Questa parte è eseguita solo una volta all'accensione-----------------------------------*/
void setup() {
  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);
  pinMode(FONA_RST, OUTPUT);
  pinMode(sensor, INPUT);

  while (!Serial); //Attende che la porta seriale sia aperta
  Serial.begin(9600);
  digitalWrite(greenLED, HIGH);
  Serial.println(F("Initializzazione SIM800L...."));

  SIM800ss.begin(9600); // if you're using software serial
  if (! SIM800.begin(SIM800ss)) {
    Serial.println(F("SIM800L non trovata.."));
    Serial.println(F("Reset tra 5 minuti")); //Fa un reset se non trova la SIM
    digitalWrite(redLED, HIGH);
    delay (300000);  //300000 = 5 minuti
    digitalWrite(redLED, LOW);
    Serial.println(F("Riavvia"));
    delay (2000);
    Riavvia(); //Riavvia tutto
  }

  Serial.println(F("SIM800L is OK"));
  digitalWrite(redLED, LOW);
  delay(1000);
  SIM800ss.print("AT+CFUN=0\r\n"); //Predispone la SIM per la funzionalità minima (AT*CFUN=0) per consumare meno energia
  delay(8000);

  myTimer.startTimer(); //start del timer all'accensione del dispositivo
  digitalWrite(greenLED, LOW);

  Serial.print(F("Tempo invio dati: "));
  Serial.println(tempoInvioDati);

    //leggo il valori all'interno della EEPROM assegnandole alle variabili temporali relative allo stato della pompa
  readEEPROMdata();
  INVIOsecond = durataAttiva + durataSpenta; //Fa ripartire il contatore invio dati da dov'era rimasto.
  EEPROMsecond = durataAttiva + durataSpenta; //Fa ripartire il contatore salvataggio dati da dov'era rimasto.
  Serial.print(F("Tempo scrittura dati: "));
  Serial.println(tempoEEPROMwrite);
}

/*---------Questa sessione è eseguita ciclicamente ogni secondo---------------------------------------*/
void loop() {
  if (seconds != myTimer.readTimer()) { //controllo dei valori ogni secondo
    seconds = myTimer.readTimer();
    state ^= 1; //Funzione booleana xor. Inverte il valore della variablie state (byte)
    digitalWrite(greenLED, state);

    /*In questa sezione viene incrementato un contatore in base al passaggio di corrente.
      Se il sensore di hall rileva corrente viene incrementata la variabile durataAttiva altrimenti viene incrementata la variabile durataSpenta*/

    sensorValue = digitalRead(sensor); //lettura dell'input e incremento variabili
    if (sensorValue == 0) {
      durataAttiva++;
    }
    else {
      durataSpenta++;
    }

    /*Incrementa il contatore  per invio dati*/
    INVIOsecond++;
    EEPROMsecond++;

    /*Aggiunti io per test*/
    Serial.print(F("durataAttiva; durataSpenta; EEPROMsecond-min; INVIOsecond-min :"));
    Serial.print(durataAttiva);
    Serial.print("; ");
    Serial.print(durataSpenta);
    Serial.print("; ");
    Serial.print(EEPROMsecond / 60);
    Serial.print("; ");
    Serial.println(INVIOsecond / 60);
  }
  //--------------------------
  /*Quando il tempo di invio INVIOsecond raggiunge il valore corrispondente a tempoInvioDati
    (es.2 ore espresso in secondi ) viene inviato il pacchetto dati a Thingspeak eseguendo la funzione 'accensione_GPRS' (vedi dopo)*/

  if (INVIOsecond  >= tempoInvioDati) { //tentativo invio dei dati raccolti se è passato il tempo prestabilito
    digitalWrite(greenLED, LOW); //Spegne il led verde  durante l'invio
    tempoAttSend = durataAttiva / 60; //carico il tempo di attività in una variabile per l'invio dei dati e resetto i tempi
    tempoMis = (durataAttiva + durataSpenta) / 60; //Carico il tempo di totale di misurazione in una variabile per l'invio dei dati

    //Sensore temperatura e umidita-------------------------------
    Serial.println(F("-----Legge temperatura e umidita-----"));
    dht1.begin();
    dht2.begin();
    i = 0;
    while (i < 3) { //Legge 3 valori.
      delay(2000);
      int t1 = dht1.readTemperature();
      int h1 = dht1.readHumidity();
      int t2 = dht2.readTemperature();
      int h2 = dht2.readHumidity();
      // Verifica errori nella prima lettura.
      if (isnan(t1) || isnan(h1)) {
        Serial.println(F("Errore lettura dati sensore DHT1!"));
      }
      else {
        Serial.print(t1);
        Serial.println(F("°C - 1"));
        Serial.print(h1);
        Serial.println(F("% - 1"));
      }
      // Verifica errori nella seconda lettura.
      if (isnan(t2) || isnan(h2)) {
        Serial.println(F("Errore lettura dati sensore DHT2!"));
      }
      else {
        Serial.print(t2);
        Serial.println(F("°C - 2"));
        Serial.print(h2);
        Serial.println(F("% - 2"));
      }
      i++;
      delay(2000);
      temperatura1 = t1; //Carico la temperatura1 in una variabile per invio dati
      umidita1 = h1;
      temperatura2 = t2; //Carico la temperatura2 in una variabile per invio dati
      umidita2 = h2;
    }

    //Lettura Dati analogici
    Serial.println(F("-----Legge dati analogici-----"));
    int Pin6 = analogRead (A6);
    int Pin7 = analogRead (A7);
    Serial.print(Pin6);
    Serial.println(F(" V "));
    Serial.print(Pin7);
    Serial.println(F(" V "));
    analog1 = Pin6;
    analog2 = Pin7;

    //-------------------------------
    Serial.println(F("-----Tentativo accensione GPRS-----"));
    accensione_GPRS();
    Serial.println(F("-----Fine tentativo accensione GPRS-----"));

    /*----Invio dati.Esegue questa parte solo se l'accensione GPRS è andata a buon fine------*/
    if (gprs_on) {
      Serial.println(F("----Tentativo invio dati...-----"));
      dtostrf(tempoAttSend, 0, 0, atm_time_string); //Comando che trasforma una variabile float in stringa. In pratica, salva il tempo nella stringa 'atm_time_string'
      Serial.print(F("atm_time_string : "));
      Serial.println(atm_time_string);
      dtostrf(tempoMis, 0, 0, atm_time_string2); //Salva anche tempo misurazione (attiva+spenta)
      Serial.print(F("atm_time_string2 : "));
      Serial.println(atm_time_string2);
      //Temperatura1
      dtostrf(temperatura1, 0, 0, atm_time_string3); //Salva temperatura 1
      Serial.print(F("atm_time_string3 : "));
      Serial.println(atm_time_string3);
      //umidita1
      dtostrf(umidita1, 0, 0, atm_time_string4); //Salvare umidita 1
      Serial.print(F("atm_time_string4 : "));
      Serial.println(atm_time_string4);
      //Temperatura2
      dtostrf(temperatura2, 0, 0, atm_time_string5); //Salva temperatura 2
      Serial.print(F("atm_time_string3 : "));
      Serial.println(atm_time_string5);
      //umidita2
      dtostrf(umidita2, 0, 0, atm_time_string6); //Salva umidita 2
      Serial.print(F("atm_time_string4 : "));
      Serial.println(atm_time_string6);
      //Analogico pin 6
      dtostrf(analog1, 0, 0, atm_time_string7); //Salva analogico pin 6
      Serial.print(F("atm_time_string7 : "));
      Serial.println(atm_time_string7);
      //Analogico pin 7
      dtostrf(analog2, 0, 0, atm_time_string8); //Salva analogico pin7
      Serial.print(F("atm_time_string8 : "));
      Serial.println(atm_time_string8);

      sprintf(http_cmd, "%s=%s&field2=%s&field3=%s&field4=%s&field5=%s&field6=%s&field7=%s&field8=%s", url_string, atm_time_string, atm_time_string2, atm_time_string3, atm_time_string4, atm_time_string5, atm_time_string6, atm_time_string7, atm_time_string8); //Comando per concatenare in una stringa (http_cmd) più stringhe che prendono il posto dei %s
      Serial.println(http_cmd); //http_cmd è un char array che contiene l' API URL + il dato da inviare (durata attiva della pompa)
      delay(1000);

      /*---Creazione HTTP request tramite la funzione HTTP_GET_start. In pratica controlla se il server thingspeak è attivo-------*/
      i = 0;
      while (!tcp_on && i < 10) {  //Fa 10 tentativi.

        if (!SIM800.HTTP_GET_start(http_cmd, &statuscode, (uint16_t *)&length)) { //Statuscode is an integer that stores the server response and length is the length of the response
          Serial.println(F("---Server NOT OK---"));  //Il server non ha risposto, i dati non sono stati inviati.
          Serial.println(F("Trying again"));
          tcp_on = false;
          digitalWrite(redLED, HIGH); //Accende il led rosso. Significa : server non ha risposto
          i++; //Fa 10 tentativi

        } else {
          Serial.println(F("---Server OK. Response : "));  //Il server  ha risposto, mostro i dati della risposta
          tcp_on = true;
          while (length > 0) { //Non capisco cosa e 'lenght'. In ogni caso rappresenta la lunghezza dei dati registrati da qualche parte (nella sim ?)
            while (SIM800.available()) {
              char c = SIM800.read();
              response += c; //response=response + c
              length--; //-- decrementa lenght
            }
          }
          Serial.println(response); //Mostra i risultati di tutti gli invii della sessione di accensione. Si resetta ad ogni accensione
          if (statuscode == 200) {
            Serial.println(F("---Dati inviati!---"));
            digitalWrite(greenLED, LOW);
            digitalWrite(redLED, LOW);
            durataAttiva = 0; //Azzera solo se ha inviato idati
            durataSpenta = 0;
            Serial.println(F("---Clear EEPROM--")); //Aggiunto io per controllo
            clearEEPROM();
            NFailure = 0 ; //Aggiunto io. Azzera il n. di connessioni fallite.
            EEPROMsecond = 0; //Aggiunto io. Se ha inviato i dati, questi sono salvati e quindi il tempo per registrare su EEProm può ripartire.NFailure = 0 ; //Azzera il n. di connessioni fallite.
          }
        }
      }
      if (!tcp_on) {  //Se alla fine dei 10 tentativi il server non ha risposto...
        Serial.println(F("---Dati NON inviati! Nuovo tentativo dopo 'tempoinviodati'---"));
        NFailure++; //Conta le connessioni fallite.
      }
    } else {  //Se non si è avviato il gprs..
      Serial.println(F("---Dati non inviati. Nuovo tentativo dopo 'tempoinviodati' "));
      NFailure++; ///Conta le connessioni fallite.
    }

    tcp_on = false;
    gprs_on = false;
    connection_on = false;
    INVIOsecond = 0; //Resetta il contatore per invio dati sia che abbia inviato sia che non abbia inviato. Ritenterà dopo l'intervallo stabilito.
    SIM800ss.print("AT+CFUN=0\r\n"); //Spostato qui per farlo eseguire sempre dopo il tentativo di invio
    Serial.print(F("NFailure : ")); //Controllo connessioni fallite
    Serial.println(NFailure);

    //Riavvia tutto se per 2 volte non è riuscito a inviare dati. Succede infatti che la SIM si 'blocchi' se non riesce ad inviare...
    if (NFailure > 1) {
      delay (5000);
      Riavvia();
    }
  }
    //Spostato dopo invio dati, in modo che sa ha inviato, il conteggio EEPROMsecond si azzera e quindi non scrive nulla su eeprom risparmiando un ciclo di scrittura.
  if (EEPROMsecond >= tempoEEPROMwrite) { //Scrivo i dati raccolti nella EEPROM se è passato il tempo prestabilito
    Serial.println(F("---Scrittura dati su Eprom : "));
    writeEEPROMdata();
    EEPROMsecond = 0;
  }
}

/*---------------------------------------------------------------------------------------------------------------------------------
  //In questa sezione viene gestito il modulo SIM 800L. E' una routine chiamata solo quando occorre inviare i dati (vedi Void loop)*/
void accensione_GPRS()
{
  /*-----Registrazione su network-----*/
  SIM800ss.print("AT+CFUN=1\r\n");

  Serial.println(F("Waiting to be registered to network..."));
  i = 0;

  while (!connection_on && i < 60) {  //Fa 60 tentativi.
    digitalWrite(redLED, LOW);  //Ad ogni ciclo spegne il led rosso
    net_status = SIM800.getNetworkStatus(); //Legge lo stato del network
    Serial.print(F("i : "));
    Serial.print(i);
    Serial.print(F(" net_status : "));
    Serial.println(net_status);

    if (net_status == 1 || net_status == 5 ) {     //Connesso. Abilitazione con net_status == 5
      /*  net_status=1 significa 'connesso al network'. Net_status=5 significa 'connesso in roaming'.
        Con la SIM Thingsmobile a volte il net_status non va mai a  1,  resta solo 5. Per questo abbiamo messo nel while net_status != 5. */
      Serial.println(F("Registered to home network!"));
      connection_on = true; //Se si è connesso aggiorna questa variabile
      digitalWrite(greenLED, HIGH); //Accende il led verde se si connette
      digitalWrite(redLED, LOW); //Ripristina il led

    } else { //Se non si è connesso ...
      Serial.println(F("Not connected to network!"));
      connection_on = false;
      digitalWrite(redLED, HIGH);  //Led rosso se non si è connesso
      delay(2000); //Tempo di attesa 2 sec prima di riprovare. Con delay 2000  verifica per  max 3 min se si è connesso (90 tentativi x 2 secondi)
      i++;
    }

  }

  /*-----accensione gprs-----*/
  if (connection_on) {  //Faccio eseguire accensione gprs solo se prima si è connesso al network.
    Serial.println(F("Turning on GPRS... "));
    delay(2000);
    i = 0;

    while (!gprs_on && i < 10) { //Tentativi per avvio gprs.
      digitalWrite(redLED, LOW);  //Ad ogni ciclo spegne il led rosso

      if (!SIM800.enableGPRS(true)) {
        Serial.println("Failed to turn on GPRS");
        Serial.println("Trying again...");
        gprs_on = false;
        digitalWrite(redLED, HIGH);
        delay(2000);
        i++;

      } else {
        Serial.println("GPRS now turned on");
        delay(2000);
        gprs_on = true;
        digitalWrite(greenLED, HIGH); //Accende il led verde se si GPRS OK
        digitalWrite(redLED, LOW);
      }
    }
  }
}

/*--------------------------------------------------------------------------------------------------------------------------------
  Funzioni EEPROM eseguite solo quando occorre leggere scrivere o cancellare dati dalla EEPROM (vedi VOID loop)*/

void clearEEPROM()
{
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    if (EEPROM.read(i) != 0)                    //skip already "empty" addresses
    {
      EEPROM.write(i, 0);                       //write 0 to address i
    }
  }
  Serial.println(F("---EEPROM erased---"));
  eepromaddress = 0;
  delay(1000); //Aggiunto io
  //reset address counter
}

//This function will write a 4 byte (32bit) long to the eeprom at
//the specified address to address + 3.
void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

//This function will return a 4 byte (32bit) long from the eeprom
//at the specified address to address + 3.
long EEPROMReadlong(long address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void writeEEPROMdata()
{
  EEPROMWritelong(address, durataAttiva);
  Serial.print(F("durataAttiva registrata "));
  Serial.println(durataAttiva);
  address += 4;
  //Scrive durataSpenta.
  EEPROMWritelong(address, durataSpenta);
  Serial.print(F("durataSpenta registrata "));
  Serial.println(durataSpenta);
  address = 0;
}

void readEEPROMdata()
{
  durataAttiva = EEPROMReadlong(0); //Legge i dati dall'indirizzo 0 al 3
  Serial.print(F("durataAttiva letta : "));
  Serial.println(durataAttiva);
  durataSpenta = EEPROMReadlong(4); //Legge i dati dall'indirizzo 4 al 7
  Serial.print(F("durataSpenta letta : "));
  Serial.println(durataSpenta);
}
