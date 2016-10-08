/*------------------------------------------------------------
	Agat disk Emulator

	Copyright 2016 Konstantin Fedorov
	email: sintechs@gmail.com
------------------------------------------------------------*/

word mfm_encode(byte inByte, byte *lastBit) {
  word outByte = 0;
  byte oneBit = 0;
  for (int b = 7; b >= 0; b--) {
    oneBit = bitRead(inByte, b);
    if (oneBit == 1) {
      bitWrite(outByte, b * 2 + 1, 0);
      bitWrite(outByte, b * 2, 1);
    } else if (oneBit == 0 && *lastBit == 1) {
      bitWrite(outByte, b * 2 + 1, 0);
      bitWrite(outByte, b * 2, 0);
    } else if (oneBit == 0 && *lastBit == 0) {
      bitWrite(outByte, b * 2 + 1, 1);
      bitWrite(outByte, b * 2, 0);
    }
    *lastBit = oneBit;
  }
  return outByte;
}

word mfm_encode_fast(byte inByte, byte *lastBit) {
  word outByte = 0;
  byte oneBit = 0;
  for (int b = 7; b >= 0; b--) {
    oneBit = (inByte >> b) & 1;
    if (oneBit == 1) {
      outByte = outByte | B01 << b * 2;
    }
//    else if (oneBit == 0 && *lastBit == 1) {
//      outByte=outByte | B00 << b*2;
//    }
    else if (oneBit == 0 && *lastBit == 0) {
      outByte = outByte | B10 << b * 2;
    }
    *lastBit = oneBit;
  }
  return outByte;
}




void sendByte (byte oneByte) {
  for (int b = 7; b >= 0; b--) {
    byte oneBit = bitRead(oneByte, b);
    if (oneBit == 1) {
      digitalWriteFast(PIN_READ, LOW);
      usec_timer.delay_cycles((usec2cycles(1) + 10));
      //delayMicroseconds(1);
      digitalWriteFast(PIN_READ, HIGH);
      usec_timer.delay_cycles((usec2cycles(1) - 20));
      //delayMicroseconds(1);
    } else {
      usec_timer.delay_cycles((usec2cycles(2) - 20));
      //delayMicroseconds(2);
    }
  }
}

void sendByte140 (byte oneByte) {
  for (int b = 7; b >= 0; b--) {
    byte oneBit = bitRead(oneByte, b);
    if (oneBit == 1) {
      digitalWriteFast(PIN_READ140, HIGH);
      delayMicroseconds(1);
      digitalWriteFast(PIN_READ140, LOW);
      delayMicroseconds(3);
    } else {
      delayMicroseconds(4);
    }
  }
}

void sendTrack() {
  digitalWriteFast(PIN_INDEX, LOW);
  for (int j = 0; j < track_size; j++) {
    // break if track num changes
    if (track * 2 != real_track) break;
    if (j == 188) digitalWriteFast(PIN_INDEX, HIGH);
    sendByte(mfm_data[side][j]);
  }
}

void sendTrack140() {
 // data stream
  for (int i = 0; i < 16; i++) {
    // break if track num changes
    if (track140 / 2 != real_track140) break;
    for (int j = 0; j < 512; j++) {
      sendByte140(gcr_data[i][j]);
    }
 }
}


void sendTrackAIM(int markers[][4]) {
  if (markers[side][3] == 0) {
    markers[side][2] = 0;
    markers[side][3] = 190;
  }
  for (int i = 0; i < markers[side][0]; i += 2) {
    // break if track num changes
    if (track * 2 != real_track) break;
    sendByte(mfm_data[side][i]);
    sendByte(mfm_data[side][i + 1]);
    if (markers[side][2] == i) digitalWriteFast(PIN_INDEX, LOW);
    if (markers[side][3] == i) digitalWriteFast(PIN_INDEX, HIGH);
  }
}


