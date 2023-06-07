#include <MultiShiftRegister.h>

const int nSteps = 8;   //numero de steps/notas do loop
int currentStep = -1;   //step atual da timeline

float duration_us, distance_cm;

int trigPin = 9;
int echoPin = 10; 
int bankPin = 8;
int clearPin = 5;
int Q8 = A0;
int CLK = A1;
int LATCH = A2;

long stepButtons[nSteps][2];  //info dos botoes (index->pin;values->preState,last debounce)
long lastPress;

int mix[nSteps];       //registo da mistura de todos os canais
int kick[nSteps][2];   //registo do canal kick
int snare[nSteps][2];  //registo do canal snare
int hihat[nSteps][2];  //registo do canal hihat
int synth[nSteps][2];  //registo do canal synth
int bank[nSteps][2];    //auxilar de display
int currentBank = 0;   //instrumento que esta selecionado: 0-> kick; 1-> snare; 2-> hihat
int nBanks = 4;

long debounceDelay = 40;  //delay do botao
long proxDelay = 10;  //delay do botao
long bankPreState = 0;   //estado do ultimo clique do botao 8
long bankLastClick = 0;  //instante do ultimo clique do botao 8
long clearPreState = 0;   //estado do ultimo clique do botao 5
long clearLastClick = 0;  //instante do ultimo clique do botao 5

String message = "";
float bpmLimitUp = 600;
float bpmLimitDown = 60;
int prox = 100;
int lastProx = 100;
long lastProxTime = 0;
boolean switched = false;

MultiShiftRegister leds (1 , 3, 4, 2); 
MultiShiftRegister seven (1 , 12, 13, 11); 

