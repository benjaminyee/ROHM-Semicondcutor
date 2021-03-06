/*
 *  ROHM SemiConductor USB-C Project
 *  Author: Benjamin Yee
 *  BD99954_Funcs.c -> All the BD99954 register setting functions
 *  BM92A_Funcs.c -> Source mode and sink mode register setting functions
 *  debugFunctions.c -> Primary UART debug functions that print out registers and their settings
 *                      to the UART console
 *  delay.c -> Frequency set and delay functions
 *  globals.c -> Flag sets and menu navigation registers
 *  helper.c -> Functions that are helpful for bit reordering. Sinking and source monitoring
 *              functions are placed in here
 *  I2C_helper.c -> Functions for I2C including read and write functions for 2 Byte, 4 Byte
 *                  and multibyte code.
 *  interruptPins.c -> enables LED and interrupt pins for LCD and system shutdown.
 *                      Readback of DIP switch state
 *  lcd.c -> LCD functions that format the display and display negotiation, \
 *              voltage and current monitoring
 *  menu.c -> Menu navigation interface for the LCD. Calls various register setting
 *            for the BM92A and BD99954
 *  UART.c -> Functions for terminal use on the computer COM4 19200
 *
 *  Pin outs:
 *  P1.6 -> SDA;    P1.7->SCL
 *  P3.0 -> GPIO0(#Alert) BM92A
 *  P1.0 -> Interrupt BD99954
 *  P5.1-5.7 -> D0-D7 LCD
 *  P7.0 -> RS;     P7.1 -> RW;     P7.2->EN (LCD)
 *  Joystick Pinouts refer to interruptPins.c
 *
 *  PDOcreator.py is a quick and nice python tool for creating and decoding PDOs
 *  and auto- negotiate PDO settings
 */
#include "I2C_Helper.h"
#include "msp432.h"
#include <time.h>
#include <stdint.h>
#include "UART.h"
#include "helper.h"
#include "interruptPins.h"
#include <stdlib.h>
#include "globals.h"
#include "lcd.h"
#include "BD99954_Funcs.h"
#include "BM92A_Funcs.h"
#include "menu.h"
#include "delay.h"
#include "debugFunctions.h"
#include "GPIO.h"

#define BD99954_ADDRESS 0x09
#define BM92A_ADDRESS 0x18
#define CURRENT_FREQ FREQ_3_MHZ