void encode_track_aim(byte mfm_data[], byte image_data[], int markers[]) {
  byte dataByte = 0;
  byte ctrlByte = 0;
  word mfmWord = 0;
  byte lastBit = 1;

  //for (int track_side = 0; track_side < 2 ; track_side++) {
    //markers[track_side][0] = track_size;
    int idx = 0;
    for (int i = 0; i < track_size; i += 2) {
      ctrlByte = image_data[i + 1];
      dataByte = image_data[i];
      //ctrlByte=1;
      if (ctrlByte == 0) {
        mfmWord = mfm_encode_fast(dataByte, &lastBit);
        mfm_data[idx++] = highByte(mfmWord);
        mfm_data[idx++] = lowByte(mfmWord);
      } else if (ctrlByte == 1) {
        mfm_data[idx++] = 0x89;
        mfm_data[idx++] = 0x24;
        mfm_data[idx++] = 0x55;
        mfm_data[idx++] = 0x55;
        lastBit = 1;
      } else if (ctrlByte == 2) {
        markers[0] = idx;
        //markers[0] = i;
        break;
      } else if (ctrlByte == 3) {
        markers[2] = i;
      } else if (ctrlByte == 13) {
        markers[3] = i;
      }
    }
    markers[0] = idx;
  //}
}

void encode_track_dsk(byte mfm_data[], byte image_data[], int track_num) {
  byte volume_num = 254;
  byte lastBit = 1;
  word mfmWord = 0;
    int idx = 0;
    byte track = track_num;
    // track GAP1
    for (int i = 0; i < 13; i++) {
      mfmWord = mfm_encode_fast(0xAA, &lastBit);
      mfm_data[idx++] = highByte(mfmWord);
      mfm_data[idx++] = lowByte(mfmWord);
    }
    for (byte sector_num = 0; sector_num < 21; sector_num++) {
      lastBit = 1;
      // address desync
      mfm_data[idx++] = 0x89;
      mfm_data[idx++] = 0x24;
      mfm_data[idx++] = 0x55;
      mfm_data[idx++] = 0x55;
      // address: prologue, volume, track, sector, epilogue
      byte addr[] = {0x95, 0x6A, volume_num, track, sector_num, 0x5A};
      for (byte i = 0; i < sizeof(addr); i++) {
        mfmWord = mfm_encode_fast(addr[i], &lastBit);
        mfm_data[idx++] = highByte(mfmWord);
        mfm_data[idx++] = lowByte(mfmWord);
      }
      // GAP2
      for (byte i = 0; i < 5; i++) {
        mfmWord = mfm_encode_fast(0xAA, &lastBit);
        mfm_data[idx++] = highByte(mfmWord);
        mfm_data[idx++] = lowByte(mfmWord);
      }
      lastBit = 1;
      // data desync
      mfm_data[idx++] = 0x89;
      mfm_data[idx++] = 0x24;
      mfm_data[idx++] = 0x55;
      mfm_data[idx++] = 0x55;
      // data prologue
      byte data[260] = {0x6A, 0x95};
      int j = 2;
      int c = 0;
      for (int i = 0; i < 256; i++) {
        int dataIndex = i + sector_num * 256;
        byte dataByte = image_data[dataIndex];
        data[j++] = dataByte;
        if (c > 255) {
          c++;
          c = c & 255;
        }
        c = c + dataByte;
      }
      // checksum
      data[j++] = c & 255;
      // data epilogue
      data[j++] = 0x5A;
      for (unsigned int i = 0; i < sizeof(data); i++) {
        mfmWord = mfm_encode_fast(data[i], &lastBit);
        mfm_data[idx++] = highByte(mfmWord);
        mfm_data[idx++] = lowByte(mfmWord);
      }
      // track GAP3
      for (int i = 0; i < 22; i++) {
        mfmWord = mfm_encode_fast(0xAA, &lastBit);
        mfm_data[idx++] = highByte(mfmWord);
        mfm_data[idx++] = lowByte(mfmWord);
      }
    }
}

