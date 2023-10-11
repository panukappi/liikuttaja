/*
* Connect:
* L432KC D13 - ACL2 4 SCLK   hardware defined for the SPI
* L432KC D12 - ACL2 3 MISO  hardware defined for the SPI
* L432KC D11 - ACL2 2 MOSI 
* L432KC D6  - ACL2 1 CS  or any other free
*  GND     - ACL2 5 GND - MOD WIFI ESP8266 Pin 2 - DFROBOT SEN0203 Pin 1
*  Vcc     - ACL2 6 Vcc - MOD WIFI ESP8266 Pin 1 - DFROBOT SEN0203 Pin 2
* L432KC D1 - Red LED - 220 Ohm - GND
* L432KC D0 - Green LED - 220 Ohm - GND
* L432KC D4 - MOD WIFI ESP8266 Pin 4
* L432KC D5 - MOD WIFI ESP8266 Pin 3
* L432KC A0 - DFROBOT SEN0203 Pin 3
*/

#include "ThisThread.h"
#include "mbed.h"
#include "ADXL362.h"
#include <cmath>
#include "ESP8266Interface.h"
#include <MQTTClientMbedOs.h>
#include "ntp-client/NTPClient.h"
#define BUFF_SIZE 6
#define ntpAddress "time.mikes.fi"  // The VTT Mikes in Helsinki
#define ntpPort 123

// ADXL362::ADXL362(PinName CS, PinName MOSI, PinName MISO, PinName SCK) :
ADXL362 ADXL362(D6,D11,D12,D13);

//Threads
    Thread detect_thread;
    Thread blink_thread;
    Thread ok_thread;
    Thread timer_thread;
    Thread ticks_thread;
    Thread heart_thread;
    Thread ntp_thread;

DigitalOut redLed(D1);
DigitalOut greenLed(D0);
AnalogIn heartPin(A0);

int ADXL362_sitting_detect();
void blink_light();
void ok_light();
void heartrateTimer();
void heart_rate();
void timer();
void ntpTime();

int8_t y,z;
int sittingDetected = 0;
char * position = "";
int i;
int sitBreak;
int blink = 0;
int ok = 0;
int heartRate;
int ticks = 0;
int rollover = 0;
time_t timestamp;
    
ESP8266Interface esp(MBED_CONF_APP_ESP_TX_PIN, MBED_CONF_APP_ESP_RX_PIN);

