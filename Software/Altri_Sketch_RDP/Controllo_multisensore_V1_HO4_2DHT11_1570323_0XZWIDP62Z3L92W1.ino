/*Versione 14/05/22
  Sketch per primo modello di circuito senza eprom.
  Salva i dati in minuti
  Salva anche la temperatura ed umidita di due sensori DHT11 se collegati ai pin 4 e/o 5 (versione DHt11 con resistenza !)
  Verificato il 14/06/22 con tutti i 3 sensori --> rileva e trasmette 6 dati : 
  misurazione e funzionamento, temp e umidità sensore 1, temp e umidità sensore 2
  
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

//-------Sensore temperatura
#include "DHT.h"
#define DHT1PIN 4     //Digital pin connesso al sensore DHT
#define DHT1TYPE DHT11   // DHT 11
#define DHT2PIN 5     //Digital pin connesso al sensore DHT
#define DHT2TYPE DHT11   // DHT 11
DHT dht1(DHT1PIN, DHT1TYPE); // Inizializza sensore DHT.
DHT dht2(DHT2PIN, DHT2TYPE); // Inizializza sensore DHT.
//---------------------------------

//Aggiunto io. Dichiarazione di funzione che punta all'indirizzo zero
void(* Riavvia)(void) = 0; //aggiunto io per gestire riavvio software se si blocca all'inizio (es. mattino per mancanza sole)

secTimer myTimer;

#define FONA_RX 9
#define FONA_TX 10
#define FONA_RST 2

SoftwareSerial SIM800ss = SoftwareSerial(FONA_TX, FONA_RX);
Adafruit_FONA SIM800 = Adafruit_FONA(FONA_RST);

char http_cmd[120]; //allungato da 80 a 120 per leggere temperatura ed umidita
char atm_time_string[20];
char atm_time_string2[20]; //Aggiunto io
char atm_time_string3[20]; //temperarura 1
char atm_time_string4[20]; //umidita 1
char atm_time_string5[20]; //temperarura 2
char atm_time_string6[20]; //umidita 2
int net_status;

uint16_t statuscode; //intero 16 bit senza segno = unsigned short
int16_t length; //intero 16 bit positivo
String response = "";
char buffer[512];

boolean connection_on = false; //aggiunto io per testare la connessione col network
boolean gprs_on = false;
boolean tcp_on = false;

int sensor = 3; //logica del sensore invesa: quando la pompa è attiva il segnale logico sul pin 3 è BASSO!
int greenLED = 11;
int redLED = 12;
int i = 0;

byte state = 1;
byte sensorValue = 0;

unsigned long seconds = 0;
unsigned long durataAttiva = 0; //tempo in secondi di attivazione della pompa
unsigned long durataSpenta = 0; //tempo in secondi di inattività della po
unsigned long tempoAttSend = 0;    //Variabili di backup per invio al sito (cosa servono ?)
unsigned long tempoMis = 0;        //Aggiunto io : variabile backup tempo accensione per invio al sito
unsigned long INVIOsecond = 0;     //Aggiunto io. Contatore per invio dati
int NFailure = 0; //Aggiunto io. Conteggia  i 'mancati invii'
int temperatura1 = 0;        //Aggiunto io : variabile backup temperatura per invio al sito
int umidita1 = 0;
int temperatura2 = 0;        //Aggiunto io : variabile backup temperatura per invio al sito
int umidita2 = 0;

/*----------------------------------------------------------------Valori modificabili----------------------------------------------*/
////ATTENZIONE Modificare eventualmente l'API KEY per inviare ad altro canale Thingspeak
//In basso l’indirizzo parziale del grafico Thingspeak che verrà successivamente completato in base alle informazioni proveniente dal sensore di hall.
char url_string[] = "http://api.thingspeak.com/update?api_key=0XZWIDP62Z3L92W1&field1"; //URL del sito - non usare https ma http!!!
int tempoInvioDati = 120; //Variabile per modificare il tempo di invio dei dati (in secondi)(es. 3600 = invio dei dati ogni H)
/*---------------------------------------------------------------------------------------------------------------------------------*/

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
    Serial.println(F("Reset tra 5 minuti")); //Aggiunto io
    digitalWrite(redLED, HIGH); //Messo io su high
    //Parte sotto modificata io. Prima stava in pausa aspettando un reset
    delay (300000);  //300000 = 5 minuti
    digitalWrite(redLED, LOW); //Aggiunto io
    Serial.println(F("Riavvia")); //Aggiunto io
    delay (2000);
    Riavvia(); //Aggiunto io. Riavvia tutto
  }

  Serial.println(F("SIM800L is OK"));
  digitalWrite(redLED, LOW); //Aggiunto io
  delay(1000);
  SIM800ss.print("AT+CFUN=0\r\n"); //Predispone la SIM per la funzionalità minima (AT*CFUN=0) per consumare meno energia
  delay(8000);

  myTimer.startTimer(); //start del timer all'accensione del dispositiv
  digitalWrite(greenLED, LOW);

  Serial.print(F("Tempo invio dati: ")); // Aggiunto io
  Serial.println(tempoInvioDati); // Aggiunto io
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
    INVIOsecond++; //aggiunto io

    /*Aggiunti io per test*/
    Serial.print(F("durataAttiva; durataSpenta; INVIOsecond :"));
    Serial.print(durataAttiva);
    Serial.print("; ");
    Serial.print(durataSpenta);
    Serial.print("; ");
    Serial.println(INVIOsecond);

  }
  //--------------------------
  /*Quando il tempo di invio INVIOsecond raggiunge il valore corrispondente a tempoInvioDati
    (es.2 ore espresso in secondi ) viene inviato il pacchetto dati a Thingspeak eseguendo la funzione 'accensione_GPRS' (vedi dopo)*/

  if (INVIOsecond  >= tempoInvioDati) { //tentativo invio dei dati raccolti se è passato il tempo prestabilito
    digitalWrite(greenLED, LOW); //Spegne il led verde  durante l'invio
    tempoAttSend = durataAttiva / 60; //carico il tempo di attività della pompa in una variabile per l'invio dei dati e resetto i tempi
    tempoMis = (durataAttiva + durataSpenta) / 60; //Aggiunto io. carico il tempo di totale di misurazione in una variabile per l'invio dei dati

    //Sensore temperatura-------------------------------
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
      temperatura1 = t1; //Aggiunto io. carico la temperatura in una variabile per invio dati
      umidita1 = h1;
      temperatura2 = t2; //Aggiunto io. carico la temperatura in una variabile per invio dati
      umidita2 = h2; 
    }

    //-------------------------------
    Serial.println(F("-----Tentativo accensione GPRS-----")); //Aggiunto io
    accensione_GPRS();
    Serial.println(F("-----Fine tentativo accensione GPRS-----")); //Aggiunto io

    /*----Invio dati.Aggiunto io 'if gprs_on' : esegue questa parte solo se l'accensione GPRS è andata a buon fine------*/
    if (gprs_on) {
      Serial.println(F("----Tentativo invio dati...-----")); //Aggiunto io

      dtostrf(tempoAttSend, 0, 0, atm_time_string); //Comando che trasforma una variabile float in stringa. In pratica, salva il tempo nella stringa 'atm_time_string'
      Serial.print(F("atm_time_string : ")); //Aggiunto io per controllo
      Serial.println(atm_time_string);
      dtostrf(tempoMis, 0, 0, atm_time_string2); //Aggiunto io per salvare anche tempo misurazione (attiva+spenta)
      Serial.print(F("atm_time_string2 : ")); //Aggiunto io per controllo secondo campo
      Serial.println(atm_time_string2);
      //Temperatura1
      dtostrf(temperatura1, 0, 0, atm_time_string3); //Aggiunto io per salvare anche temperatura 1
      Serial.print(F("atm_time_string3 : ")); //Aggiunto io per controllo secondo campo
      Serial.println(atm_time_string3);
      //umidita1
      dtostrf(umidita1, 0, 0, atm_time_string4); //Aggiunto io per salvare anche anche umidita 1
      Serial.print(F("atm_time_string4 : ")); //Aggiunto io per controllo secondo campo
      Serial.println(atm_time_string4);
        //Temperatura2
      dtostrf(temperatura2, 0, 0, atm_time_string5); //Aggiunto io per salvare anche temperatura 1
      Serial.print(F("atm_time_string3 : ")); //Aggiunto io per controllo secondo campo
      Serial.println(atm_time_string5);
      //umidita2
      dtostrf(umidita2, 0, 0, atm_time_string6); //Aggiunto io per salvare anche anche umidita 1
      Serial.print(F("atm_time_string4 : ")); //Aggiunto io per controllo secondo campo
      Serial.println(atm_time_string6);

      sprintf(http_cmd, "%s=%s&field2=%s&field3=%s&field4=%s&field5=%s&field6=%s", url_string, atm_time_string, atm_time_string2, atm_time_string3, atm_time_string4, atm_time_string5, atm_time_string6); //Comando per concatenare in una stringa (http_cmd) più stringhe che prendono il posto dei %s
      Serial.println(http_cmd); //http_cmd è un char array che contiene l' API URL + il dato da inviare (durata attiva della pompa)
      delay(1000);

      /*---Creazione HTTP request tramite la funzione HTTP_GET_start. In pratica controlla se il server thingspeak è attivo-------*/
      i = 0; //Aggiunto io
      while (!tcp_on && i < 10) {  //Aggiunto io per fargli fare 10 tentativi.

        if (!SIM800.HTTP_GET_start(http_cmd, &statuscode, (uint16_t *)&length)) { //Statuscode is an integer that stores the server response and length is the length of the response
          Serial.println(F("---Server NOT OK---"));  //Aggiunto io. Il server non ha risposto, i dati non sono stati inviati.
          Serial.println(F("Trying again"));  //Aggiunto io.
          tcp_on = false;
          digitalWrite(redLED, HIGH); //Accende il led rosso. Significa : server non ha risposto
          i++; //Aggiunto io per fargli fare 10 tentativi

        } else {
          Serial.println(F("---Server OK. Response : "));  //Aggiunto io. Il server  ha risposto, mostro i dati della risposta
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
            digitalWrite(redLED, LOW);  //Aggiunto io
            durataAttiva = 0; //spostati io qui. Azzera solo se ha inviato idati
            durataSpenta = 0;
            NFailure = 0 ; //Aggiunto io. Azzera il n. di connessioni fallite.
          }
        }
      }
      if (!tcp_on) {  //Aggiunto io. Se alla fine dei 10 tentativi il server non ha risposto...
        Serial.println(F("---Dati NON inviati! Nuovo tentativo dopo 'tempoinviodati'---"));  //Aggiunto io
        NFailure++; //Aggiunto io per contare le connessioni fallite.
      }
    } else {  //Se non si è avviato il gprs..
      Serial.println(F("---Dati non inviati. Nuovo tentativo dopo 'tempoinviodati' "));  //Aggiunto io
      NFailure++; ///Aggiunto io per contare le connessioni fallite.
    }

    tcp_on = false; //Aggiunto io
    gprs_on = false; //Spostato io qui
    connection_on = false; //aggiunto io
    INVIOsecond = 0; //Aggiunto io. Resetta il contatore per invio dati sia che abbia inviato sia che non abbia inviato. Ritenterà dopo l'intervallo stabilito.

    SIM800ss.print("AT+CFUN=0\r\n"); //Spostato io qui per farlo eseguire sempre dopo il tentativo di invio
    Serial.print(F("NFailure : ")); //Aggiunto io per controllo connessioni fallite
    Serial.println(NFailure);

    //Aggiunto io. Riavvia tutto se per 3 volte non è riuscito a inviare dati. Succede infatti che la SIM si 'blocchi' se non riesce ad inviare...
    if (NFailure > 2) {
      delay (5000);
      Riavvia();
    }
  }
}

