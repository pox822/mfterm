/**
 * Copyright (C) 2011 Anders Sundman <anders@4zm.org>
 *
 * This file is part of mfterm.
 *
 * mfterm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mfterm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mfterm.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mifare.h"
#include "util.h"
#include "tag.h"

mf_tag_t current_tag;
mf_tag_t current_auth;

void strip_non_auth_data(mf_tag_t* tag);
int load_mfd(const char* fn, mf_tag_t* tag);
int save_mfd(const char* fn, const mf_tag_t* tag);

int load_mfd_4k(const char *fn, mf_tag_t *tag);

int append_to_4k(const char *fn);

int load_mfd(const char *fn, mf_tag_t *tag) {
    int result = load_mfd_4k(fn, tag);
    if (result == 0) {
        return 0;
    }
    if (result == 2) {
        return 1;
    }

    if (append_to_4k(fn) == 1) {
        return 1;
    }

    if (load_mfd_4k(fn, tag) == 0) {
        return 0;
    }
    printf("Could not read file: %s\n", fn);
    return 1;
}

int append_to_4k(const char *fn) {
    FILE *mfd_file_to_append = fopen(fn, "a");
    if (mfd_file_to_append == NULL) {
        printf("Could not open file: %s\n", fn);
        return 1;
    }

    fseek(mfd_file_to_append, 0, SEEK_END);
    long currentSize = ftell(mfd_file_to_append);
    long initialSize = currentSize;
    if (initialSize != 1024) {
        printf("File needs to be either 1k or 4k\n");
        return 1;
    }
    while (currentSize < 4096) {
        fputc('0', mfd_file_to_append);
        currentSize++;
    }

    fclose(mfd_file_to_append);
    printf("Zeros appended for %ld to %ld\n", initialSize, currentSize);
    return 0;
}

int load_mfd_4k(const char *fn, mf_tag_t *tag) {
    FILE* mfd_file = fopen(fn, "rb");
    if (mfd_file == NULL) {
        printf("Could not open file: %s\n", fn);
        return 2;
    }

    if (fread(tag, 1, sizeof(mf_tag_t), mfd_file) == sizeof(mf_tag_t)) {
        fclose(mfd_file);
        return 0;
    }
    return 1;
}

int save_mfd(const char* fn, const mf_tag_t* tag) {
  FILE* mfd_file = fopen(fn, "w");

  if (mfd_file == NULL) {
    printf("Could not open file for writing: %s\n", fn);
    return 1;
  }

  if (fwrite(tag, 1, sizeof(mf_tag_t), mfd_file) != sizeof(mf_tag_t)) {
    printf("Could not write file: %s\n", fn);
    fclose(mfd_file);
    return 1;
  }

  fclose(mfd_file);
  return 0;
}

int load_tag(const char* fn) {
  return load_mfd(fn, &current_tag);
}

int save_tag(const char* fn) {
  return save_mfd(fn, &current_tag);
}

int load_auth(const char* fn) {
  if (load_mfd(fn, &current_auth))
    return 1;

  strip_non_auth_data(&current_auth);
  return 0;
}

int save_auth(const char* fn) {
  return save_mfd(fn, &current_auth);
}


int import_auth() {
  memcpy(&current_auth, &current_tag, sizeof(mf_tag_t));
  strip_non_auth_data(&current_auth);
  return 0;
}

void print_tag(mf_size_t size) {
  if (size == MF_1K)
    print_tag_block_range(0, MF_1K / sizeof(mf_block_t) - 1);
  else if (size == MF_4K)
    print_tag_block_range(0, MF_4K / sizeof(mf_block_t) - 1);
  else {
    printf("Unsupported tag size.\n");
  }
  return;
}

void print_tag_head() {
  print_tag_block_range(0, 3);
  return;
}

void print_tag_byte_bits(size_t byte, size_t first_bit, size_t last_bit) {

  // The byte to show parts of
  uint8_t data = current_tag.amb[byte / 16].mbd.abtData[byte % 16];

  printf("[");

  for (size_t i = 0; i < 8; ++i) {
    // Separate nibbles
    if (i == 4)
      printf(" ");

    // Outside mask
    if (i < first_bit || i > last_bit) {
      printf("-");
      continue;
    }

    // Inside mask
    if ((1<<i) & data)
      printf("1");
    else
      printf("0");
  }

  printf("]");
}

void print_tag_bytes(size_t first_byte, size_t last_byte) {

  // Write the data one block at a time
  while (first_byte <= last_byte) {

    size_t byte_len = last_byte - first_byte;

    // Fill up start with spaces
    size_t block_offset = first_byte % 16;
    for (size_t i = 0; i < block_offset; ++i)
      printf("-- ");

    // Print the data
    uint8_t* block_data = current_tag.amb[first_byte / 16].mbd.abtData;
    size_t block_last = block_offset + byte_len;
    if (block_last > 15)
      block_last = 15;
    print_hex_array_sep(block_data  + block_offset,
                        block_last - block_offset + 1, " ");

    // Fill up end with spaces
    for (size_t i = block_last; i < 15; ++i)
      printf("-- ");

    // Finish of with a nl
    printf("\n");

    first_byte += block_last - block_offset + 1;
  }
}

void print_tag_data_range(size_t byte_offset, size_t bit_offset,
                          size_t byte_len, size_t bit_len) {

  printf("Offset: [%zu, %zu] Length: [%zu, %zu]\n",
         byte_offset, bit_offset, byte_len, bit_len);

  // Print partial first byte
  if (bit_offset) {
    size_t total_bits = byte_len * 8 + bit_len;
    size_t last_bit = bit_offset + total_bits - 1;
    if (last_bit > 7)
      last_bit = 7;

    print_tag_byte_bits(byte_offset, bit_offset, last_bit);
    printf("\n");

    total_bits -= last_bit - bit_offset + 1;

    // Update data to be printed
    byte_offset++;
    bit_offset = 0;
    byte_len = total_bits / 8;
    bit_len = total_bits % 8;
  }

  // Print bytes
  if (byte_len) {
    print_tag_bytes(byte_offset, byte_offset + byte_len - 1);

    // Update data to be printed
    byte_offset += byte_len;
    byte_len = 0;
  }

  // Print trailing bits
  if (bit_len) {
    print_tag_byte_bits(byte_offset, 0, bit_len);
    printf("\n");
  }
}


void print_tag_block_range(size_t first, size_t last) {

  // Print header
  printf("xS  xB  00                   07 08                   0f        ASCII       \n");
  printf("---------------------------------------------------------------------------\n");

  // Iterate over all blocks
  for (size_t block = first; block <= last; ++block) {

    // Sector number
    printf("%02zx  ",
           block < 0x10*4 ? block / 4 : 0x10 + (block - 0x10*4) / 0x10);

    // Block number
    printf("%02zx  ", block);

    // then print the block data
    print_hex_array_sep(current_tag.amb[block].mbd.abtData,
                        sizeof(mf_block_t), " ");

    // finally, an ascii rendering
    printf(" [");
    print_ascii_rendering(current_tag.amb[block].mbd.abtData,
                         sizeof(mf_block_t), '.');
    printf("]");

    // EOL
    printf("\n");

    // Indicate sector bondaries with extra nl
    if (block < last && block < 16*4 && (block + 1) % 4 == 0)
      printf("\n");
    else if (block < last && block > 16*4 && (block + 1) % 16 == 0)
      printf("\n");
  }
}

void print_keys(const mf_tag_t* tag, mf_size_t size) {
  printf("xS  xB  KeyA          KeyB\n");
  printf("----------------------------------\n");
  for (int block = 3; block < 0x10 * 4; block += 4) {
    printf("%02x  %02x  ", block / 4, block);
    print_hex_array(tag->amb[block].mbt.abtKeyA, 6);
    printf("  ");
    print_hex_array(tag->amb[block].mbt.abtKeyB, 6);
    printf("\n");
  }

  if (size == MF_1K)
    return;

  printf("\n");

  for (int block = 0xf; block < 0x0c * 0x10; block += 0x10) {
    printf("%02x  %02x  ", 0x10 + block/0x10, 0x10*4 + block);
    print_hex_array(tag->amb[0x10*4 + block].mbt.abtKeyA, 6);
    printf("  ");
    print_hex_array(tag->amb[0x10*4 + block].mbt.abtKeyB, 6);
    printf("\n");
  }
}

void print_ac(const mf_tag_t* tag) {

  static const char* ac_data_str[8] = {
    /* 0 0 0 */ "   A|B A|B A|B A|B   .   .   .   .   .   .",
    /* 0 0 1 */ "   A|B  x   x  A|B   .   .   .   .   .   .",
    /* 0 1 0 */ "   A|B  x   x   x    .   .   .   .   .   .",
    /* 0 1 1 */ "    B   B   x   x    .   .   .   .   .   .",
    /* 1 0 0 */ "   A|B  B   x   x    .   .   .   .   .   .",
    /* 1 0 1 */ "    B   x   x   x    .   .   .   .   .   .",
    /* 1 1 0 */ "   A|B  B   B  A|B   .   .   .   .   .   .",
    /* 1 1 1 */ "    x   x   x   x    .   .   .   .   .   .",
  };

  static const char* ac_trailer_str[8] = {
    /* 0 0 0 */ "    .   .   .   .    x   A   A   x   A   A",
    /* 0 0 1 */ "    .   .   .   .    x   A   A   A   A   A",
    /* 0 1 0 */ "    .   .   .   .    x   x   A   x   A   x",
    /* 0 1 1 */ "    .   .   .   .    x   B  A|B  B   x   B",
    /* 1 0 0 */ "    .   .   .   .    x   B  A|B  x   x   B",
    /* 1 0 1 */ "    .   .   .   .    x   x  A|B  B   x   x",
    /* 1 1 0 */ "    .   .   .   .    x   x  A|B  x   x   x",
    /* 1 1 1 */ "    .   .   .   .    x   x  A|B  x   x   x",
  };

  // Print header
  printf("xS  xB  Raw       C1 C2 C3    R   W   I   D   AR  AW  ACR ACW BR  BW\n");
  printf("--------------------------------------------------------------------\n");

  // Iterate over all blocks (in 1k sectors)
  for (size_t block = 0; block < 0x10 * 4; ++block) {

    // Sector number
    printf("%02zx  ",
           block < 0x10*4 ? block / 4 : 0x10 + (block - 0x10*4) / 0x10);

    // Block number
    printf("%02zx  ", block);

    const uint8_t* ac = tag->amb[block_to_trailer(block)].mbt.abtAccessBits;

    // Print raw bytes
    print_hex_array(ac, 4);

    // Print the C1, C2, C3 bits
    int c1 = (ac[1] & 1<<(4 + (block % 4))) > 0;
    int c2 = (ac[2] & 1<<(0 + (block % 4))) > 0;
    int c3 = (ac[2] & 1<<(4 + (block % 4))) > 0;
    printf("   %d  %d  %d", c1, c2, c3);

    // Print enterpretation
    int c123 = (c1<<2) | (c2<<1) | c3;
    if (block % 4 < 3) {
      // Data block
      printf("%s", ac_data_str[c123]);
    }
    else {
      // Trailer block
      printf("%s", ac_trailer_str[c123]);
    }

    printf("\n");

    // Indicate sector bondaries with extra nl
    if ((block + 1) % 4 == 0)
      printf("\n");
  }
}


