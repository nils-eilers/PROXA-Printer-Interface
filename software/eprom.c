/*
   PROXA PRINTER INTERFACE

   Copyright (c) 2015-2016 Nils Eilers <nils.eilers@gmx.de>

   This work is free. You can redistribute it and/or modify it under the
   terms of the Do What The Fuck You Want to Public License, Version 2,
   as published by Sam Hocevar. See the COPYING file for more details.

   This is a beautiful example of how not to write a parser.

*/


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include <ctype.h>
#include <stdbool.h>
#include <libgen.h> // basename

#define MAX_IMGSIZE     65536
#define MAX_LINE        1024

#ifdef _WIN32
#define DEFAULT_OUTFILENAME "EPROM.BIN"
#define DEFAULT_CFGFILENAME "EPROM.CFG"
#define OPTION "/"
#define OPTION_USAGE "?"
#else
#define DEFAULT_OUTFILENAME "eprom.bin"
#define DEFAULT_CFGFILENAME "eprom.cfg"
#define OPTION "-"
#define OPTION_USAGE "h"
#endif


#define IEEE_LISTEN      0x20
#define IEEE_UNLISTEN    0x3F
#define IEEE_SECONDARY   0x60
#define IEEE_OPEN        0xF0
#define IEEE_CLOSE       0xE0

#define STORE             128
#define PRINT              64
#define AUTOFEED           32


uint8_t eprom[MAX_IMGSIZE];
int imagesize = MAX_IMGSIZE;
int tables = MAX_IMGSIZE / 4096;

const int offset_conv_tbl[4] = { 0, 256, 2048, 2048 + 256 };
int conversion_table = 0;
int device_address = 4;

char line[MAX_LINE + 1];
char input[MAX_LINE + 1];
int lineno;

const char delim[] = " \t";
const char numdelim[] = " \t:|";

char* outfilename = NULL;
char* cfgfilename = NULL;
char* called_as;
bool debug = false;
bool cfgfilename_given = false;

bool autofeed = false;


void write_image(void) {
  FILE *out;

  if ((out = fopen(outfilename, "wb")) == NULL) {
    perror("unable to create image file");
    exit(EXIT_FAILURE);
  }
  if (fwrite(eprom, 1, imagesize, out) < imagesize) {
    perror("writing image file");
    exit(EXIT_FAILURE);
  }
  if (fclose(out)) {
    perror("closing image file");
    exit(EXIT_FAILURE);
  }
}


// Strip leading and trailing whitespaces
void trim(char* s) {
  // Strip trailing whitespace
  int size = strlen(s);
  if (size == 0) return;
  char *end = s + size - 1;
  while (end >= s && isspace(*end)) --end;
  *(end + 1) = '\0';
  // Strip leading whitespace
  while (isspace(s[0])) {
    memmove(s, s + 1, strlen(s) - 1);
    s[strlen(s) - 1] = '\0';
  }
}

// Reads a number from cfg file
int number(char* s) {
  char *endptr = NULL;
  trim(s);
  long int val = strtol(s, &endptr, 0);
  if (endptr == s) {
    fprintf(stderr, "%s:%d: Illegal or no value\n", cfgfilename,
        lineno);
    exit(EXIT_FAILURE);
  }
  return val;
}


// Output a syntax error message and abort
void syntax_error(void) {
  trim(input);
  fprintf(stderr, "Syntax error in line %u: '%s'\n", lineno, input);
  exit(EXIT_FAILURE);
}


// Process assignments to settings or character conversions
bool assignment(char* s) {
  char *p = strchr(s, '=');
  if (p == NULL) return false;
  *p++ = '\0';
  trim(s);
  trim(p);
  if (debug) printf("'%s'='%s'\n", s, p);
  if (!strcasecmp(s, "CHIP")) {
    char *endptr = NULL;
    long int chip = strtol(p, &endptr, 0);
    if (*endptr != '\0') {
      fprintf(stderr, "%s:%d: Illegal value '%s'\n",
          cfgfilename, lineno, p);
      fprintf(stderr, "Legal values are: 2732, 2764, 27128, 27256"
          " and 27512\n");
      exit(EXIT_FAILURE);
    }
    switch (chip) {
      case 2732:  imagesize = 4096;  tables =  1; break;
      case 2764:  imagesize = 8192;  tables =  2; break;
      case 27128: imagesize = 16384; tables =  4; break;
      case 27256: imagesize = 32768; tables =  8; break;
      case 27512: imagesize = 65536; tables = 16; break;
      default:
                  fprintf(stderr, "Illegal chip %s\n", p);
                  exit(EXIT_FAILURE);
    }
  } else if (!strcasecmp(s, "OUT")) {
    // Don't overwrite command line parameters
    if (outfilename != NULL) return true;
    outfilename = malloc(strlen(p) + 1);
    if (outfilename == NULL) {
      fprintf(stderr, "malloc failed\n");
      exit(EXIT_FAILURE);
    }
    strcpy(outfilename, p);
  } else if (!strcasecmp(s, "ADDRESS")) {
    device_address = number(p);
    // No range checks here, they're done later
  } else if (!strcasecmp(s, "AUTOFEED")) {
    if (!strcasecmp(p, "ON")) autofeed = true;
    else if (!strcasecmp(p, "OFF")) autofeed = false;
    else syntax_error();
  } else {
    fprintf(stderr, "Unknown identifier '%s'\n", s);
    exit(EXIT_FAILURE);
  }
  return true;
}


