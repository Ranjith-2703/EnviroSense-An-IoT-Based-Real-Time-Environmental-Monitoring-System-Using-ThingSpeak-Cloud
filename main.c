#include <LPC21xx.h>
#include <stdio.h>
#include <string.h>
/* LCD driver is maintained separately in this header. */
#include "LCDSTR_HEADER.h"
/*========================================================
                    CONFIGURATION
========================================================*/
#define PCLK_HZ              15000000UL
#define UART_BAUD            9600UL

#define WIFI_SSID            "YOUR_WIFI_SSID"
#define WIFI_PASSWORD        "YOUR_WIFI_PASSWORD"
#define THINGSPEAK_WRITE_KEY "YOUR_WRITE_API_KEY"

#define MQ_R_TOP             10000UL
#define MQ_R_BOTTOM          12000UL

#define MQ_GOOD_MAX_MV       900U
#define MQ_MOD_MAX_MV        1800U

/*========================================================
                    PIN DEFINITIONS
========================================================*/
#define DHT_PIN              (1UL << 4)

/*========================================================
                    GLOBAL VARIABLES
========================================================*/
char esp_rx_buffer[256];

/*========================================================
                    TIMER0
========================================================*/
void timer0_init(void)
{
    T0TCR = 0x02;
    T0PR  = (PCLK_HZ / 1000000UL) - 1UL;
    T0IR  = 0xFF;
    T0TCR = 0x01;
}

void delay_us(unsigned long us)
{
    unsigned long start = T0TC;

    while((unsigned long)(T0TC-start) < us);
}

void delay_ms(unsigned long ms)
{
    while(ms--)
        delay_us(1000UL);
}

/*========================================================
                    GPIO HELPERS
========================================================*/
void gpio_set(unsigned long mask)
{
    IO0SET = mask;
}

void gpio_clear(unsigned long mask)
{
    IO0CLR = mask;
}



/*========================================================
                    UART0
========================================================*/
void uart0_init(void)
{
    unsigned long divisor;

    PINSEL0 &= ~0x0000000FUL;
    PINSEL0 |=  0x00000005UL;

    U0LCR = 0x83;

    divisor = (PCLK_HZ + (8UL*UART_BAUD)) /
              (16UL*UART_BAUD);

    U0DLM = (divisor >> 8) & 0xFF;
    U0DLL = divisor & 0xFF;

    U0FDR = 0x10;

    U0LCR = 0x03;
    U0FCR = 0x07;
    U0IER = 0x00;
}

void uart0_putc(char c)
{
    while((U0LSR & (1UL<<5)) == 0);

    U0THR = (unsigned char)c;
}

void uart0_puts(const char *s)
{
    while(*s)
        uart0_putc(*s++);
}

int uart0_getc_timeout(char *c,
                       unsigned long timeout_ms)
{
    unsigned long start = T0TC;

    while((unsigned long)(T0TC-start)
          < timeout_ms*1000UL)
    {
        if(U0LSR & 0x01)
        {
            *c = (char)U0RBR;
            return 1;
        }
    }

    return 0;
}

void uart0_flush_rx(void)
{
    volatile unsigned long dummy;

    while(U0LSR & 0x01)
        dummy = U0RBR;

    (void)dummy;
}

int uart0_wait_token(const char *token,
                     unsigned long timeout_ms)
{
    unsigned long start = T0TC;
    unsigned int index = 0;
    unsigned int token_len = strlen(token);
    char c;

    if(token_len == 0 ||
       token_len >= sizeof(esp_rx_buffer))
        return 0;

    memset(esp_rx_buffer,0,
           sizeof(esp_rx_buffer));

    while((unsigned long)(T0TC-start)
          < timeout_ms*1000UL)
    {
        if(U0LSR & 0x01)
        {
            c = (char)U0RBR;

            if(index < sizeof(esp_rx_buffer)-1)
            {
                esp_rx_buffer[index++] = c;
                esp_rx_buffer[index] = '\0';
            }
            else
            {
                memmove(esp_rx_buffer,
                        esp_rx_buffer+1,
                        sizeof(esp_rx_buffer)-2);

                esp_rx_buffer[sizeof(esp_rx_buffer)-2] = c;
                esp_rx_buffer[sizeof(esp_rx_buffer)-1] = '\0';
            }

            if(strstr(esp_rx_buffer,token)!=0)
                return 1;

            if(strstr(esp_rx_buffer,"ERROR")!=0)
                return 0;

            if(strstr(esp_rx_buffer,"FAIL")!=0)
                return 0;
        }
    }

    return 0;
}