void encode_track_gcr(byte mfm_data[], byte image_data[],byte track_num) {
  byte volume_num = 254;

  for (byte sector_num = 0; sector_num < 16; sector_num++) {
    // 22 ffs
    for (byte i = 0; i < 22; i++) {
      gcr_data[sector_num][i] = 0xff;
    }
    // sync header
    gcr_data[sector_num][22] = 0x03;
    gcr_data[sector_num][23] = 0xfc;
    gcr_data[sector_num][24] = 0xff;
    gcr_data[sector_num][25] = 0x3f;
    gcr_data[sector_num][26] = 0xcf;
    gcr_data[sector_num][27] = 0xf3;
    gcr_data[sector_num][28] = 0xfc;
    gcr_data[sector_num][29] = 0xff;
    gcr_data[sector_num][30] = 0x3f;
    gcr_data[sector_num][31] = 0xcf;
    gcr_data[sector_num][32] = 0xf3;
    gcr_data[sector_num][33] = 0xfc;

    // address header
    gcr_data[sector_num][0x22] = 0xd5;
    gcr_data[sector_num][0x23] = 0xaa;
    gcr_data[sector_num][0x24] = 0x96;
    gcr_data[sector_num][0x25] = ((volume_num >> 1) | 0xaa);
    gcr_data[sector_num][0x26] = (volume_num | 0xaa);
    gcr_data[sector_num][0x27] = ((track_num >> 1) | 0xaa);
    gcr_data[sector_num][0x28] = (track_num | 0xaa);
    gcr_data[sector_num][0x29] = ((sector_num >> 1) | 0xaa);
    gcr_data[sector_num][0x2a] = (sector_num | 0xaa);
    byte chksum = (volume_num ^ track_num ^ sector_num);
    gcr_data[sector_num][0x2b] = ((chksum >> 1) | 0xaa);
    gcr_data[sector_num][0x2c] = (chksum | 0xaa);
    // address footer
    gcr_data[sector_num][0x2d] = 0xde;
    gcr_data[sector_num][0x2e] = 0xaa;
    gcr_data[sector_num][0x2f] = 0xeb;

    // sync header
    for (byte i = 0x30; i < 0x35; i++) gcr_data[sector_num][i] = 0xff;

    // data header
    gcr_data[sector_num][0x35] = 0xd5;
    gcr_data[sector_num][0x36] = 0xaa;
    gcr_data[sector_num][0x37] = 0xad;
    // data
    byte nib_sector_data[343];
    nibbalize_data (sector_num,image_data, nib_sector_data);
    for (int i = 0x38; i < 0x18f; i++) gcr_data[sector_num][i] = nib_sector_data[i - 0x38];

    gcr_data[sector_num][0x18f] = 0xde;
    gcr_data[sector_num][0x190] = 0xaa;
    gcr_data[sector_num][0x191] = 0xeb;
    for (int i = 0x192; i < 0x1a0; i++) gcr_data[sector_num][i] = 0xff;
    for (int i = 0x1a0; i < 0x200; i++) gcr_data[sector_num][i] = 0x00;
    //memcpy(track_data[sector_num], data, sizeof(data));
  }
  //delay(100);
}

void nibbalize_data (int sector_num,byte* source_data, byte* nib_sector_data) {
  int rev_i;
  byte prenibble[343];
  byte logical_sector_num = sectorMap[sector_num];
  for (int i = 0; i < 256; i++) {
    byte onebyte = source_data[logical_sector_num * 256 + i];
    byte twobits = onebyte & 0b00000011;

    prenibble[i] = (onebyte & 0b11111100) >> 2;
    if (i < 86) {
      rev_i = 342 - 1 - i;
      prenibble[rev_i] = bitflip[twobits];
    } else if (i >= 86 && i < 86 * 2) {
      rev_i = 342 - 1 - i + 86;
      prenibble[rev_i] = (bitflip[twobits] << 2) | prenibble[rev_i];
    } else if (i >= 86 * 2) {
      rev_i = 342 - 1 - i + 86 * 2;
      prenibble[rev_i] = (bitflip[twobits] << 4) | prenibble[rev_i];
    }
  }

  byte a = 0;
  byte b = 0;
  byte res = 0;
  int ind;
  for (int i = 0; i < 343; i++) {
    if (i < 86) ind = 342 - 1 - i; else ind = i - 86;
    b = prenibble[ind];
    if (i == 342) res = a; else res = a ^ b;
    nib_sector_data[i] = table62[res];
    a = b;
  }
}

