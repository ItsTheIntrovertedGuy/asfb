OPTIONS=-Wextra -Wall -Werror -pedantic -std=c99 -Wformat=2 -Wformat-truncation -Wundef -Wdouble-promotion -Wshadow -Wpointer-arith -Wcast-align -Wconversion -Wfloat-conversion -Wsign-conversion
DISABLEDWARNINGS=-Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers
OPTIMIZATIONS=-O0

INCLUDES=-I$${HOME}/Programming/C/include/
LIBRARIES=

CODEFLAGS=-g
FILE_MAIN_CODE=main.c
FILE_MAIN_OUTPUT=main

build:
	gcc $(FILE_MAIN_CODE) -o $(FILE_MAIN_OUTPUT) $(OPTIONS) $(DISABLEDWARNINGS) $(OPTIMIZATIONS) $(CODEFLAGS) $(LIBRARIES) $(INCLUDES)

run:
	./$(FILE_MAIN_OUTPUT)
