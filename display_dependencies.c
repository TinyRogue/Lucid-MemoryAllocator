#include "display_dependencies.h"
#include "tested_declarations.h"
#include "rdebug.h"

void red() {
    printf("\033[0;31m");
}


void bred() {
    printf("\033[1;31m");
}


void green() {
    printf("\033[0;32m");
}


void bgreen() {
    printf("\033[1;32m");
}


void yellow() {
    printf("\033[0;33m");
}


void byellow() {
    printf("\033[1;33m");
}


void blue() {
    printf("\033[0;34m");
}


void bblue() {
    printf("\033[1;34m");
}


void magenta() {
    printf("\033[0;34m");
}


void bmagenta() {
    printf("\033[1;34m");
}


void cyan() {
    printf("\033[0;35m");
}


void bcyan() {
    printf("\033[1;35m");
}


void reset() {
    printf("\033[0m");
}