int esp_command(const char *cmd,
                const char *expected,
                unsigned long timeout_ms)
{
    uart0_flush_rx();

    uart0_puts(cmd);
    uart0_puts("\r\n");

    return uart0_wait_token(expected,
                            timeout_ms);
}

/*========================================================
                    ADC
========================================================*/
void adc_init(void)
{
    /* P0.28 = AD0.1
       P0.29 = AD0.2 */

    PINSEL1 &= ~((3UL<<24)|(3UL<<26));
    PINSEL1 |=  ((1UL<<24)|(1UL<<26));

    ADCR = (3UL<<8)|(1UL<<21);
}

unsigned int adc_read(unsigned char channel)
{
    unsigned long result;
    unsigned long start;

    if(channel != 1 && channel != 2)
        return 0xFFFFU;

    ADCR = (3UL<<8) |
           (1UL<<21) |
           (1UL<<channel);

    ADCR |= (1UL<<24);

    start = T0TC;

    do
    {
        result = ADDR;

        if(result & (1UL<<31))
        {
            if(((result>>24)&7UL)==channel)
            {
                return (unsigned int)
                       ((result>>6)&0x3FFUL);
            }
        }

    }while((unsigned long)(T0TC-start)
           <1000UL);

    ADCR &= ~(7UL<<24);

    return 0xFFFFU;
}

unsigned int adc_to_mv(unsigned int raw)
{
    return (unsigned int)
           (((unsigned long)raw*3300UL)/1023UL);
}

/*========================================================
                    DHT11
========================================================*/
int dht_wait_level(unsigned char level,
                   unsigned int timeout_us)
{
    unsigned long start = T0TC;

    while(((IO0PIN & DHT_PIN) ? 1U:0U)!=level)
    {
        if((unsigned long)(T0TC-start)
           >= timeout_us)
            return 0;
    }

    return 1;
}

int dht11_read(unsigned char *humidity,
               unsigned char *temperature)
{
    unsigned char data[5]={0,0,0,0,0};
    unsigned char i,bit;

    IO0DIR |= DHT_PIN;

    gpio_set(DHT_PIN);
    delay_ms(2);

    gpio_clear(DHT_PIN);
    delay_ms(20);

    gpio_set(DHT_PIN);
    delay_us(30);

    IO0DIR &= ~DHT_PIN;

    if(!dht_wait_level(0,100))
        return 0;

    if(!dht_wait_level(1,100))
        return 0;

    if(!dht_wait_level(0,100))
        return 0;

    for(i=0;i<40;i++)
    {
        if(!dht_wait_level(1,100))
            return 0;

        delay_us(40);

        bit = (IO0PIN & DHT_PIN)
              ? 1U:0U;

        data[i/8] <<= 1;

        if(bit)
            data[i/8] |= 1U;

        if(!dht_wait_level(0,100))
            return 0;
    }

    if((unsigned char)
       (data[0]+data[1]+data[2]+data[3])
       !=data[4])
        return 0;

    *humidity = data[0];
    *temperature = data[2];

    return 1;
}

/*========================================================
                    ESP8266
========================================================*/
int esp_init(void)
{
    char cmd[160];

    delay_ms(1500);

    if(!esp_command("AT","OK",3000))
        return 0;

    if(!esp_command("ATE0","OK",3000))
        return 0;

    if(!esp_command("AT+CWMODE=1","OK",5000))
        return 0;

    sprintf(cmd,
            "AT+CWJAP=\"%s\",\"%s\"",
            WIFI_SSID,
            WIFI_PASSWORD);

    if(!esp_command(cmd,"OK",25000))
        return 0;

    if(!esp_command("AT+CIPMUX=0","OK",5000))
        return 0;

    return 1;
}

/*========================================================
                    THINGSPEAK
========================================================*/
void format_voltage(char *out,
                    unsigned int mv)
{
    sprintf(out,"%u.%03u",
            mv/1000U,
            mv%1000U);
}