const char* sprint_key(const uint8_t* key) {
  static char str_buff[13];

  if (!key)
    return NULL;

  sprintf(str_buff, "%02x%02x%02x%02x%02x%02x",
          (unsigned int)(key[0]),
          (unsigned int)(key[1]),
          (unsigned int)(key[2]),
          (unsigned int)(key[3]),
          (unsigned int)(key[4]),
          (unsigned int)(key[5]));

  return str_buff;
}

// Return a string describing the tag type 1k|4k
const char* sprint_size(mf_size_t size) {
  static const char* str_1k = "1k";
  static const char* str_4k = "4k";

  if (size == MF_1K)
    return str_1k;

  if (size == MF_4K)
    return str_4k;

  return NULL;
}


uint8_t* read_key(uint8_t* key, const char* str) {
  if (!key || !str)
    return NULL;

  static char byte_tok[] = {0, 0, 0};
  char* byte_tok_end;
  for (int i = 0; i < 6; ++i) {
    byte_tok[0] = str[i*2];
    byte_tok[1] = str[i*2+1];
    key[i] = (uint8_t)strtol(byte_tok, &byte_tok_end, 16);
    if (*byte_tok_end != '\0') {
      return NULL;
    }
  }

  return key;
}

void clear_tag(mf_tag_t* tag) {
  memset((void*)tag, 0x00, MF_4K);
}

