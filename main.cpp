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
#include "arm_common_tables.h"
#include "arm_const_structs.h"
#include "math_helper.h"
#include <time.h>
#define BUFF_SIZE 6
#define SAMPLES             512                
#define FFT_SIZE            SAMPLES / 2         

// ADXL362::ADXL362(PinName CS, PinName MOSI, PinName MISO, PinName SCK) :
ADXL362 ADXL362(D6,D11,D12,D13);

Ticker  timer;
//Threads
    Thread detect_thread;
    Thread blink_thread;
    Thread ok_thread;
    Thread timer_thread;
    Thread heart_thread;

DigitalOut redLed(D1);
DigitalOut greenLed(D0);
AnalogIn heartPin(A0);

void ADXL362_sitting_detect();
void blink_light();
void ok_light();
void heart_rate();
void sitTimer();

int8_t y,z;
int sittingDetected = 0;
char * position = "";
int i;
int sitBreak;
int blink = 0;
int ok = 0;
float32_t   sensor_data[SAMPLES*2];
float32_t   output_fft[FFT_SIZE];
bool        do_sample = false;
float32_t   maxValue;
int maxIndex;
int heartRate;

void getPeak(){
    for (int k = 10; k < FFT_SIZE-11; k += 1){
        if (output_fft[k] > maxValue){
            maxValue = output_fft[k];
            maxIndex = k;
        }
    }
}

void sample()
{
    do_sample = true;
}


int main(){

    ADXL362.reset();
     // we need to wait at least 500ms after ADXL362 reset
    ThisThread::sleep_for(600000);
    ADXL362.set_mode(ADXL362::MEASUREMENT);
    detect_thread.start(ADXL362_sitting_detect);
    timer_thread.start(sitTimer);
    blink_thread.start(blink_light);
    ok_thread.start(ok_light);
    heart_thread.start(heart_rate);

    ESP8266Interface esp(MBED_CONF_APP_ESP_TX_PIN, MBED_CONF_APP_ESP_RX_PIN);
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

    esp.get_ip_address();
    printf("IP via DHCP: %s\n", deviceIP.get_ip_address());

    esp.gethostbyname(MBED_CONF_APP_MQTT_BROKER_HOSTNAME, &MQTTBroker, NSAPI_IPv4);
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
    msg.payloadlen = 41;

    socket.open(&esp);
    socket.connect(MQTTBroker);

    client.connect(data);
        while(1) {
        client.publish(MBED_CONF_APP_MQTT_TOPIC, msg);
        
        // Sleep time must be less than TCP timeout
        // TODO: check if socket is usable before publishing
        ThisThread::sleep_for(10000000);
        if (heartRate > 99){
            msg.payloadlen = 42;
        }
        else{
            msg.payloadlen = 41;
        }
        sprintf(buffer, "{\"position\": \"%s\", \"heartrate\": %d}", position, heartRate);
    }


}

void heart_rate() {
    while(1) {
        char str[2048] = {0};
        timer.attach(&sample, 0.097); //25us 40KHz sampling rate
        //t.reset();
        //t.start();
        for (int i = 0; i < 2*SAMPLES; i += 2) {
            while (do_sample == false) {}
            do_sample = false;
            //sensor_data[i] = sound_analog.read(); 
            sensor_data[i] = heartPin.read();   //Real part
            sensor_data[i + 1] = 0;                  //Imaginary Part set to zero
            //t.stop();
            //printf("%d \n", t.read_us());
            //t.reset();
            //t.start();
        }
        timer.detach();






        /*------------------------ FFT CALCULATION ---------------------------*/
        // Init the Complex FFT module, intFlag = 0, doBitReverse = 1
        // NB using predefined arm_cfft_sR_f32_lenXXX, in this case XXX is 512
        // Run FFT on sample data.
        arm_cfft_f32(&arm_cfft_sR_f32_len512, sensor_data, 0, 1);

        // Complex Magniture Module put results into Output(Half size of the Input)
        arm_cmplx_mag_f32(sensor_data, output_fft, FFT_SIZE);


        /*---------------------- SERIAL DATA FFT -----------------------------*/
        for (int i = 0; i < (int)(FFT_SIZE); i++) {
            char buf[20];
            if(i == 0) {
                output_fft[i] = 0.0;              /* The first value of FFT set is 0 */
            }
            sprintf (buf, "%.4f", output_fft[i]); /* convert float to string */
            strcat(str, buf);                     /* appended to string */
            if(i < (int)(FFT_SIZE - 1)) {
                strcat(str, ",");
            }
        }
        //myserial.printf("%s\n", str);
        printf("%s\n", str);

        getPeak();

        heartRate = maxIndex * 1.2;
        maxValue = 0;
    } 
}


void blink_light() {
    while(1) {
        if(blink) {
            redLed.write(1);
            ThisThread::sleep_for(200000);
            redLed.write(0);
            ThisThread::sleep_for(150000);
        }
        ThisThread::sleep_for(1000000);
    }
}

void ok_light() {
    while(1) {
        if(ok) {
            greenLed.write(1);
            ThisThread::sleep_for(10000000);
        }
        else {
            greenLed.write(0);
        }
        ThisThread::sleep_for(1000000);
    }
}


void ADXL362_sitting_detect()
{
    int8_t x1,y1,z1,x2,y2,z2;
    int detect = 0;
    while(1){
        x1=ADXL362.scanx_u8();
        y1=ADXL362.scany_u8();
        z1=ADXL362.scanz_u8();
        ThisThread::sleep_for(10000);
        x2=ADXL362.scanx_u8();
        y2=ADXL362.scany_u8();
        z2=ADXL362.scanz_u8();
            
        y=(y1 + y2)/2;
        z=(z1 + z2)/2;
        if (y>-50 || z>70){
            detect = 1;
            position = "Sitting ";
            }
        else {
            detect = 0;
            position = "Standing";
        }

        sittingDetected = detect;
        ThisThread::sleep_for(10000);
        }    
}

void sitTimer() {
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
            ThisThread::sleep_for(10000000);
            i++;
            if (i == 180) {
                blink = 1;
                while(sittingDetected) {
                    ThisThread::sleep_for(1000000);
                }
                ThisThread::sleep_for(1000000);
                blink = 0;
                for (int i = 0; i < 18; i++){
                    if (heartRate < 100){
                        i = -1;
                    }
                    ThisThread::sleep_for(10000000);
                }
                ok = 1;
                while(sittingDetected == 0){
                    ThisThread::sleep_for(1000000);
                }
                ok = 0;
            }
        }
    }
}