/*---------------------------------------------------------------------------------------------------------------------------------
  //In questa sezione viene gestito il modulo SIM 800L. E' una routine chiamata solo quando occorre inviare i dati (vedi Void loop)*/
void accensione_GPRS()
{
  /*-----Registrazione su network-----*/
  SIM800ss.print("AT+CFUN=1\r\n");

  Serial.println(F("Waiting to be registered to network..."));
  i = 0; //Aggiunto io

  //net_status = SIM800.getNetworkStatus(); Tolto io

  while (!connection_on && i < 90) {  //Modificato io per fargli fare 90 tentativi.
    digitalWrite(redLED, LOW);  //Aggiunto io. Ad ogni ciclo spegne il led rosso
    net_status = SIM800.getNetworkStatus(); //Legge lo stato del network
    Serial.print(F("i : ")); //Aggiunto io per controllo
    Serial.print(i);  //Aggiunto io per controllo
    Serial.print(F(" net_status : ")); //Aggiunto io per controllo
    Serial.println(net_status);  //Aggiunto io per controllo

    if (net_status == 1 || net_status == 5 ) {     //Connesso. Aggiunto io abilitazione con net_status == 5
      /*  net_status=1 significa 'connesso al network'. Net_status=5 significa 'connesso in roaming'.
        Con la SIM Thingsmobile a volte il net_status non va mai a  1,  resta solo 5. Per questo abbiamo messo nel while net_status != 5. */
      /*Nota : se non risulta connesso, va in standby fisso, occorre un reset manuale per farlo ripartire...*/
      Serial.println(F("Registered to home network!"));
      connection_on = true; //Aggiunto io. Se si è connesso aggiorna questa variabile
      digitalWrite(greenLED, HIGH); //Aggiunto io. Accende il led verde se si connette
      digitalWrite(redLED, LOW); //Aggiunto io. Ripristina il led

    } else { //Se non si è connesso ...
      Serial.println(F("Not connected to network!")); //aggiunto io.
      connection_on = false; //Aggiunto io
      digitalWrite(redLED, HIGH);  //Aggiunto io.Led rosso se non si è connesso
      delay(2000); //Tempo di attesa 2 sec prima di riprovare. Con delay 2000  verifica per  max 3 min se si è connesso (90 tentativi x 2 secondi)
      i++;
    }

  }

  /*-----accensione gprs-----*/
  if (connection_on) {  //Aggiunto io. Faccio eseguire accensione gprs solo se prima si è connesso al network.
    Serial.println(F("Turning on GPRS... "));
    delay(2000);
    i = 0;

    while (!gprs_on && i < 10) { //Tentativi per avvio gprs.
      digitalWrite(redLED, LOW);  //Aggiunto io. Ad ogni ciclo spegne il led rosso

      if (!SIM800.enableGPRS(true)) {
        Serial.println("Failed to turn on GPRS");
        Serial.println("Trying again...");
        gprs_on = false;
        digitalWrite(redLED, HIGH);  //Aggiunto io.
        delay(2000);
        i++;

      } else {
        Serial.println("GPRS now turned on");
        delay(2000);
        gprs_on = true;
        digitalWrite(greenLED, HIGH); //Aggiunto io. Accende il led verde se si GPRS OK
        digitalWrite(redLED, LOW);  //Aggiunto io.
      }
    }
  }
}