// Convert PETSCII to ASCII
uint8_t petscii_to_ascii(uint8_t v) {
  if (v > (128 + 64) && v < (128 + 91)) return v - 128;
  if (v > (96 - 32) && v < (123 - 32)) return v + 32;
  if (v > (192 - 128) && v < (219 - 128)) return v + 128;
  if (v == 255) return '~';
  return v;
}

// Inserts a character mapping in current conversion table
bool char_conversion(char* s) {
  int from, to;
  uint16_t addr;

  char *p = strchr(s, ':');
  if (p == NULL) p = strchr(s, '|');
  if (p == NULL) return false;
  *p++ = '\0';
  from = number(s);
  to   = number(p);
  if (debug) printf("%3u / 0x%02X ---> %3u / 0x%02x\n", from, from, to, to);
  for (int addr_i = 0; addr_i < 16; addr_i++) {
    addr = addr_i * 4096;   // Each table/device address uses 4 KB
    addr += 512;            // Q6 (print enable) must be high
    addr += offset_conv_tbl[conversion_table];
    addr += 255 - from;     // Characters are transmitted inverted
    eprom[addr] = to;
  }

  return true;
}


// Init EPROM table with IEEE-488 logic table
void init_logic(void) {
  int t, q, Q0, Q1, Q6, addr, s, a;

  for (t = 0; t < tables; t++) {
    // The device address switch uses inverted logic
    a = device_address + tables - 1 - t;

    if (a < 4 || a > 30) {
      fprintf(stderr, "Error: resulting device address "
          "not in the range of 4-30\n");
      exit(EXIT_FAILURE);
    }
    if (a >= 8 && a <= 11) {
      fprintf(stderr, "Warning: the resulting device address %u is "
          "usually used by floppy drives\n", a);
    }

    for (q = 0; q < 8; q++) {
      Q0 = (q & 1) ? 1 : 0;
      Q1 = (q & 2) ? 1 : 0;
      Q6 = (q & 4) ? 1 : 0;

      // LISTEN resets conversion table to zero and enables printing
      addr = 1024;        // /ATN must be low, inverted to A10 thus high
      addr += t * 4096;   // Each device address table occupies 4 KB
      addr += Q0 * 256;   // Q0: selects conversion table
      addr += Q1 * 2048;  // Q1: selects conversion table
      addr += Q6 * 512;   // Q6: printing enabled

      addr += 255 - (IEEE_LISTEN + a);
      eprom[addr] = PRINT | STORE;
      if (autofeed) eprom[addr] |= AUTOFEED;


      // UNLISTEN resets conversion table to zero and disables printing
      addr = 1024;        // /ATN must be low, inverted to A10 thus high
      addr += t * 4096;   // Each device address table occupies 4 KB
      addr += Q0 * 256;   // Q0: selects conversion table
      addr += Q1 * 2048;  // Q1: selects conversion table
      addr += Q6 * 512;   // Q6: printing enabled

      addr += 255 - (IEEE_UNLISTEN);
      eprom[addr] = STORE;
      if (autofeed) eprom[addr] |= AUTOFEED;


      // If printing is enabled, the secondary address encoded in the
      // OPEN and DATA selects the conversion table
      if (Q6) {
        for (s = 0; s < 4; s++) {
          // OPEN sets conversion table and enables printing
          addr = 1024;        // /ATN must be low, inverted to A10 thus high
          addr += t * 4096;   // Each device address table occupies 4 KB
          addr += Q0 * 256;   // Q0: selects conversion table
          addr += Q1 * 2048;  // Q1: selects conversion table
          addr += Q6 * 512;   // Q6: printing enabled

          addr += 255 - (IEEE_OPEN + s);
          eprom[addr] = s | PRINT | STORE;
          if (autofeed) eprom[addr] |= AUTOFEED;


          // SECONDARY sets conversion table and enables printing
          addr = 1024;        // /ATN must be low, inverted to A10 thus high
          addr += t * 4096;   // Each device address table occupies 4 KB
          addr += Q0 * 256;   // Q0: selects conversion table
          addr += Q1 * 2048;  // Q1: selects conversion table
          addr += Q6 * 512;   // Q6: printing enabled

          addr += 255 - (IEEE_SECONDARY + s);
          eprom[addr] = s | PRINT | STORE;
          if (autofeed) eprom[addr] |= AUTOFEED;
        }
      }
    }
  }
}