void strip_non_auth_data(mf_tag_t* tag) {
  static const size_t bs = sizeof(mf_block_t);

  // Clear 1k sector data 16 á 4 - only keep sector trailer
  for (size_t i = 0; i < 0x10; ++i)
    memset(((void*)tag) + i * 4 * bs, 0x00, 3 * bs);

  // Clear 2-4k sector data 12 á 16 - only keep sector trailer
  for (size_t i = 0; i < 0x0c; ++i)
    memset(((void*)tag) + 0x10 * 4 * bs + i * 0x10 * bs, 0x00, 0x0f * bs);
}


size_t block_count(mf_size_t size) {
  return size / 0x10;
}

size_t sector_count(mf_size_t size) {
  return size == MF_1K ? 0x10 : 0x1c;
}

int is_trailer_block(size_t block) {
  return (block + 1) % (block < 0x80 ? 4 : 0x10) == 0;
}

size_t block_to_sector(size_t block) {
  if (block < 0x10*4)
    return block / 4;

  return 0x10 + (block - 0x10*4) / 0x10;
}

size_t block_to_header(size_t block) {
  if (block < 0x10*4)
    return block + (block % 4);

  return block + (block % 0x10);
}

// Return the trailer block for the specified block
size_t block_to_trailer(size_t block)
{
  if (block < 0x10*4)
    return block + (3 - (block % 4));

  return block + (0xf - (block % 0x10));
}

