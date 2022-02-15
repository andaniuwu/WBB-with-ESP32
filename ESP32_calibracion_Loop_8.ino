#include <SPI.h>  //Librería para comunicación con SD
#include "SD.h"   //Librería de SD
#include <Wire.h>   // Librería de I2C para RTC y OLED
#include <Adafruit_GFX.h>   //Librería general para pantallas
#include <Adafruit_SSD1306.h> //Librería para OLED
#include "RTClib.h"   //Librería para RTC



 
File myFile; //Crea instancia para direccionar datos a SD

#define sclk    17      // TX pin SCLK ADS1222 1 y 2, entrada de reloj de serie -D2
#define Dout1   32       // pin Dout ADS1222 1, DRDY/DOUT drdy-> nivel bajo datos a leer listos, salida de datos por flaco positivo de slck, MSB primero -D3
#define Dout2   4       // pin Dout ADS1222 2, DRDY/DOUT drdy-> nivel bajo datos a leer listos, salida de datos por flaco positivo de slck, MSB primero -D4
#define mux     16      // RX pin MUX ADS1222 1 y 2, selección de la entrada analógica 0->AINP1+ AINN1- 1->AINP2+ AINN2 -D5

#define PULSADOR_wii 15    // Para led, no los uso ahora
#define LED_wii      2    // Para LED, no lo uso ahora
#define ON_wii       1     // Enciende WBB

unsigned long sensor [4] ={0, 0, 0, 0}, sensor_cal[4] ={0, 0, 0, 0};
char filename[]="/12345678_0205.txt";   // DíaMesAño_HoraMin(01012022_0205)
int ch1;  //Variables para corregir sensores
int ch3;

//variables varias para la configuración del programa
int pul; //para guardar el estado del pulsador
int ss1,ss2,ss3,ss4; //sumatoria de cada sensor
int ps1,ps2,ps3,ps4; //promedio de cada sensor
int ns1,ns2,ns3,ns4; //nuevo valor de cada sensor (calibración)


//************Declara vriables para interrupción************************
volatile int FlagInt;  //Indicador de que hubo unterrupción
hw_timer_t * timer = NULL;  //Creamos una variable para renombrar el timer. 
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED; //Variable para sincronizar var entre int y loop
//******************Rutina de interrupción***************************
void IRAM_ATTR onTimer() { 
  portENTER_CRITICAL_ISR(&timerMux);  //Bloquea proceso
  FlagInt=1;   //Activa bandera de interrupción
  portEXIT_CRITICAL_ISR(&timerMux);   //Desbloquea proceso
}


RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};
 
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET    -1  // Reset pin # (or -1 if sharing reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 

// Inicializa ADS1222
void iniADS1222(void){
byte x, n;
  digitalWrite(mux, HIGH);
  for (n =0; n <3; n++){            // realizamos 3 lecturas completas
    delay (10);
    for(x =0; x <24; x++){          // 24 pulsos-> 24 bits
      digitalWrite(sclk, HIGH);     // HIGH pulse, algo más de 33us
      delayMicroseconds(30);  
      digitalWrite(sclk, LOW);      // LOW pulse, algo más de 33us
      delayMicroseconds(30);                          
    }
  }
  delay (10);
  while (digitalRead(Dout1) + digitalRead(Dout2)){}   // esperamos datos preparados 
  for(x =0; x <26; x++){                              // auto-calibrado 26 pulsos-> 24bits + 2  On calibración
    digitalWrite(sclk, HIGH); 
    delayMicroseconds(5);  
    digitalWrite(sclk, LOW); 
    delayMicroseconds(5);
   }
   #ifdef debug
    Serial.println("Auto-calibrado..");
   #endif
   while (digitalRead(Dout1) + digitalRead(Dout2)){}      // esperamos fin calibracion                           
   #ifdef debug
    Serial.println("Inicializando");
   #endif
} 

// ADS1222 leemos (se leen los 20 bits MSB, solo son efectivos los 20 bits de más peso)
void read_ads1222(bool canal){
byte x, n =0, z =2;
  if (canal){
    n =1;    z =3;  }
  digitalWrite(mux, canal);                      // selecionamos el canal
  delayMicroseconds(8);  //tenia 8
  do{
    delayMicroseconds(2); // tenía 2
  } while (digitalRead(Dout1) + digitalRead(Dout2));   // esperamos datos listos, Dout1 y Dout2 
  
  for(x =23; x >=4; x--){                   // del bit 23 al 3
    digitalWrite(sclk, HIGH);
    digitalRead(Dout1) ? bitWrite(sensor[n], x, 1): bitWrite(sensor[n], x, 0);// algo más de 16us, leemos 0 y 1 
    digitalRead(Dout2) ? bitWrite(sensor[z], x, 1): bitWrite(sensor[z], x, 0);// algo más de 16us, leemos 2 y 3
    digitalWrite(sclk, LOW);
    delayMicroseconds(30);  //tenía 30
  }
  for (x =0; x <5; x++){                  // realizamos 5 pulsos, bits del 3 al 0 + pulso 25 -> forzamos Dout1 y Dout2 a 1
  digitalWrite(sclk, HIGH);
  delayMicroseconds(30);  //tenía 30                                                 
  digitalWrite(sclk, LOW);
  delayMicroseconds(30);    //tenía 30
  }
}