void create_image(void) {
  FILE *cfg;
  uint16_t addr;

  if ((cfg = fopen(cfgfilename, "rt")) == NULL) {
    perror("unable to open config file");
    exit(EXIT_FAILURE);
  }

  // Default to zero
  memset(eprom, 0, MAX_IMGSIZE);

  // Init conversion tables with raw data
  for (conversion_table = 0; conversion_table < 4; conversion_table++) {
    for (int addr_i = 0; addr_i < 16; addr_i++) {
      for (int i = 0; i < 256; i++) {
        addr = addr_i * 4096;   // Each device address table occupies 4 KB
        addr += 512;            // Q6 (print enable) must be high
        addr += offset_conv_tbl[conversion_table];
        addr += 255 - i;        // Characters are transmitted inverted
        eprom[addr] = i;
      }
    }
  }

  conversion_table = 0; // reset to default value

  for (;;) {
    ++lineno;
    if (fgets(line, MAX_LINE, cfg) == NULL) {
      if (feof(cfg)) break;     // EOF reached
      perror("reading config file");
      exit(EXIT_FAILURE);
    }
    // Skip comments
    if (line[0] == '#' || line[0] == ';') continue;

    strncpy(input, line, MAX_LINE);

    for (;;) {
      // Strip CR/LF, leading and trailing whitespace
      trim(line);
      if (line[0] == '\0') break;

      if (assignment(line)) break;

      strcpy(input, line);
      char *word;
      word = strtok(input, delim);

      if (word == NULL) break;;

      if (!strcasecmp(word, "CONVERSION")) {
        word = strtok(NULL, delim);
        if (strcasecmp(word, "TABLE")) syntax_error();
        word = strtok(NULL, numdelim);
        if (word == NULL) syntax_error();
        conversion_table = number(word);
        if (conversion_table < 0 || conversion_table > 3) {
          fprintf(stderr, "%s:%d: Illegal value (must be 0-3): '%s'\n",
              cfgfilename, lineno, line);
          exit(EXIT_FAILURE);
        }
        if (debug) printf("Conversion Table: %d\n", conversion_table);
        word = strtok(NULL, "\0");
        if (word == NULL) {
          break;
        } else {
          strcpy(line, word);
          continue;
        }
      } else if (!strcasecmp(word, "PETSCII")) {
        if (debug) printf("PETSCII\n");
        // Init current conversion table with PETSCII --> ASCII
        for (int addr_i = 0; addr_i < 16; addr_i++) {
          for (int i = 0; i < 256; i++) {
            addr = addr_i * 4096;   // Each table/device address uses 4 KB
            addr += 512;            // Q6 (print enable) must be high
            addr += offset_conv_tbl[conversion_table];
            addr += 255 - i;        // Characters are transmitted inverted
            eprom[addr] = petscii_to_ascii(i);
          }
        }
        break;
      } else if (!strcasecmp(word, "RAW") || !strcasecmp(word, "ASCII")) {
        if (debug) printf("RAW\n");
        // default, so nothing to do here
        break;
      } else {
        if (char_conversion(line)) break;
        else syntax_error();
      }
    }
  }

  init_logic();

  if (fclose(cfg)) {
    perror("closing config file");
    exit(EXIT_FAILURE);
  }
}


void usage(void) {
  printf("usage: %s [config file]\n\n"
      OPTION "o filename\tFilename for EPROM binary\n"
      OPTION "D\t\tOutput debug information\n"
      OPTION OPTION_USAGE "\t\tThis help screen\n\n", called_as);
}


int main(int argc, char* argv[]) {
  called_as = basename(argv[0]);

  for (int i=1; i < argc; i++) {
    if (!strcmp(argv[i], OPTION "o")) {
      if (argc < (i + 2)) {
        fprintf(stderr, "filename expected\n");
        return EXIT_FAILURE;
      }
      outfilename = argv[++i];
      continue;
    } else if (!strcmp(argv[i], OPTION OPTION_USAGE)) {
      usage();
      return EXIT_SUCCESS;
    } else if (!strcmp(argv[i], OPTION "D")) {
      debug = true;
    } else if (argv[i][0] == OPTION[0]) {
      fprintf(stderr, "Unknown option '%s'\n\n", argv[i]);
      usage();
      return EXIT_FAILURE;
    } else {
      if (cfgfilename_given) {
        fprintf(stderr, "Too many parameters\n");
        return EXIT_FAILURE;
      }
      cfgfilename_given = true;
      cfgfilename = argv[i];
    }
  }

  if (cfgfilename == NULL) cfgfilename = DEFAULT_CFGFILENAME;
  create_image();

  if (outfilename == NULL) outfilename = DEFAULT_OUTFILENAME;
  write_image();

  printf("%s written successfully.\n", outfilename);
  return EXIT_SUCCESS;
}

