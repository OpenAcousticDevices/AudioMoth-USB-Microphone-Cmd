/****************************************************************************
 * main.c
 * openacousticdevices.info
 * April 2024
 *****************************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "hidapi.h"

/* Debug constant */

#define DEBUG                                   false

/* Buffer constants */

#define USB_SERIAL_NUMBER_OFFSET                5
#define USB_SERIAL_NUMBER_LENGTH                16

#define ARGUMENT_BUFFER_SIZE                    1024
#define SERIAL_NUMBER_BUFFER_SIZE               1024

#define MAXIMUM_NUMBER_OF_DEVICES               256

/* Configuration constants */

#define NUMBER_OF_SAMPLE_RATES                  8
#define FILTER_FREQ_MULTIPLIER                  100
#define MAXIMUM_FILTER_FREQUENCY                192000

/* HID configuration constants */

#define HID_CONFIGURATION_MESSAGE               0x01
#define HID_UPDATE_GAIN_MESSAGE                 0x02
#define HID_SET_LED_MESSAGE                     0x03
#define HID_RESTORE_MESSAGE                     0x04

/* Return state */

#define OKAY_RESPONSE                           0
#define ERROR_RESPONSE                          1

/* USB constants */

#define USB_PACKETSIZE                          64
#define AUDIOMOTH_USB_VID                       0x16D0
#define AUDIOMOTH_USB_PID                       0x06F3

/* Filter type enum */

typedef enum {NO_FILTER, LOW_PASS_FILTER, BAND_PASS_FILTER, HIGH_PASS_FILTER} filterType_t;

/* Operation enum */

typedef enum {NO_OP, LIST_OP, CONFIG_OP, UPDATE_GAIN_OP, SET_LED_OP, RESTORE_OP} operationType_t;

/* Configuration value arrays */

static int validSampleRates[NUMBER_OF_SAMPLE_RATES] = {8000, 16000, 32000, 48000, 96000, 192000, 250000, 384000};

static uint32_t sampleRates[NUMBER_OF_SAMPLE_RATES] = {384000, 384000, 384000, 384000, 384000, 384000, 250000, 384000};

static uint32_t sampleRateDiAUDIOMOTH_USB_VIDers[NUMBER_OF_SAMPLE_RATES] = {48, 24, 12, 8, 4, 2, 1, 1};

/* USB configuration data structure */

#pragma pack(push, 1)

typedef struct {
    uint32_t time;
    uint8_t gain;
    uint8_t clockDiAUDIOMOTH_USB_VIDer;
    uint8_t acquisitionCycles;
    uint8_t oversampleRate;
    uint32_t sampleRate;
    uint8_t sampleRateDiAUDIOMOTH_USB_VIDer;
    uint16_t lowerFilterFreq;
    uint16_t higherFilterFreq;
    uint8_t enableEnergySaverMode : 1; 
    uint8_t disable48HzDCBlockingFilter : 1;
    uint8_t enableLowGainRange : 1;
    uint8_t disableLED : 1;
} configSettings_t;

#pragma pack(pop)

static configSettings_t defaultConfigSettings = {
    .time = 0,
    .gain = 2,
    .clockDiAUDIOMOTH_USB_VIDer = 4,
    .acquisitionCycles = 16,
    .oversampleRate = 1,
    .sampleRate = 384000,
    .sampleRateDiAUDIOMOTH_USB_VIDer = 1,
    .lowerFilterFreq = 0,
    .higherFilterFreq = 0,
    .enableEnergySaverMode = 0,
    .disable48HzDCBlockingFilter = 0,
    .enableLowGainRange = 0,
    .disableLED = 0
};

/* USB buffers */

static uint8_t usbInputBuffer[USB_PACKETSIZE];

static uint8_t usbOutputBuffer[USB_PACKETSIZE];

/* Serial number buffer */