// Return the trailer block for the specified sector
size_t sector_to_trailer(size_t sector) {
  if (sector < 0x10)
    return sector * 4 + 3;
  else
    return 0x10 * 4 + (sector - 0x10) * 0x10 + 0xf;
}

// Return the sector size (in blocks) that contains the block
size_t sector_size(size_t block) {
  return block < 0x10*4 ? 4 : 16;
}

// Extract the key for the block parameters sector of the tag and return it
uint8_t* key_from_tag(const mf_tag_t* tag,
                     mf_key_type_t key_type,
                     size_t block) {

  static uint8_t key[6];

  size_t trailer_block = block_to_trailer(block);

  if (key_type == MF_KEY_A)
    memcpy(key, tag->amb[trailer_block].mbt.abtKeyA, 6);
  else
    memcpy(key, tag->amb[trailer_block].mbt.abtKeyB, 6);

  return key;
}

// Write key to the sector of a tag, where the sector is specified by
// the block.
void key_to_tag(mf_tag_t* tag, const uint8_t* key,
                mf_key_type_t key_type, size_t block) {

  size_t trailer_block = block_to_trailer(block);

  if (key_type == MF_KEY_A)
    memcpy(tag->amb[trailer_block].mbt.abtKeyA, key, 6);
  else
    memcpy(tag->amb[trailer_block].mbt.abtKeyB, key, 6);
}

/**
 * Return block index of the first block in every sector in turn on
 * repeated calls. Initialize the iterator by calling with state
 * 0. Subsequent calls should use the tag size as state. The iterator
 * returns -1 as an end marker.
 */
int sector_header_iterator(int state) {
  static int block;

  if (state == 0)
    return block = 0;

  if (block + 4 < 0x10*4)
    return block += 4;

  if (state == MF_1K) // End marker for 1k state
    return -1;

  if (block + 0x10 < 0x100)
    return block += 0x10;

  return -1; // End marker for 4k state
}
