/*
* Connect:
* L432KC D13 - ACL2 4 SCLK   hardware defined for the SPI
* L432KC D12 - ACL2 3 MISO  hardware defined for the SPI
* L432KC D11 - ACL2 2 MOSI 
* L432KC D5  - ACL2 1 CS  or any other free
*  GND     - ACL2 5 GND
*  Vcc     - ACL2 6 Vcc
* The ACL2 pins 7 and 8 will be connected if hardware interrupts will be used.
* L432KC D1 - Red LED - 220 Ohm - GND
* L432KC D0 - Green LED - 220 Ohm - GND
*/

#include "ThisThread.h"
#include "mbed.h"
#include "ADXL362.h"
#include <cmath>
#define BUFF_SIZE 6

// ADXL362::ADXL362(PinName CS, PinName MOSI, PinName MISO, PinName SCK) :
ADXL362 ADXL362(D5,D11,D12,D13);

//Threads
    Thread detect_thread;
    Thread blink_thread;
    Thread ok_thread;
    Thread timer_thread;

DigitalOut redLed(D1);
DigitalOut greenLed(D0);

int ADXL362_sitting_detect();
void blink_light();
void ok_light();
void timer();

int8_t y,z;
int sittingDetected = 0;
int i;
int sitBreak;
int blink = 0;
int ok = 0;


int main(){

    ADXL362.reset();
     // we need to wait at least 500ms after ADXL362 reset
    ThisThread::sleep_for(600ms);
    ADXL362.set_mode(ADXL362::MEASUREMENT);
    detect_thread.start(ADXL362_sitting_detect);
    timer_thread.start(timer);
    blink_thread.start(blink_light);
    ok_thread.start(ok_light);
}

void blink_light() {
    while(1) {
        if(blink) {
            redLed.write(1);
            ThisThread::sleep_for(200ms);
            redLed.write(0);
            ThisThread::sleep_for(200ms);
        }
        ThisThread::sleep_for(1s);
    }
}

void ok_light() {
    while(1) {
        if(ok) {
            greenLed.write(1);
            ThisThread::sleep_for(10s);
        }
        else {
            greenLed.write(0);
        }
        ThisThread::sleep_for(1s);
    }
}


int ADXL362_sitting_detect()
{
    int8_t x1,y1,z1,x2,y2,z2;
    int detect = 0;
    while(1){
        x1=ADXL362.scanx_u8();
        y1=ADXL362.scany_u8();
        z1=ADXL362.scanz_u8();
        ThisThread::sleep_for(10ms);
        x2=ADXL362.scanx_u8();
        y2=ADXL362.scany_u8();
        z2=ADXL362.scanz_u8();
            
        y=(y1 + y2)/2;
        z=(z1 + z2)/2;
        if (y>-50 || z>70){
            detect = 1;
            }
        else {
            detect = 0;
        }

        sittingDetected = detect;
        ThisThread::sleep_for(10ms);
        }    
}

void timer() {
    while(1) {
        sitBreak = 0;
        i = 0;
        while(i < 180 && sitBreak < 2) {
            if (sittingDetected == 0) {
                sitBreak++;
            }
            else {
                sitBreak = 0;
            }
            ThisThread::sleep_for(10s);
            i++;
            if (i == 180) {
                while(sittingDetected) {
                    blink = 1;
                    ThisThread::sleep_for(10ms);
                }
                blink = 0;
                ThisThread::sleep_for(180s);
                ok = 1;
                ThisThread::sleep_for(1200ms);
                ok = 0;
            }
        }
    }
}