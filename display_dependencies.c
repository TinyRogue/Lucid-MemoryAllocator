#include "display_dependencies.h"


void red() {
    printf("\033[0;31m");
}


void bred() {
    printf("\033[1;31m");
}


void green() {
    printf("\033[0;32m");
}


void bold_green() {
    printf("\033[1;32m");
}


void yellow() {
    printf("\033[0;33m");
}


void bold_yellow() {
    printf("\033[1;33m");
}


void blue() {
    printf("\033[0;34m");
}


void bold_blue() {
    printf("\033[1;34m");
}


void magenta() {
    printf("\033[0;34m");
}


void bold_magenta() {
    printf("\033[1;34m");
}


void cyan() {
    printf("\033[0;35m");
}


void bold_cyan() {
    printf("\033[1;35m");
}


void reset() {
    printf("\033[0m");
}

