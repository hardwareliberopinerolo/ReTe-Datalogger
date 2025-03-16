void setup_corrente() {
Serial.print("ACS712_LIB_VERSION: ");
  Serial.println(ACS712_LIB_VERSION);
  ACS.autoMidPoint(20);
  Serial.print("Valore Medio: ");
  Serial.println(ACS.getMidPoint());
}

void legge_corrente() {
  int mA = ACS.mA_DC(10);
  Serial.print("Corrente in mA: ");
  Serial.print(mA);
  Ampere =floor((float(mA)/1000.0)*10)/10.0;  //Esempio da https://www.delftstack.com/howto/arduino/arduino-round/
  Serial.print(" - A: ");
  Serial.println(Ampere);
  
}  