static char parsedSerialNumbers[MAXIMUM_NUMBER_OF_DEVICES][USB_SERIAL_NUMBER_LENGTH + 1];

static char currentSerialNumber[SERIAL_NUMBER_BUFFER_SIZE];

static wchar_t currentWideSerialNumber[SERIAL_NUMBER_BUFFER_SIZE];

/* Function to print buffer */

static void printBuffer(uint8_t *buffer) {
    
    for (int i = 0; i < USB_PACKETSIZE - 1; i += 1) {
        
        printf("%02x ", buffer[i]);
        
    }
    
    printf("%02x\n", buffer[USB_PACKETSIZE - 1]);
    
}

/* Convert wide char to char */

static bool convertToNarrow(wchar_t *src, char *dest, int bufferLength) {
  
    int i = 0;
    
    if (src == NULL) return false;

    while (src[i] != '\0' && i < bufferLength - 1) {
      
        wchar_t code = src[i];
        
        if (code < 128) {
            
            dest[i] = (char)code;
            
        } else {
            
            dest[i] = '?';
            
            if (code >= 0xD800 && code <= 0xD8FF) i = i + 1;
            
        }
        
        i = i + 1;
        
    }

    if (bufferLength > 0) dest[i] = '\0';

    return true;

}

/* Argument parsing functions */

static bool parseArgument(char *pattern, char *text) {

    static char buffer[ARGUMENT_BUFFER_SIZE];

    for (int i = 0; i < ARGUMENT_BUFFER_SIZE; i += 1) {

        buffer[i] = toupper(text[i]);

        if (buffer[i] == 0) break;

    }

    return strncmp(pattern, buffer, ARGUMENT_BUFFER_SIZE) == 0;

}

static bool parseNumber(char *text, int *number) {

    for (int i = 0; i < ARGUMENT_BUFFER_SIZE; i += 1) {

        char character = text[i];

        if (character == 0) break;

        if (character < '0' || character > '9') return false;

    }

    if (number != NULL) *number = atoi(text);

    return true;

}

static bool parseSerialNumber(char *text, char *serialNumber) {

    int i = 0;

    while (i < USB_SERIAL_NUMBER_LENGTH) {

        serialNumber[i] = toupper(text[i]);

        bool valid = (serialNumber[i] >= '0' && serialNumber[i] <= '9') || (serialNumber[i] >= 'A' && serialNumber[i] <= 'F');

        if (valid == false) return false;

        i += 1;

    }

    serialNumber[i] = '\0';

    return text[i] == '\0';

}

static bool parseNumberAgainstList(char *text, int *validNumbers, int length, int *index) {

    int value;

    bool isNumber = parseNumber(text, &value);

    if (isNumber == false) return false;

    for (int i = 0; i < length; i += 1) {

        if (value == validNumbers[i]) {

            *index = i;

            return true;

        }

    }

    return false;

}

/* Function to send command to USB device */

static bool communicate(operationType_t operationType, char *path) {

    /* Open device */

    hid_device *device = hid_open_path(path);

    if (device == NULL) return false;

    /* Initialise receiver and transmit buffers */
 
    memset(usbOutputBuffer, 0, USB_PACKETSIZE);

    memset(usbInputBuffer, 0, USB_PACKETSIZE);

    if (operationType == CONFIG_OP) {

        usbOutputBuffer[1] = HID_CONFIGURATION_MESSAGE;

        memcpy(usbOutputBuffer + 2, &defaultConfigSettings, sizeof(configSettings_t));

    } else if (operationType == UPDATE_GAIN_OP) {

        usbOutputBuffer[1] = HID_UPDATE_GAIN_MESSAGE;

        memcpy(usbOutputBuffer + 2, &defaultConfigSettings, sizeof(configSettings_t));

    } else if (operationType == SET_LED_OP) {

        usbOutputBuffer[1] = HID_SET_LED_MESSAGE;

        memcpy(usbOutputBuffer + 2, &defaultConfigSettings, sizeof(configSettings_t));

    } else if (operationType == RESTORE_OP) {

        usbOutputBuffer[1] = HID_RESTORE_MESSAGE;

    } 

    /* Write buffer to device */

    hid_write(device, usbOutputBuffer, USB_PACKETSIZE);

    if (DEBUG) printBuffer(usbOutputBuffer);

    /* Read response from device */

    int length = hid_read_timeout(device, usbInputBuffer, USB_PACKETSIZE, 100);

    if (DEBUG) printBuffer(usbInputBuffer);

    hid_close(device);

    /* Check response length and contents */

    if (length != USB_PACKETSIZE) return false;

    int lengthToCheck = operationType == RESTORE_OP ? 1 : 1 + sizeof(configSettings_t);

    for (int i = 0; i < lengthToCheck; i += 1) {

        if (usbOutputBuffer[i + 1] != usbInputBuffer[i]) return false;

    }
                
    return true;

}
               