int main(void) {
    __disable_irq();
    set_DCO(CURRENT_FREQ);
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;       // Stop watchdog timer
    gpio_init();    //Enables GPIO Pins such as LEDs
    InitI2C();       //SDA -> P1.6 SCL->P1.7
    LCD_init();
    LCD_command(0x01); // clear screen, move cursor home
    terminal_init();
    interruptPinInit();
    __enable_irq();    // Enable global interrupt
    unsigned short alertRead, BD_rail;
    UARTEnable();
    terminal_transmitWord("Initializing Registers\n\r");
    chargeState();  //Reads DIP Switch and Charge enable or disable
    srcAllPDOMode();    //Writes all the PDO settings for the BM92A
    sinkAllPDOMode();
    if(((readTwoByte(0x03,BM92A_ADDRESS)&0x0300)>>8)!=0){   //Catch statement for no battery attached
        // Occurs before the snk reg set because 0x0909 sent to 0x05 on the BM92A causes the source to disconnect 5V
        // turning off the BM92A
        BD99954_Startup_Routine();
        readTwoByte(0x02,BM92A_ADDRESS);
        monitorSnkVoltage();
        write_word(0x71,BD99954_ADDRESS,0x000F);
    }
    BM92Asnk_regSet();  //Sets the registers for sink mode initially
    BM92Asnk_commandSet();
    terminal_transmitWord("BD9954 Refreshed Registers\n\r");
    cursorFlag = TRUE;
    terminal_transmitWord("Successful Initialization\n\r");
    BD99954_Startup_Routine();  //Map sets and battery threshold sets
    readTwoByte(0x02,BM92A_ADDRESS);
    clear_BD_int();
    while(1) {

        if(cursorFlag == TRUE) {    //Triggered by joystick action
            if(rightFlag == TRUE)menuScroll(1);
            if(leftFlag == TRUE) menuScroll(2);
            displayMode();          //refreshes the menu with new joystick presses
            cursorFlag = FALSE;
        }

        if(AlertFlag) {
            alertRead = readTwoByte(0x02,BM92A_ADDRESS);
            BD_rail = readTwoByte(0x70,BD99954_ADDRESS);
            write_word(0x70,BD99954_ADDRESS,0x00FF);
            if(mode_set ==0) {  //Checks to see if in Sink or Source mode
                if(((readTwoByte(0x03,BM92A_ADDRESS)&0x0300)>>8)!=0){
                    monitorSnkVoltage();
                    write_word(0x71,BD99954_ADDRESS,0x000F);    //All this occurs after exiting of the while loop
                    LCD_wake();
                    displayMode();
                    readTwoByte(0x02,BM92A_ADDRESS);
                }
                if((BD_rail&0x0004)>>2){
                    monitorVCCSnkVoltage();
                    write_word(0x72,BD99954_ADDRESS,0x000F);//All this occurs after exiting of the while loop
                    LCD_wake();
                    displayMode();
                    readTwoByte(0x02,BM92A_ADDRESS);

                }
                cursorFlag = FALSE;select = 0;
                BD99954_INT = FALSE;
                AlertFlag = FALSE;

            }
            else if(mode_set == 1) {
                if((BD_rail&0x0004)>>2){
                    reverseDisable();    //Ensures source mode is not active in sink mode
                    clear_BD_int();
                    BM92Asnk_regSet();   BM92Asnk_commandSet(); // Register writes the sink mode
                    monitorVCCSnkVoltage();
                    write_word(0x72,BD99954_ADDRESS,0x000F);//All this occurs after exiting of the while loop
                    LCD_wake();
                    delay_ms(100,CURRENT_FREQ);
                    if(((readTwoByte(0x03,BM92A_ADDRESS)&0x0300)>>8)!=0){//Occurs if the USB_C is plugged in while VCC was plugged in
                        readTwoByte(0x02,BM92A_ADDRESS);
                        monitorSnkVoltage();
                        write_word(0x71,BD99954_ADDRESS,0x000F);    //All this occurs after exiting of the while loop
                        LCD_wake();
                        readTwoByte(0x02,BM92A_ADDRESS);
                    }
                    settings_menu = 1;fast_set = 1; //Reset flag checks
                    AlertFlag = FALSE; select = 0;
                    cursorFlag = FALSE;
                    BD99954_INT = FALSE;
                    delay_ms(500,CURRENT_FREQ);
                    readTwoByte(0x02,BM92A_ADDRESS);
                    displayMode();

                }
                if((alertRead &0x2000)>>13) {   //Source mode plug in event. Checks to see if Alert flag has plug insert event
                    sourceNegotiate();  // Determines output voltage of negotiation
                    AlertFlag = FALSE;
                    if(((readTwoByte(0x03,BM92A_ADDRESS)&0x0300)>>8)!=0) monitorSrcVoltage();   //While loop that monitors ACP node
                    reverseVoltage(5024);//All this occurs after exiting of the while loop. Reverts Vbus to 5V for next plug in event
                    LCD_wake();     //Wakes up monitor in case the unplug event was in sleep mode
                    select = 0;
                    cursorFlag = FALSE;
                    settings_menu = 1;fast_set = 2;
                    readTwoByte(0x02,BM92A_ADDRESS);    // clear alert flag in prep for display mode
                    displayMode();  //Display shows source mode fast set menu
                    readTwoByte(0x02,BM92A_ADDRESS);

                }
                else{   //if its just a register update
                   alertRead = readTwoByte(0x02,BM92A_ADDRESS);
               }
            }
            AlertFlag = FALSE;
        }
        __sleep();      // go to lower power mode and wait for interrupt

    }
}
