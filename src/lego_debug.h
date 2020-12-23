#ifndef LEGO_DEBUG_H
#define LEGO_DEBUG_H

//#define TERM_COLOR_Black "\u001b[30m"
#define TERM_COLOR_GRAY "\e[37m"
#define TERM_COLOR_RED "\e[91m"
#define TERM_COLOR_GREEN "\e[92m"
#define TERM_COLOR_ORANGE "\e[38;5;214m"
#define TERM_COLOR_YELLOW "\e[93m"
#define TERM_COLOR_BLUE "\e[94m"
#define TERM_COLOR_MAGENTA "\e[35m"
#define TERM_COLOR_PURPLE "\e[357m"
#define TERM_COLOR_CYAN "\e[96m"
#define TERM_COLOR_WHITE "\e[97m"
#define TERM_COLOR_RESET "\e[0m"

#define F_CONFIG_BAUD SERIAL_SPEED

String debugHeader(void);

void debugSetup();
void debugLoop(void);
void debugEverySecond(void);
void debugStart(void);
void debugStop(void);

void serialPrintln(String & debugText, uint8_t level);
void serialPrintln(const char * debugText, uint8_t level);

#endif