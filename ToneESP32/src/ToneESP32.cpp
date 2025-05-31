#include "ToneESP32.h"

ToneESP32::ToneESP32(int pin, int channel) {	
  this->pin = pin;
  this->channel = channel;  
  ledcAttachChannel(pin, 2000, 8, channel);
}



void ToneESP32::tone(int note, int duree ) {     
	  ledcWriteTone(pin,note);                        
    delay(duree);
    ledcWrite( pin, 0 );
}


void ToneESP32::noTone() {
    ledcWrite(pin, 0);
}
