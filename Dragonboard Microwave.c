//Name: Yaroslav Khalitov
//File: Microwave Final Project
//Class: CSC 202
//Description: This is the C code for the Dragonboard that simulates
// a microwave with internal and external components.

#include <hidef.h>      /* common defines and macros */
#include <mc9s12dg256.h>     /* derivative information */
#include <string.h>
#pragma LINK_INFO DERIVATIVE "mc9s12dg256b"

#include "main_asm.h" /* interface to the assembly module */
#include "queue.h"

//operation variables
int running = 1;
int soundOff = 1;
int abortedReset = 0;
int i = 0;

int doneDoOnce = 0;
int ultrasonicDoOnce = 0;
int previousLight = 0;
int light = 0;
int previousWarningLight = 1;
int warningLight = 1;


//1 - Waits for time setting
//2 - Cooking
//3 - Done
//4 - Abort
int cookingStage = 1; 

//information variables
int brightness;
int potValue;
int previousPotValue;
int pitchValue = 2276;

//cooking time
int first;
int last;

//ultrasonic
long pulsecycles;
long startcycle;
long distance_in_mm;

//MiniIDE display messages
char* inputDis = "**Waiting For Input**\n";
char* cookingDis = "Cooking...\n";
char* doneDis = "**Finished Cooking**\n";
char* abortDis = "**Cooking Aborted**\n";
char* servoLatchDis = "**Door Latched**\n";
char* servoUnlatchDis = "**Door Unlatched**\n";
char* lightDis = "**Adjusting Light**\n";
char* distanceDis = "**Too Close Warning**\n";
char* powerChangedDis = "**Power Changed**\n";



//-------------------INTERRUPTS-------------------

//SW5 interrupt
void interrupt 25 abortHandler(){
   //reset stage
   cookingStage = 4;
   
   //reset variables/instances
   sound_off();
   soundOff = 1;
   doneDoOnce = 0;
   ultrasonicDoOnce = 0;
   
   //display message to LCD 
   clear_lcd();
   set_lcd_addr(0x02);
   type_lcd("**ABORTED**");
   set_lcd_addr(0x42);
   type_lcd("Returning...");
   
   
   //clear the flag
   PIFH = 0xFF;
}


//sound interrupt
void interrupt 13 soundHandler(){
  tone(pitchValue);  
}


//ultrasonic distance measure interrupt
void interrupt 9 handler() {
  if(PTT | 0xFD == 0xFF) {
    startcycle = TCNT;        //rising edge interrupt
  } 
  else {
    pulsecycles = TC1 - startcycle;     //falling edge interrupt
    TSCR1 = 0x00;         //stop counter
  }
  
  TFLG1 = TFLG1 | 0x02;     //reset interrupt flag 
} 


