#ifndef LCDSTR_HEADER_H
#define LCDSTR_HEADER_H

#include <LPC21xx.h>

/*
 * LPC2129 16x2 LCD, 8-bit interface
 *
 * LCD D0-D7 -> P0.16-P0.23
 * LCD RS    -> P0.24
 * LCD EN    -> P0.25
 *
 * P0.0/P0.1 remain available for UART0 (ESP8266).
 * This header uses delay_ms() from the main application.
 */
#define LCD_D       (0xFFUL << 16)
#define RS          (1UL << 24)
#define E           (1UL << 25)
#define LCD_MASK    (LCD_D | RS | E)

void delay_ms(unsigned long ms);

void LCD_INIT(void);
void LCD_COMMAND(unsigned char cmd);
void LCD_DATA(unsigned char data);
void LCD_STR(const char *s);

void LCD_INIT(void)
{
    /* P0.16-P0.25 as GPIO; do not alter PINSEL0/UART0 pins. */
    PINSEL1 &= ~(0x000FFFFFUL);
    IO0DIR |= LCD_MASK;

    IO0CLR = LCD_MASK;
    delay_ms(20);

    LCD_COMMAND(0x38); /* 8-bit, 2 lines, 5x8 font */
    LCD_COMMAND(0x0C); /* Display ON, cursor OFF */
    LCD_COMMAND(0x06); /* Increment cursor */
    LCD_COMMAND(0x01); /* Clear display */
    delay_ms(2);
}

void LCD_COMMAND(unsigned char cmd)
{
    IO0CLR = LCD_D | RS;
    IO0SET = (((unsigned long)cmd << 16) & LCD_D);

    IO0SET = E;
    delay_ms(1);
    IO0CLR = E;
    delay_ms(1);
}

void LCD_DATA(unsigned char data)
{
    IO0CLR = LCD_D;
    IO0SET = (((unsigned long)data << 16) & LCD_D);
    IO0SET = RS;

    IO0SET = E;
    delay_ms(1);
    IO0CLR = E;
    delay_ms(1);
}

void LCD_STR(const char *s)
{
    while(*s)
        LCD_DATA((unsigned char)*s++);
}

/* Compatibility functions used by the existing main program. */
void lcd_init(void)
{
    LCD_INIT();
}

void lcd_cmd(unsigned char cmd)
{
    LCD_COMMAND(cmd);
}

void lcd_data(unsigned char data)
{
    LCD_DATA(data);
}

void lcd_string(const char *s)
{
    LCD_STR(s);
}

void lcd_goto(unsigned char row, unsigned char col)
{
    unsigned char address = (row == 0) ? 0x00 : 0x40;
    LCD_COMMAND((unsigned char)(0x80 | (address + col)));
}

void lcd_clear(void)
{
    LCD_COMMAND(0x01);
    delay_ms(2);
}

#endif