int main(){

    ADXL362.reset();
     // we need to wait at least 500ms after ADXL362 reset
    ThisThread::sleep_for(600ms);
    ADXL362.set_mode(ADXL362::MEASUREMENT);
    detect_thread.start(ADXL362_sitting_detect);
    timer_thread.start(timer);
    blink_thread.start(blink_light);
    ok_thread.start(ok_light);
    ticks_thread.start(heartrateTimer);
    heart_thread.start(heart_rate);
    ntp_thread.start(ntpTime);


    SocketAddress deviceIP;
    //Store broker IP
    SocketAddress MQTTBroker;
    TCPSocket socket;

    printf("\nConnecting wifi..\n");

    MQTTClient client(&socket);
    int ret = esp.connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);

    if(ret != 0)
    {
        printf("\nConnection error\n");
    }
    else
    {
        printf("\nConnection success\n");
    }

    esp.get_ip_address(&deviceIP);
    printf("IP via DHCP: %s\n", deviceIP.get_ip_address());

    esp.gethostbyname(MBED_CONF_APP_MQTT_BROKER_HOSTNAME, &MQTTBroker, NSAPI_IPv4, "esp");
    MQTTBroker.set_port(MBED_CONF_APP_MQTT_BROKER_PORT);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.MQTTVersion = 3;
    char *id = MBED_CONF_APP_MQTT_ID;
    data.clientID.cstring = id;

    char buffer[64];
    
    MQTT::Message msg;
    msg.qos = MQTT::QOS0;
    msg.retained = false;
    msg.dup = false;
    msg.payload = (void*)buffer;
    msg.payloadlen = 40;

    socket.open(&esp);
    socket.connect(MQTTBroker);

    client.connect(data);
        while(1) {
        
        // Sleep time must be less than TCP timeout
        // TODO: check if socket is usable before publishing
        ThisThread::sleep_for(10s);
        msg.payloadlen = 47;
        if (heartRate > 99){
            msg.payloadlen++;
        }
        if (sittingDetected){
            msg.payloadlen--;
        }
        if (timestamp > 9){
            msg.payloadlen++;
            if (timestamp > 99){
                msg.payloadlen++;
                if (timestamp > 999){
                    msg.payloadlen++;
                    if (timestamp > 9999){
                        msg.payloadlen++;
                        if (timestamp > 99999){
                            msg.payloadlen++;
                            if (timestamp > 999999){
                                msg.payloadlen++;
                                if (timestamp > 9999999){
                                    msg.payloadlen++;
                                    if (timestamp > 99999999){
                                        msg.payloadlen++;
                                        if (timestamp > 999999999){
                                            msg.payloadlen++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
       
        sprintf(buffer, "{\"time\":%i,\"position\":\"%s\",\"heartrate\":%d}", timestamp, position, heartRate);
        client.publish(MBED_CONF_APP_MQTT_TOPIC, msg);
    }


}

void heart_rate() {
    int peak = 0;
    int peaks[6] = {0,0,0,0,0,0};
    int times[5] = {0,0,0,0,0};
    int rates[5] = {0,0,0,0,0};
    int ratesAvg;
    int rateNo = 0;
    int peakNo = 0;
    int rate;
    int readings[8] = {0,0,0,0,0,0,0,0};

    while (true) {
        int heartReading = heartPin.read_u16() >>6;
        //printf("%d\n", heartRate);
        for (int i = 0; i < 7; i++){
            readings[i] = readings[i+1];
        }
        readings[7] = heartReading;

        for (int i = 0; i < 3; i++){
            if (readings[i] < readings[3]){
                peak++;
            }
        }
        for (int i = 4; i < 7; i++){
            if (readings[i] < readings[3]){
                peak++;
            }
        }
        if (peak == 5){
            peaks[peakNo] = ticks + 9999 * rollover;
            ThisThread::sleep_for(50ms);
            peakNo++;
            if (peakNo == 6){
                peakNo = 0;
                for (int i = 0; i < 5; i++){
                    for (int j = 0; j < 6; j++){
                        if (peaks[i+1] - peaks[i] > times[j]){
                            for (int k = j; k < 4; k++){
                                times[k+1] = times[k];
                            }
                            times[j] = peaks[i+1] - peaks[i];
                        }
                    }
                }
                rate = 600 / times[3];

                for (int i = 0; i < 6; i++){
                    peaks[i] = 0;
                }
                for (int i = 0; i < 5; i++){
                    times[i] = 0;
                }
                if (rate > 30 && rate < 190){
                    rates[rateNo] = rate;
                    rateNo++;
                }

                if (rateNo == 5){
                    rateNo = 0;
                    for (int i = 0; i < 5; i++){
                        ratesAvg += rates[i];
                    }
                    ratesAvg = ratesAvg / 5;
                    heartRate = ratesAvg;
                    printf("\nHeart rate: %d\n", ratesAvg);
                    //heartRate = ratesAvg;
                }
            }
        }
        peak = 0;
        ThisThread::sleep_for(50ms);
    }
}

void heartrateTimer() {
    while(1){
    ticks++;
    if (ticks > 9999){
        ticks = 0;
        rollover++;
    }
    ThisThread::sleep_for(100ms);
}
}

void blink_light() {
    while(1) {
        if(blink) {
            redLed.write(1);
            ThisThread::sleep_for(200ms);
            redLed.write(0);
            ThisThread::sleep_for(150ms);
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

void ntpTime(){
    NTPClient ntp(&esp);
    ntp.set_server(ntpAddress, ntpPort);
    while(1){
        timestamp = ntp.get_timestamp();
        ThisThread::sleep_for(10s);
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
            position = "Sitting";
            }
        else {
            detect = 0;
            position = "Standing";
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
                blink = 1;
                while(sittingDetected) {
                    ThisThread::sleep_for(1s);
                }
                ThisThread::sleep_for(1s);
                blink = 0;
                for (int i = 0; i < 18; i++){
                    if (heartRate < 100){
                        i = -1;
                    }
                    ThisThread::sleep_for(10s);
                }
                ok = 1;
                while(sittingDetected == 0){
                    ThisThread::sleep_for(1s);
                }
                ok = 0;
            }
        }
    }
}