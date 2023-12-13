#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include "libdither.h"

// settings struct

typedef struct PrintSettings {
  cups_bool_t InsertSheet;
  cups_adv_t AdvanceMedia;
  cups_cut_t CutMedia;
  unsigned int AdvanceDistance;
  unsigned int DitherMode;
} PrintSettings;

void ReadSettings(cups_page_header2_t *header, PrintSettings *settings) {
  settings->AdvanceMedia = header->AdvanceMedia;
  settings->CutMedia = header->CutMedia;
  settings->AdvanceDistance = header->AdvanceDistance;
  settings->DitherMode = header->cupsInteger[0];
}

void initPrinter() {
  char buf[] = {0x1b, 0x40};
  fwrite(buf, 1, sizeof(buf), stdout);
}

void imageMode(uint16_t xsize, uint16_t ysize) {
  char buf[] = {0x1d, 0x76, 0x30, 0, xsize&0xff, xsize>>8, ysize&0xff, ysize>>8};
  fwrite(buf, 1, sizeof(buf), stdout);
}

void feedLines(uint8_t amount) {
  char buf[] = {0x1b, 0x64, amount};
  fwrite(buf, 1, sizeof(buf), stdout);
}

void cutPaper() {
  char buf[] = {0x1b, 0x6d};
  fwrite(buf, 1, sizeof(buf), stdout);
}

uint16_t pixelToByte(uint16_t position) {;
  position = (position+7) & 0xFFFFFFF8;
  position = position >> 3;
  return position;
}


int main(int argc, char *argv[]) {
  cups_raster_t *ras = cupsRasterOpen(0, CUPS_RASTER_READ);
  cups_page_header2_t header;
  int page = 0;
  unsigned char *rasterLine = NULL;
  PrintSettings settings;
  while (cupsRasterReadHeader2(ras, &header)) {
    page++;
    ReadSettings(&header, &settings);
    initPrinter();
    fprintf(stdout, "PAGE: %i/%i\n", page, header.NumCopies);
    fprintf(stdout, "BPP: %u\n", header.cupsBitsPerPixel);
    fprintf(stdout, "BitsPerColour: %u\n", header.cupsBitsPerColor);
    fprintf(stdout, "Width: %u\n", header.cupsWidth);
    fprintf(stdout, "Height: %u\n", header.cupsHeight);
    
    rasterLine = malloc(header.cupsBytesPerLine);

    for (unsigned int y = 0; y < header.cupsHeight; y++) {
      if (cupsRasterReadPixels(ras, rasterLine, header.cupsBytesPerLine) == 0) {
        break;
      }
      uint16_t outSize = pixelToByte(header.cupsWidth);
      imageMode(outSize, 1);

      unsigned char *outputLine = NULL;
      outputLine = malloc(outSize);

      for(unsigned int pixel = 0; pixel < header.cupsWidth; pixel++) {
        unsigned char pdata = rasterLine[pixel];
        if (pdata < 128) {
          outputLine[pixel/8] |= 1 << (7-pixel%8);
        } else {
          outputLine[pixel/8] |= 0 << (7-pixel%8);
        }
      }
      fwrite(outputLine, 1, outSize, stdout);
      free(outputLine);
      outputLine = NULL;
    }
    
    free(rasterLine);
    rasterLine = NULL;
    
    //advance every page
    if (settings.AdvanceMedia == CUPS_ADVANCE_PAGE) {
      feedLines(settings.AdvanceDistance);
    }

    //cut every page
    if (settings.CutMedia == CUPS_CUT_PAGE) {
      cutPaper();
    }
    
  }
  cupsRasterClose(ras);

  //advance every job
  if (settings.AdvanceMedia == CUPS_ADVANCE_JOB) {
    feedLines(settings.AdvanceDistance);
  }

  //cut every job
  if (settings.CutMedia == CUPS_CUT_JOB) {
    cutPaper();
  }

  return 0;
}