void setup() {

  Serial.begin(9600);

  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  
  pinMode(bankPin, INPUT);
  pinMode(clearPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(Q8, INPUT);
  pinMode(CLK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  for (int i = 0; i < nSteps; i++) {
    stepButtons[i][0] = LOW;  //inicializar preState botoes
    stepButtons[i][1] = 0;    //tempo desde ultimo clique

    mix[i] = 0;  //inicializar mix a -1 (silencio)

    kick[i][0] = 0;  //inicializar preState kick 
    kick[i][1] = 0;  //inicializar registo kick
    snare[i][0] = 0;  //inicializar preState snare
    snare[i][1] = 0;  //inicializar registo snare
    hihat[i][0] = 0;  //inicializar preState hihat
    hihat[i][1] = 0;  //inicializar registo hihat
    synth[i][0] = 0;  //inicializar preState synth
    synth[i][1] = 0;  //inicializar registo synth
  }

  updateBankLed();
  //duration_us = pulseIn(echoPin, HIGH);
  //distance_cm = 0.017 * duration_us;
  //prox = int(100*distance_cm/50);
  //lastProx = prox;
}

void loop() {
  float p = analogRead(A3);
  message = "";

  int bankRead = digitalRead(bankPin);
  int clearRead = digitalRead(clearPin);

  //generate 10-microsecond pulse to TRIG pin
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(50);
  digitalWrite(trigPin, LOW);
  
  // calcular distancia

  duration_us = pulseIn(echoPin, HIGH);
  distance_cm = (0.017 * duration_us-13); //-13 para calibracao
  if (100*distance_cm/50 < 100 && lastProx>=100) switched = true;


  if (distance_cm > 50) {
      if ((millis() - lastProxTime) > proxDelay) {
      if (prox >= 0) {
        if (prox + 3 > 100) prox = 100;
        else prox+= 3;
        lastProxTime = millis();
      }
      lastProx = prox;
    }
  }
  else {
     if (lastProx - 100*distance_cm/50 > 20) {
      if (prox > int(100*distance_cm/50)) { 
        if (prox - 3 < int(100*distance_cm/50)) prox = int(100*distance_cm/50);
        else prox-=3;
      }
    }
    else {
      if (prox < int(100*distance_cm/50)) { 
        if (prox + 3 > int(100*distance_cm/50)) prox = int(100*distance_cm/50);
        else prox+=3;
      }
    }
    //if (abs(lastProx - 100*distance_cm/50) > 20 && lastProx != 100) prox = lastProx;
    //if (prox != lastProx) lastProxTime = millis();
    if (prox > 100) prox = 100;
    if (prox < 0) prox = 0;
    lastProx = prox;
  }

  Serial.print(distance_cm);
  Serial.print(" ");
  Serial.println(prox);

  //leitura do botao de troca de banks
  if ((millis() - bankLastClick) > debounceDelay) {
    if (bankRead != bankPreState) {
      bankPreState = bankRead;
      bankLastClick = millis();
      if (bankPreState == 1) {
          if (currentBank < nBanks - 1) currentBank += 1;
          else currentBank = 0;
            //updade do led 7seg
            updateBankLed();
      }
    }
  }

  //leitura do botao clear
  if ((millis() - clearLastClick) > debounceDelay) {
    if (clearRead != clearPreState) {
      clearPreState = clearRead;
      clearLastClick = millis();
      if (clearPreState == 1) clearTimelines();
    }
  }

  //identificacao do bank atual
  copyBank();

  int buttonPress, value;

  if (get_pins() > 0) {
    buttonPress = int(log(get_pins())/log(2));
    if (millis() - lastPress < debounceDelay) {
       bank[buttonPress][1] = !bank[buttonPress][1];
    }
  }
    else {
      lastPress = millis();
    }

  for (int i = 0; i < nSteps; i++) {

    //feedback leds
    if (checkAllLeds() && i == currentStep) leds.clear(i);
    else if (bank[i][1] == HIGH) leds.set(i);
    else if (i == currentStep) leds.set(i);
    else leds.clear(i);
    leds.shift();

    //guardar alteracoes no bank atual
    storeBank();

    //atualizacao do mix
    updateMix(i);
  }

  //leitura de mensagens vindas do Pure Data
  while (Serial.available()) {
    char incomingByte = (char)Serial.read();
    if (currentStep == -1 && incomingByte == 'S') currentStep = 0;
    else if (currentStep != -1 && incomingByte == 'C') {
      if (currentStep < nSteps - 1) currentStep += 1;
      else currentStep = 0;
    }
  }

  //construcao da mensagem e envio da info guardada dos slots + variacao dos BPMs
  message = messageBuild(p);
  Serial.println(message);
}

//atualizacao do mix dos canais
// 0:[] 1:K 2:S 3:H 4:I 5:KS 6:KH 7:KI 8:SH 9:SI 10:HI 11:KSH 12:KSI 13:KHI 14:SHI 15:KSHI
void updateMix(int i) {
         if (kick[i][1] == 0 && snare[i][1] == 0 && hihat[i][1] == 0 && synth[i][1] == 0) mix[i] = 0;
    else if (kick[i][1] == 1 && snare[i][1] == 0 && hihat[i][1] == 0 && synth[i][1] == 0) mix[i] = 1;
    else if (kick[i][1] == 0 && snare[i][1] == 1 && hihat[i][1] == 0 && synth[i][1] == 0) mix[i] = 2;
    else if (kick[i][1] == 0 && snare[i][1] == 0 && hihat[i][1] == 1 && synth[i][1] == 0) mix[i] = 3;
    else if (kick[i][1] == 0 && snare[i][1] == 0 && hihat[i][1] == 0 && synth[i][1] == 1) mix[i] = 4;
    else if (kick[i][1] == 1 && snare[i][1] == 1 && hihat[i][1] == 0 && synth[i][1] == 0) mix[i] = 5;
    else if (kick[i][1] == 1 && snare[i][1] == 0 && hihat[i][1] == 1 && synth[i][1] == 0) mix[i] = 6; 
    else if (kick[i][1] == 1 && snare[i][1] == 0 && hihat[i][1] == 0 && synth[i][1] == 1) mix[i] = 7;
    else if (kick[i][1] == 0 && snare[i][1] == 1 && hihat[i][1] == 1 && synth[i][1] == 0) mix[i] = 8;
    else if (kick[i][1] == 0 && snare[i][1] == 1 && hihat[i][1] == 0 && synth[i][1] == 1) mix[i] = 9;
    else if (kick[i][1] == 0 && snare[i][1] == 0 && hihat[i][1] == 1 && synth[i][1] == 1) mix[i] = 10;
    else if (kick[i][1] == 1 && snare[i][1] == 1 && hihat[i][1] == 1 && synth[i][1] == 0) mix[i] = 11;
    else if (kick[i][1] == 1 && snare[i][1] == 1 && hihat[i][1] == 0 && synth[i][1] == 1) mix[i] = 12;
    else if (kick[i][1] == 1 && snare[i][1] == 0 && hihat[i][1] == 1 && synth[i][1] == 1) mix[i] = 13;
    else if (kick[i][1] == 0 && snare[i][1] == 1 && hihat[i][1] == 1 && synth[i][1] == 1) mix[i] = 14;
    else if (kick[i][1] == 1 && snare[i][1] == 1 && hihat[i][1] == 1 && synth[i][1] == 1) mix[i] = 15;
}

//construcao da mensagem a enviar para o Pure Data
String messageBuild(float p) {
  String s = "";
  for (int i = 0; i < nSteps; i++) {
    if (mix[i] < 10) s = s + String(0) + String(mix[i]);
    else s = s + String(mix[i]);
  }
  if (int(bpmLimitDown + (bpmLimitUp - bpmLimitDown) * p / 1023) < 100) s = s + String(0) + String(int(bpmLimitDown + (bpmLimitUp - bpmLimitDown) * p / 1023)) + String(100-prox) + '\0';
  else s = s + String(int(bpmLimitDown + (bpmLimitUp - bpmLimitDown) * p / 1023)) + String(100-prox) + '\0';
  return s;
}

bool checkAllLeds() {
  for (int i = 0; i < nSteps; i++) if (bank[i][1] == LOW) return false;
  return true;
}

void updateBankLed() {
  for (int i = 0; i<8; i++) seven.clear(i);
  seven.shift();

  switch(currentBank) {
      case 0: 
        seven.set(5); seven.set(2);
      break;
      case 1:
        seven.set(6); seven.set(5); seven.set(0); seven.set(4); seven.set(3);
      break;
      case 2:
        seven.set(6); seven.set(5); seven.set(0); seven.set(2); seven.set(3);
      break;
      case 3:
        seven.set(7); seven.set(5); seven.set(0); seven.set(2);
      break;
  }

  seven.shift();
}

void clearTimelines() {
   for (int i = 0; i < nSteps; i++) {
     kick[i][1] = 0;
     snare[i][1] = 0;
     hihat[i][1] = 0;
     synth[i][1] = 0;
   }
}

//identificacao do bank atual
void copyBank() {
    switch(currentBank) {
      case 0: 
      for (int i = 0; i < nSteps; i++) {
        bank[i][0] = kick[i][0];
        bank[i][1] = kick[i][1];
      }
      break;
      case 1: 
      for (int i = 0; i < nSteps; i++) {
        bank[i][0] = snare[i][0];
        bank[i][1] = snare[i][1];
      }
      break; 
      case 2: 
      for (int i = 0; i < nSteps; i++) {
        bank[i][0] = hihat[i][0];
        bank[i][1] = hihat[i][1];
      }
      break;
    case 3: 
      for (int i = 0; i < nSteps; i++) {
        bank[i][0] = synth[i][0];
        bank[i][1] = synth[i][1];
      }
      break;
  }
}

//guardar alteracpes no bank atual
void storeBank() {
    switch(currentBank) {
      case 0: 
      for (int i = 0; i < nSteps; i++) {
        kick[i][0] = bank[i][0];
        kick[i][1] = bank[i][1];
      }
      break;
      case 1: 
      for (int i = 0; i < nSteps; i++) {
        snare[i][0] = bank[i][0];
        snare[i][1] = bank[i][1];
      }
      break; 
      case 2: 
      for (int i = 0; i < nSteps; i++) {
        hihat[i][0] = bank[i][0];
        hihat[i][1] = bank[i][1];
      }
      break;
      case 3: 
      for (int i = 0; i < nSteps; i++) {
        synth[i][0] = bank[i][0];
        synth[i][1] = bank[i][1];
      }
      break;
    }
}

void trigger( int pin )
{
    digitalWrite(pin,HIGH);
    digitalWrite(pin,LOW);
}

int get_pins() {
    trigger(LATCH);
    int value = 0;
    for( int i = 0; i < 8; ++i ) {
        value = value << 1;
        value += get_one();
    }
    return value;
}

int get_one() {
    int value = digitalRead(Q8);
    trigger(CLK);
    return value;
}