void setup() 
{

 //******************Inicia Comunicación serial*****************
Serial.begin(115200);
   if (!SD.begin(5)) {   //Inicializa SD
    Serial.println("initialization failed!");
  while (1);
  }
//*********************Inicia OLED****************************
if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) 
{   Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever   
}
 display.display();
 delay(2);
 display.clearDisplay();

display.clearDisplay();
display.setTextColor(WHITE);
//display.startscrollright(0x00, 0x0F);
display.setTextSize(2);
display.setCursor(0,0);
display.print("WBB system");
display.display();
delay(3000);
display.clearDisplay();  //Limpia pantalla
//*****************************************************
//***********************Inicia RTC****************************+
if (! rtc.begin()) {
Serial.println("Couldn't find RTC");
while (1);
}
//**************Inicia Timer para interrupción**************
  timer = timerBegin(0, 80, true); //Inicializa timer 0, preescaler 80, conteo incremental (1us/incremento)
  timerAttachInterrupt(timer, &onTimer, true); //Usando timer0 nombramos rutina "onTimer" que se activará en edge
  timerAlarmWrite(timer, 10000, true); //Interrumpe cada 10mS (100Hz) se interrumpirá, y recarga el contador
  timerAlarmEnable(timer); //Habilitamos el timer para iniciar las interrupciones
//**********************************************************
//rtc.adjust(DateTime(__DATE__, __TIME__));  //Solo se usa la primera vez para configurar el RTC con el time de la PC


byte ciclo =0;
  pinMode(ON_wii, OUTPUT);                           
  digitalWrite(ON_wii, HIGH);          // encendemos controladores WII y TFT                                          
  pinMode(mux, OUTPUT);                           
  digitalWrite(mux, LOW);  
  pinMode(sclk, OUTPUT);                           
  digitalWrite(sclk, LOW); 
  pinMode(LED_wii, OUTPUT);
  digitalWrite(LED_wii, HIGH);  
  pinMode(PULSADOR_wii, INPUT);   
  pinMode(Dout1, INPUT);
  pinMode(Dout2, INPUT);          
  
//inicia los sensores
  iniADS1222();
  delay(200);



//configuración RTC (seguimos dentro de setup)

DateTime now = rtc.now();   //Obtiene datos para nombrar archivo
sprintf (filename,"/%04d%02d%02d_%02d%02d.txt", now.year(),now.month(),now.day(),now.hour(),now.minute());
  myFile = SD.open(filename, FILE_WRITE);
//  myFile = SD.open("/test.txt", FILE_WRITE);
    display.setCursor(0,40);
    display.print("Grabando");
  read_ads1222(0);
   ch1=sensor[0];   //Variables para reposicionar valor en caso de error de lectura
   ch3=sensor[2];

//interrupción
attachInterrupt(digitalPinToInterrupt(PULSADOR_wii), calibracionRegistro,  RISING);



}