int thingspeak_upload(unsigned int analog_mv,
                      unsigned char humidity,
                      unsigned char temperature,
                      unsigned int mq_sensor_mv)
{
    char request[256];
    char analog_text[16];
    char mq_text[16];
    char command[48];

    int request_len;

    format_voltage(analog_text,analog_mv);
    format_voltage(mq_text,mq_sensor_mv);

    sprintf(request,
        "GET /update?api_key=%s&field1=%s&field2=%u&field3=%u&field4=%s HTTP/1.1\r\n"
        "Host: api.thingspeak.com\r\n"
        "Connection: close\r\n\r\n",
        THINGSPEAK_WRITE_KEY,
        analog_text,
        (unsigned int)humidity,
        (unsigned int)temperature,
        mq_text);

    request_len = strlen(request);

    if(request_len <= 0 ||
       request_len > 240)
        return 0;

    if(!esp_command(
       "AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",80",
       "OK",15000))
        return 0;

    sprintf(command,
            "AT+CIPSEND=%d",
            request_len);

    if(!esp_command(command,">",8000))
    {
        esp_command("AT+CIPCLOSE","OK",3000);
        return 0;
    }

    uart0_puts(request);

    if(!uart0_wait_token("200 OK",15000))
    {
        esp_command("AT+CIPCLOSE","OK",3000);
        return 0;
    }

    esp_command("AT+CIPCLOSE","OK",3000);

    return 1;
}

/*========================================================
                    LCD DISPLAY
========================================================*/
void display_readings(unsigned int analog_mv,
                      unsigned int mq_adc_mv,
                      unsigned int mq_sensor_mv,
                      unsigned char humidity,
                      unsigned char temperature,
                      int dht_valid,
                      int upload_ok)
{
    char line[17];
    char v[8];

    lcd_clear();

    format_voltage(v,analog_mv);

    sprintf(line,"V:%s",v);

    lcd_goto(0,0);
    lcd_string(line);

    lcd_goto(0,9);

    if(mq_adc_mv < MQ_GOOD_MAX_MV)
        lcd_string("MQ:GOOD");
    else if(mq_adc_mv < MQ_MOD_MAX_MV)
        lcd_string("MQ:MOD ");
    else
        lcd_string("MQ:POOR");

    lcd_goto(1,0);

    if(dht_valid)
    {
        sprintf(line,"T:%uC H:%u%%",
                (unsigned int)temperature,
                (unsigned int)humidity);

        lcd_string(line);
    }
    else
    {
        lcd_string("DHT11 READ ERROR");
    }

    lcd_goto(1,15);

    if(upload_ok)
        lcd_data('U');
    else
        lcd_data('-');

    (void)mq_sensor_mv;
}

/*========================================================
                    MAIN
========================================================*/
int main(void)
{
    unsigned int analog_raw;
    unsigned int mq_raw;

    unsigned int analog_mv;
    unsigned int mq_adc_mv;
    unsigned int mq_sensor_mv;

    unsigned char humidity=0;
    unsigned char temperature=0;

    int dht_valid;
    int wifi_ok;
    int upload_ok;

    unsigned long scaled;

    /* DHT11 P0.4 GPIO */
    PINSEL0 &= ~(3UL<<8);

    /* LCD P0.16-P0.25 GPIO; ADC P0.28/P0.29 configured in adc_init(). */
    PINSEL1 &= ~0x000FFFFFUL;

    IO0DIR &= ~DHT_PIN;

    timer0_init();
    uart0_init();
    adc_init();
    lcd_init();

    lcd_goto(0,0);
    lcd_string("EnviroSense");

    lcd_goto(1,0);
    lcd_string("Starting...");

    delay_ms(1500);

    wifi_ok = esp_init();

    while(1)
    {
        /* Analog input: AD0.2 */
        analog_raw = adc_read(2);

        /* MQ135 input: AD0.1 */
        mq_raw = adc_read(1);

        if(analog_raw==0xFFFFU)
            analog_raw=0;

        if(mq_raw==0xFFFFU)
            mq_raw=0;

        analog_mv = adc_to_mv(analog_raw);
        mq_adc_mv = adc_to_mv(mq_raw);

        /* Recover voltage before divider */
        scaled = (unsigned long)mq_adc_mv *
                 (MQ_R_TOP+MQ_R_BOTTOM) /
                 MQ_R_BOTTOM;

        if(scaled > 5000UL)
            scaled=5000UL;

        mq_sensor_mv = (unsigned int)scaled;

        /* DHT11 */
        dht_valid = dht11_read(&humidity,
                               &temperature);

        upload_ok=0;

        if(wifi_ok && dht_valid)
        {
            upload_ok = thingspeak_upload(
                analog_mv,
                humidity,
                temperature,
                mq_sensor_mv);

            if(!upload_ok)
                wifi_ok=0;
        }

        /* Display */
        display_readings(
            analog_mv,
            mq_adc_mv,
            mq_sensor_mv,
            humidity,
            temperature,
            dht_valid,
            upload_ok);

        /* Reconnect */
        if(!wifi_ok)
        {
            delay_ms(1000);
            wifi_ok=esp_init();
        }

        /* 20-second interval */
        delay_ms(20000);
    }
}