/* Main function */

int main(int argc, char **argv) {

    /* Display version number */

    puts("AudioMoth-USB-Microphone 1.0.0");

    /* Parse variables */

    bool parseError = false;

    int numberOfSerialNumbers = 0;

    operationType_t operationType = NO_OP;

    /* Settings variable */

    filterType_t filterType = NO_FILTER;

    int gain, index, lowerFilterFreq, higherFilterFreq;

    /* Exit if no arguments */

    if (argc == 1) return OKAY_RESPONSE;

    /* Parse first argument */

    int argumentCounter = 1;

    char *argument = argv[argumentCounter];

    if (parseArgument("LIST", argument)) {

        operationType = LIST_OP;

    } else if (parseArgument("RESTORE", argument)) {

        operationType = RESTORE_OP;

    } else if (parseArgument("CONFIG", argument)) {

        operationType = CONFIG_OP;

    } else if (parseArgument("LED", argument)) {

        operationType = SET_LED_OP;

        argumentCounter += 1;

        if (argumentCounter == argc) {

            parseError = true;

        } else {

            argument = argv[argumentCounter];

            if (parseArgument("TRUE", argument) || parseArgument("ON", argument) || parseArgument("1", argument)) {

                defaultConfigSettings.disableLED = false;

            } else if (parseArgument("FALSE", argument) || parseArgument("OFF", argument) || parseArgument("0", argument)) {

                defaultConfigSettings.disableLED = true;

            } else {

                parseError = true;

            }

        }

    } else if (parseArgument("UPDATE", argument)) {

        operationType = UPDATE_GAIN_OP;

    } else {

        parseError = true;

    }    

    /* Parsing additional arguments */

    argumentCounter += 1;

    while (argumentCounter < argc && parseError == false) {

        argument = argv[argumentCounter];

        if (parseSerialNumber(argument, parsedSerialNumbers[numberOfSerialNumbers]) && operationType != LIST_OP) {

            numberOfSerialNumbers += 1;

        } else if (parseNumberAgainstList(argument, validSampleRates, NUMBER_OF_SAMPLE_RATES, &index) && operationType == CONFIG_OP) {

            defaultConfigSettings.sampleRate = sampleRates[index];

            defaultConfigSettings.sampleRateDiAUDIOMOTH_USB_VIDer = sampleRateDiAUDIOMOTH_USB_VIDers[index];
        
        } else if ((parseArgument("GAIN", argument) || parseArgument("G", argument)) && (operationType == CONFIG_OP || operationType == UPDATE_GAIN_OP)) {

            argumentCounter += 1;

            if (argumentCounter == argc) {

                parseError = true;

                break;

            }

            argument = argv[argumentCounter];

            bool valid = parseNumber(argument, &gain);

            if (valid) valid = gain >= 0 && gain <= 4;

            if (valid) {

                defaultConfigSettings.gain = gain;

            } else {

                parseError = true;

            }

        } else if ((parseArgument("LOWPASSFILTER", argument) || parseArgument("LPF", argument)) && operationType == CONFIG_OP && filterType == NO_FILTER) {

            filterType = LOW_PASS_FILTER;

            argumentCounter += 1;

            if (argumentCounter == argc) {

                parseError = true;

                break;

            }

            argument = argv[argumentCounter];

            bool valid = parseNumber(argument, &higherFilterFreq);

            if (valid) valid = higherFilterFreq <= MAXIMUM_FILTER_FREQUENCY && higherFilterFreq % FILTER_FREQ_MULTIPLIER == 0;

            if (valid) {

                defaultConfigSettings.lowerFilterFreq = UINT16_MAX;
                defaultConfigSettings.higherFilterFreq = higherFilterFreq / FILTER_FREQ_MULTIPLIER;

            } else {

                parseError = true;

            }

        } else if ((parseArgument("HIGHPASSFILTER", argument) || parseArgument("HPF", argument)) && operationType == CONFIG_OP && filterType == NO_FILTER) {

            filterType = HIGH_PASS_FILTER;

            argumentCounter += 1;

            if (argumentCounter == argc) {

                parseError = true;

                break;

            }

            argument = argv[argumentCounter];

            bool valid = parseNumber(argument, &lowerFilterFreq);

            if (valid) valid = lowerFilterFreq <= MAXIMUM_FILTER_FREQUENCY && lowerFilterFreq % FILTER_FREQ_MULTIPLIER == 0;

            if (valid) {

                defaultConfigSettings.lowerFilterFreq = lowerFilterFreq / FILTER_FREQ_MULTIPLIER;
                defaultConfigSettings.higherFilterFreq = UINT16_MAX;

            } else {

                parseError = true;

            }

        } else if ((parseArgument("BANDPASSFILTER", argument) || parseArgument("BPF", argument)) && operationType == CONFIG_OP && filterType == NO_FILTER) {

            filterType = BAND_PASS_FILTER;

            argumentCounter += 1;

            if (argumentCounter == argc) {

                parseError = true;

                break;

            }

            argument = argv[argumentCounter];

            bool valid = parseNumber(argument, &lowerFilterFreq);

            if (valid) valid = lowerFilterFreq <= MAXIMUM_FILTER_FREQUENCY && lowerFilterFreq % FILTER_FREQ_MULTIPLIER == 0;

            if (valid == false) {

                parseError = true;

            }
                
            argumentCounter += 1;

            if (argumentCounter == argc) {

                parseError = true;

                break;

            }

            argument = argv[argumentCounter];

            valid = parseNumber(argument, &higherFilterFreq);

            if (valid) valid = higherFilterFreq <= MAXIMUM_FILTER_FREQUENCY && higherFilterFreq % FILTER_FREQ_MULTIPLIER == 0;

            if (valid) {

                defaultConfigSettings.lowerFilterFreq = lowerFilterFreq / FILTER_FREQ_MULTIPLIER;
                defaultConfigSettings.higherFilterFreq = higherFilterFreq / FILTER_FREQ_MULTIPLIER;
                
            } else {

                parseError = true;

            }

        } else if ((parseArgument("LOWGAINRANGE", argument) || parseArgument("LGR", argument)) && (operationType == CONFIG_OP || operationType == UPDATE_GAIN_OP)) {

            defaultConfigSettings.enableLowGainRange = true;

        } else if ((parseArgument("ENERGYSAVERMODE", argument) || parseArgument("ESM", argument)) && operationType == CONFIG_OP) {

            defaultConfigSettings.enableEnergySaverMode = true;

        } else if ((parseArgument("DISABLE48HZ", argument) || parseArgument("D48", argument)) && operationType == CONFIG_OP) {

            defaultConfigSettings.disable48HzDCBlockingFilter = true;

        } else {

            parseError = true;

        } 

        argumentCounter += 1;

    }

    /* Return on error so far */

    if (parseError) {

        puts("[ERROR] Could not parse arguments.");

        return ERROR_RESPONSE;

    }

    /* Check filter values */

    if (filterType == BAND_PASS_FILTER && defaultConfigSettings.lowerFilterFreq >= defaultConfigSettings.higherFilterFreq) {

        puts("[ERROR] Band-pass lower frequency is not less than higher frequency.");

        return ERROR_RESPONSE; 

    }

    int nyquistFrequency = defaultConfigSettings.sampleRate / defaultConfigSettings.sampleRateDiAUDIOMOTH_USB_VIDer / FILTER_FREQ_MULTIPLIER / 2;

    if (filterType == LOW_PASS_FILTER && defaultConfigSettings.higherFilterFreq > nyquistFrequency) {

        puts("[ERROR] Low-pass frequency is not compatible with sample rate.");

        return ERROR_RESPONSE; 

    } else if (filterType == HIGH_PASS_FILTER && defaultConfigSettings.lowerFilterFreq > nyquistFrequency) {

        puts("[ERROR] High-pass frequency is not compatible with sample rate.");

        return ERROR_RESPONSE; 

    } else if (filterType == BAND_PASS_FILTER && defaultConfigSettings.lowerFilterFreq > nyquistFrequency) {

        puts("[ERROR] Band-pass lower frequency is not compatible with sample rate.");

        return ERROR_RESPONSE; 

    } else if (filterType == BAND_PASS_FILTER && defaultConfigSettings.higherFilterFreq > nyquistFrequency) {

        puts("[ERROR] Band-pass higher frequency is not compatible with sample rate.");

        return ERROR_RESPONSE; 

    }

    /* Check for repeated serial numbers */ 

    for (int i = 0; i < numberOfSerialNumbers; i += 1) {

        for (int j = 0; j < numberOfSerialNumbers; j += 1) {

            if (i != j && strncmp(parsedSerialNumbers[i], parsedSerialNumbers[j], USB_SERIAL_NUMBER_LENGTH) == 0) {

                puts("[ERROR] Repeated device ID.");

                return ERROR_RESPONSE; 

            }

        }

    }

    /* Access enumerated devices */
    
    struct hid_device_info *deviceInfo = hid_enumerate(AUDIOMOTH_USB_VID, AUDIOMOTH_USB_PID);
    
    /* Perform the requested action */

    char *operationStrings[] = {"CONFIG", "UPDATE", "LED", "RESTORE"};

    if (operationType == LIST_OP) {

        /* List enumerated AudioMoth USB Microphone */

        while (deviceInfo != NULL) {
        
            char *path = deviceInfo->path;
            
            if (path != NULL) {
        
                bool success = convertToNarrow(deviceInfo->serial_number, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
            
                if (success == false) {
            
                    hid_device *device = hid_open_path(path);

                    if (device != NULL) {
               
                        int error = hid_get_serial_number_string(device, currentWideSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                   
                        if (error == false) {
                       
                            success = convertToNarrow(currentWideSerialNumber, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                       
                        }
               
                    }
               
                }
        
                if (success) {
            
                    char *currentSerialNumberPtr = strstr(currentSerialNumber, "_") + 1;
        
                    if (currentSerialNumberPtr == currentSerialNumber + USB_SERIAL_NUMBER_OFFSET && strlen(currentSerialNumberPtr) == USB_SERIAL_NUMBER_LENGTH) {

                        int j = 0;
            
                        bool digit = false;
                        
                        char frequencyString[] = {0, 0, 0, 0};
            
                        for (int i = 0; i < USB_SERIAL_NUMBER_OFFSET - 1; i += 1) {
                    
                            if (digit || currentSerialNumber[i] > '0') {
                        
                                frequencyString[j++] = currentSerialNumber[i];

                                digit = true;

                            }

                        }

                        printf("%s - %skHz AudioMoth USB Microphone\n", currentSerialNumberPtr, frequencyString);
                            
                    }
                        
                }
                
            }
            
            deviceInfo = deviceInfo->next;

        }

    } else if (deviceInfo == NULL) {
        
        puts("[WARNING] No AudioMoth USB Microphones found.");

    } else if (numberOfSerialNumbers == 0) {

        /* Send CONFIG, UPDATE, LED or RESTORE to all connected AudioMoth USB Microphone */

        while (deviceInfo != NULL) {

            char *path = deviceInfo->path;
            
            if (path != NULL) {
        
                bool success = convertToNarrow(deviceInfo->serial_number, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
            
                if (success == false) {
            
                    hid_device *device = hid_open_path(path);

                    if (device != NULL) {
                   
                        int error = hid_get_serial_number_string(device, currentWideSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                           
                        if (error == false) {
                           
                            success = convertToNarrow(currentWideSerialNumber, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                           
                        }
                   
                    }
                   
                    hid_close(device);
               
                }
            
                if (success) {
                    
                    char *currentSerialNumberPtr = strstr(currentSerialNumber, "_") + 1;

                    if (currentSerialNumberPtr == currentSerialNumber + USB_SERIAL_NUMBER_OFFSET && strlen(currentSerialNumberPtr) == USB_SERIAL_NUMBER_LENGTH) {

                        bool completed = communicate(operationType, path);

                        if (completed) {

                            char *operationString = operationStrings[operationType - 2];

                            printf("Sent %s command to device ID %s.\n", operationString, currentSerialNumberPtr);

                        } else {
                            
                            printf("[ERROR] Problem communicating with device ID %s.\n", currentSerialNumberPtr);

                        }

                    }
                    
                }
                
            }

            deviceInfo = deviceInfo->next;

        }

    } else {

        /* Send CONFIG, UPDATE, LED or RESTORE to AudioMoth USB Microphone specified by serial number */

        struct hid_device_info *firstDeviceInfo = deviceInfo;

        for (int i = 0; i < numberOfSerialNumbers; i += 1) {

            bool found = false;

            deviceInfo = firstDeviceInfo;

            while (deviceInfo != NULL) {
            
                char *path = deviceInfo->path;
            
                if (path != NULL) {
        
                    bool success = convertToNarrow(deviceInfo->serial_number, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
            
                    if (success == false) {
            
                        hid_device *device = hid_open_path(path);

                        if (device != NULL) {
                   
                            int error = hid_get_serial_number_string(device, currentWideSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                       
                            if (error == false) {
                       
                                success = convertToNarrow(currentWideSerialNumber, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);
                       
                            }
           
                        }
               
                        hid_close(device);
               
                    }

                    if (success) {

                        convertToNarrow(deviceInfo->serial_number, currentSerialNumber, SERIAL_NUMBER_BUFFER_SIZE);

                        char *currentSerialNumberPtr = strstr(currentSerialNumber, "_") + 1;

                        if (currentSerialNumberPtr == currentSerialNumber + USB_SERIAL_NUMBER_OFFSET && strlen(currentSerialNumberPtr) == USB_SERIAL_NUMBER_LENGTH && strncmp(currentSerialNumberPtr, parsedSerialNumbers[i], USB_SERIAL_NUMBER_LENGTH) == 0) {

                            bool completed = communicate(operationType, path);

                            if (completed) {

                                char *operationString = operationStrings[operationType - 2];
                                
                                printf("Sent %s command to device ID %s.\n", operationString, currentSerialNumberPtr);

                            } else {
                                
                                printf("[ERROR] Problem communicating with device ID %s.\n", currentSerialNumberPtr);

                            }

                            found = true;

                        }

                    }

                }

                deviceInfo = deviceInfo->next;

            }

            if (found == false) printf("[ERROR] Could not find device ID %s.\n", parsedSerialNumbers[i]);

        }

    }

    return OKAY_RESPONSE;

}