//-------------------MAIN-------------------
void main(void) {
  //initializations
  PLL_init();
  ad0_enable();
  lcd_init();
  keypad_enable();
  sound_init();
  servo76_init();
  seg7_disable();
  SCI0_init(9600);
 
    
  //enable interrupts                 
  _asm CLI
  PIEH = 0xFF;      //port H interrupts        
  
  //Port Outputs
  //external blue LED
  DDRE = 0xFF;  
  PORTE = 0x00; 
  
  //external red LED
  DDRM = 0xFF;
  PTM = 0x00;
     
  //internal LED 
  DDRJ = 0xFF;
  
  //motor & internal LED
  DDRB = 0xFF;
  
  //motor & 7 segment display
  DDRP = 0xFF;
  
    
  
    
  
  //-------------------MAIN LOOP-------------------
  while (running){
    //clear the lcd
    clear_lcd();
    
    //set registers for ultrasonic sensor if need be
    if (ultrasonicDoOnce == 0){
      TIOS = 0x01;       // set channel 0 OC and channel 1 IC
      TIE  = 0x02;       // enable interrupts on channel 1
      TCTL2 = 0x02;      // clear PT0 when TCNT == TC0
      TCTL4 = 0x0C;      // interrupt set to happen on rise or fall of channel 1
      TSCR2 = 0x02;     // set TIMER clock at 6MHz    
      ultrasonicDoOnce = 1;  
    }
    
    //get measurement from ultrasonic sensor
    TSCR1 = 0x80;      //start counter  
    PTT = PTT | 0x01;  // make trigger line go high
    TC0 = TCNT + 60;    // pulse trig for 10 microseconds
    distance_in_mm = ((pulsecycles*17)/600); //calculate distance
    
    //turn on red light if your too close
    if (distance_in_mm > 20 && distance_in_mm <2000) {        
      if (distance_in_mm < 200){
        PTM = PTM | 0x04; //turns on
        warningLight = 0;        
      }else{
        PTM = PTM & 0xFB; //turns off
        warningLight = 1; 
      }  
    }
    
    //output status to SCI
    if (previousWarningLight == 1 && warningLight == 0){
      while (i != strlen(distanceDis)){
        outchar0(distanceDis[i]);
        i++; 
      }
      i = 0;    
    }
    previousWarningLight = warningLight;
    
    
    //get brightness & turn on/off LED
    brightness = ad0conv(4);   
    if (brightness >= 30){
      PORTE = PORTE & 0x7F; //turns off     
      light = 0;
    }else{
      PORTE = PORTE | 0x80; //turns on
      light = 1;
        
    }
    
    //output status to SCI
    if (previousLight != light){
      while (i != strlen(lightDis)){
        outchar0(lightDis[i]);
        i++; 
      }
      i = 0;    
    }
    previousLight = light;
    
    //-------------WAITING FOR INPUT-------------
    if (cookingStage == 1){
      //display prompt to LCD
      set_lcd_addr(0x01);
      type_lcd("**Enter Time**");
      set_lcd_addr(0x40);
      type_lcd("Time: ");  
      set_lcd_addr(0x49);
      type_lcd("sec");
      
      //output status to SCI
      while (i != strlen(inputDis)){
        outchar0(inputDis[i]);
        i++; 
      }
      i = 0;
      
      //get tenths place time and display
      first = getkey();
      set_lcd_addr(0x46);
      hex2lcd(first);
      wait_keyup();
            
      //get first place time and display
      last = getkey();
      set_lcd_addr(0x47);
      hex2lcd(last);
      wait_keyup();
      
      //preset previous pot value to current
      previousPotValue = ad0conv(7);
      previousPotValue = previousPotValue + 100;
      previousPotValue = previousPotValue / 100;
      if (previousPotValue == 11){
        previousPotValue = 10;  
      }
         
      //move on to stage 2
      cookingStage = 2;
      
      //output status to SCI 
      while (i != strlen(servoLatchDis)){
        outchar0(servoLatchDis[i]);
        i++; 
      }
      i = 0; 
    
    //-------------COOKING-------------           
    }else if (cookingStage == 2){
      //display remaining time to LCD
      type_lcd("Cooking: ");
      set_lcd_addr(0x09);
      hex2lcd(first);
      hex2lcd(last);
      set_lcd_addr(0x0C);
      type_lcd("sec");
      last--;
      
      //output status to SCI
      while (i != strlen(cookingDis)){
        outchar0(cookingDis[i]);
        i++; 
      }
      i = 0;
           
      //turn on food rotation & lock/disable LED's
      PTJ = 0x02; //sets bit#1 high disabling LED
      PORTB = 0x01; //makes motor spin CW
      set_servo76(5500);
      
      //showcase power level
      set_lcd_addr(0x40);
      type_lcd("Power: ");
      
      potValue = ad0conv(7);
      potValue = potValue + 100;
      potValue = potValue / 100;
      if (potValue == 11){
        potValue = 10;  
      }
      
      //output status to SCI & LCD 
      if (previousPotValue != potValue){
        while (i != strlen(powerChangedDis)){
          outchar0(powerChangedDis[i]);
          i++; 
        }
        i = 0;  
      }
      previousPotValue = potValue;
      set_lcd_addr(0x47);
      write_int_lcd(potValue);
      
      //count down
      if (last == -1){
        first--;
        last = 9;  
      }
      
      //count down reaches 0/move on to stage 3
      if (first == -1){
        cookingStage = 3;
        PORTB = 0x00;
      }
      
    //-------------DONE-------------  
    }else if(cookingStage == 3){
      //display done message to LCD
      set_lcd_addr(0x03);
      type_lcd("**ENJOY**");
      set_lcd_addr(0x40);
      type_lcd("Cooking Complete");
      
      
      //turn off spinning plate & lock/ enable LED's
      if (doneDoOnce == 0){
        PTJ = 0x00; //sets bit#1 low enabling LED
        PORTB = 0x00; 
        set_servo76(3300);
        
        //output status to SCI
        while (i != strlen(doneDis)){
          outchar0(doneDis[i]);
          i++; 
        }
        i = 0;
        
        while (i != strlen(servoUnlatchDis)){
          outchar0(servoUnlatchDis[i]);
          i++; 
        }
        i = 0;  
             
        doneDoOnce = 1;
      }
    
    
      //toggle LED's & sound
      sound_init();
      PORTB = ~PORTB;
      if (soundOff){
        sound_on();
        soundOff = 0;  
      }else{
        sound_off();
        soundOff = 1;;  
      } 

    //-------------ABORTED-------------     
    }else if (cookingStage == 4){
      //displays stopped text to LCD
      set_lcd_addr(0x02);
      type_lcd("**ABORTED**");
      set_lcd_addr(0x42);
      type_lcd("Returning...");
      
      //turn off spinning plate & lock/ enable LED's
      PTJ = 0x00; //sets bit#1 low enabling LED
      PORTB = 0x00; 
      set_servo76(3300);
      
      //output status to SCI
      if (abortedReset == 0){
        while (i != strlen(abortDis)){
          outchar0(abortDis[i]);
          i++; 
        }
        i = 0;
      
        while (i != strlen(servoUnlatchDis)){
          outchar0(servoUnlatchDis[i]);
          i++; 
        }
        i = 0;  
        
      }
      
      //reset if 2 seconds passed
      abortedReset++;     
      if (abortedReset == 2){
        cookingStage = 1;
        abortedReset = 0;        
      }      
      
    }//cooking stages close
    
    
    
    
    ms_delay(1000); //wait one second  
  }//main while loop close


  for(;;) {} /* wait forever */
}