//******************LOOP**********************************************************
int i; 
int j=0;
int t=20; //tiempo seg
void loop()
{

  
 while (j>0 && j<5){
              i=0;
              display.clearDisplay();
              display.setTextColor(WHITE);
              //display.startscrollright(0x00, 0x0F);
              display.setTextSize(1);
              display.setCursor(0,0);
              display.print("calibrando");
              display.display();
              delay(3000);
              display.clearDisplay();
              
                ss1=0;
                ss2=0;
                ss3=0;
                ss4=0;
                
                    //aqui sacamos promedio de 500 muestras para cada sensor
                    if(FlagInt==1 && i>=0){
                      while(i<=500){
                          i=i+1;
                          read_ads1222(0);  //Lee datos de sensor 1 y 3
                          read_ads1222(1);   //Lee datos de sensor 2 y 4
                          Serial.print(sensor[0]);
                          Serial.print(" ");
                          Serial.print(sensor[1]);
                          Serial.print(" ");
                          Serial.print(sensor[2]);
                          Serial.print(" ");            
                          Serial.println(sensor[3]);
              
                          ss1 = ss1 + sensor[0];
                          ss2 = ss2 + sensor[1];
                          ss3 = ss3 + sensor[2];
                          ss4 = ss4 + sensor[3];
                          
              
                          
                          display.clearDisplay();
                          display.setCursor(0,0);
                          display.print("prueba numero");
                          display.setCursor(0,20);
                          display.print(i);
                          display.setCursor(30,20);
                          display.print("de 500");
               
                          
                          display.display();
                          display.clearDisplay();

                          
                    }
                    }
                    //promedio de cada sensor (i es el numero de muestras declarado arriba, 500 en este caso)
                    ps1=ss1/500;
                    ps2=ss2/500;
                    ps3=ss3/500;
                    ps4=ss4/500; //promedio de cada sensor
                    

                    display.setCursor(0,0);
                    display.print("suma terminada");
                    delay(3000);
                    
                    display.display();
                    display.clearDisplay();
                    
                          Serial.print(ps1);
                          Serial.print(" ");
                          Serial.print(ps2);
                          Serial.print(" ");
                          Serial.print(ps3);
                          Serial.print(" ");
                          Serial.println(ps4);
              
              
                    //calibramos cada sensor restando su promedio y dejándolo en cero (o esa es la idea)
                    ns1=sensor[0]-ps1;
                    ns2=sensor[1]-ps2;
                    ns3=sensor[2]-ps3;
                    ns4=sensor[3]-ps4; //nuevo valor de cada sensor (calibración)
                    display.setCursor(0,0);
                    display.print("calibracion");
                    display.setCursor(0,10);
                    display.print("terminada");
                    delay(3000);
                    
                    display.display();
                    display.clearDisplay();
              

                          Serial.print(ns1);
                          Serial.print(" ");
                          Serial.print(ns2);
                          Serial.print(" ");
                          Serial.print(ns3);
                          Serial.print(" ");
                          Serial.println(ns4);

                    display.setCursor(0,0);
                    display.print("p sensor 1 ");
                    display.print(ps1);
                    display.setCursor(0,8);
                    display.print("p sensor 2 ");
                    display.print(ps2);
                    display.setCursor(0,16);
                    display.print("p sensor 3 ");
                    display.print(ps3);
                    display.setCursor(0,24);
                    display.print("p sensor 4 ");
                    display.print(ps4);
                    delay(3000);
                    
                    display.display();
                    display.clearDisplay();

                    display.setCursor(0,0);
                    display.print("n sensor 1 ");
                    display.print(ns1);
                    display.setCursor(0,8);
                    display.print("n sensor 2 ");
                    display.print(ns2);
                    display.setCursor(0,16);
                    display.print("n sensor 3 ");
                    display.print(ns3);
                    display.setCursor(0,24);
                    display.print("n sensor 4 ");
                    display.print(ns4);
                    delay(3000);
                    
                    display.display();
                    display.clearDisplay();
              

//--------------------------------------------------------------------------------------------------------------------------------
//aqui empieza la lectura y guardado de datos
                    
                    while (t>=0){
                    
                    display.setCursor(0,0);
                    display.print("1er prueba con");
                    display.setCursor(0,8);
                    display.print("ojos abiertos");
                    display.setCursor(0,16);
                    display.print("comienza en");
                    display.setCursor(0,24);
                    display.print(t);
                    display.setCursor(0,32);
                    display.print("segundos");
                    
                    delay(1000);
                    t=t-1;
                    }
                    display.display();
                    display.clearDisplay();

                    display.setCursor(0,0);
                    display.print("iniciando 1er prueba");
                    display.setCursor(0,8);
                    display.print("ojos abiertos");
                    delay(3000);
                    display.display();
                    display.clearDisplay();

                    i=0;
                    while(i<=499){
                    
                    display.display();
                    display.clearDisplay();

                    ns1=sensor[0]-ps1;
                    ns2=sensor[1]-ps2;
                    ns3=sensor[2]-ps3;
                    ns4=sensor[3]-ps4; //nuevo valor de cada sensor (calibración)

                    display.setCursor(0,0);
                    display.print("mire adelante");
                    display.setCursor(0,8);
                    display.print("sin movimientos");
                    display.setCursor(0,16);
                    display.print("bruscos");
                    display.setCursor(0,24);
                    display.print("registrando datos...");
                    display.setCursor(0,32);
                    display.print("prueba ");
                    display.setCursor(40,32);
                    display.print(i);
                    display.setCursor(60,32);
                    display.print("de 500");
                    display.display();                    

                                    DateTime now = rtc.now();
                                    display.setCursor(80,40);
                                    display.println(now.second(), DEC);  //Segundos
                                    display.setCursor(25,40);
                                    display.println(":");   
                                    display.setCursor(65,40);
                                    display.println(":");
                                    display.setCursor(40,40); 
                                    display.setTextColor(WHITE, BLACK);  //Borra ultimo valor
                                    display.println(now.minute(), DEC);  //Minutos
                                    display.setCursor(0,40);
                                    display.setTextColor(WHITE, BLACK);  //Borra ultimo valor
                                    display.println(now.hour(), DEC);  //Horas
                                    display.setCursor(0,50);        //Pocición en segunda fila
                                    display.println(now.day(), DEC);  //Día
                                    display.setCursor(25,50);
                                    display.println("/"); 
                                    display.setCursor(40,50);
                                    display.println(now.month(), DEC);  //mes
                                    display.setCursor(55,50);
                                    display.println("/");
                                    display.setCursor(70,50);
                                    display.println(now.year(), DEC);  //año
                                    //display.setCursor(0,40);
                                    //display.print(daysOfTheWeek[now.dayOfTheWeek()]);
                                    display.display(); 
                            
                            i=i+1;
                            read_ads1222(0);  //Lee datos de sensor 1 y 3
                            read_ads1222(1);   //Lee datos de sensor 2 y 4
                            Serial.print(ns1);
                            Serial.print(" ");
                            Serial.print(ns2);
                            Serial.print(" ");
                            Serial.print(ns3);
                            Serial.print(" ");            
                            Serial.println(ns4);
                        if (myFile) {
                      //    Serial.print("Writing to test.txt...");
                            myFile.print(ns1);
                            myFile.print(" ");
                            myFile.print(ns2);
                            myFile.print(" ");
                            myFile.print(ns3);
                            myFile.print(" ");            
                            myFile.println(ns4);
                         // close the file:
                    }
                    
                    }
                    display.display();
                    display.clearDisplay();
                    display.setCursor(0,0);
                    display.print("guardado terminado");
                    display.setCursor(0,8);
                    display.print("revise trajeta sd");
                    delay(3000);
                    display.display();
                    display.clearDisplay();

                    myFile.close();
                    Serial.println("Hecho");
                    j=0;

                    









/*   if(FlagInt==1 && i>=0){
      i=i-1;
      read_ads1222(0);  //Lee datos de sensor 1 y 3

//termina sección de corrección
      read_ads1222(1);   //Lee datos de sensor 2 y 4
      Serial.print(sensor[0]);
      Serial.print(" ");
      Serial.print(sensor[1]);
      Serial.print(" ");
      Serial.print(sensor[2]);
      Serial.print(" ");            
      Serial.println(sensor[3]);
  if (myFile) {
//    Serial.print("Writing to test.txt...");
      myFile.print(sensor[0]);
      myFile.print(" ");
      myFile.print(sensor[1]);
      myFile.print(" ");
      myFile.print(sensor[2]);
      myFile.print(" ");            
      myFile.println(sensor[3]);
   // close the file:
      FlagInt=0;   //Borra bandera
  }
  }   
  if(i==0){
    myFile.close();
    Serial.println("Hecho");
    display.setCursor(0,40);
    display.print("Completado");
//    display.display(); 
  } 
DateTime now = rtc.now();
 
//display.clearDisplay();  //Limpia pantalla
display.setTextSize(2);  //Determina tamaño de texto
display.setCursor(76,0); //Determina pocisión inicial de texto
display.setTextColor(WHITE, BLACK);  //Borra ultimo valor
display.println(now.second(), DEC);  //Segundos
display.setCursor(25,0);
display.println(":");   
display.setCursor(65,0);
display.println(":");
display.setCursor(40,0); 
display.setTextColor(WHITE, BLACK);  //Borra ultimo valor
display.println(now.minute(), DEC);  //Minutos
display.setCursor(0,0);
display.setTextColor(WHITE, BLACK);  //Borra ultimo valor
display.println(now.hour(), DEC);  //Horas
display.setCursor(0,20);        //Pocición en segunda fila
display.println(now.day(), DEC);  //Día
display.setCursor(25,20);
display.println("/"); 
display.setCursor(40,20);
display.println(now.month(), DEC);  //mes
display.setCursor(55,20);
display.println("/");
display.setCursor(70,20);
display.println(now.year(), DEC);  //año
//display.setCursor(0,40);
//display.print(daysOfTheWeek[now.dayOfTheWeek()]);
display.display(); 
*/

}


}


//*********************************interrupcion y programa con el botón***********************************************************
void calibracionRegistro(){

j=j+1;

}
