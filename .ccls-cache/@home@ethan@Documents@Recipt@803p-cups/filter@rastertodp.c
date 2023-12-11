#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>

void initPrinter() {
  char buf[] = {0x1b, 0x40};
  fwrite(buf, 1, sizeof(buf), stdout);
}

void imageMode(uint16_t xsize, uint16_t ysize) {
  char buf[] = {0x1d, 0x76, 0x30, 0, xsize&0xff, xsize>>8, ysize&0xff, ysize>>8};
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
  
  while (cupsRasterReadHeader2(ras, &header)) {
    page++;
    fprintf(stderr, "PAGE: %i/%i\n", page, header.NumCopies);
    fprintf(stderr, "BPP: %u\n", header.cupsBitsPerPixel);
    fprintf(stderr, "BitsPerColour: %u\n", header.cupsBitsPerColor);
    fprintf(stderr, "Width: %u\n", header.cupsWidth);
    fprintf(stderr, "Height: %u\n", header.cupsHeight);
    
    rasterLine = malloc(header.cupsBytesPerLine);

    initPrinter();
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
        if (pdata > 128) {
          outputLine[pixelToByte(pixel+1)-1] |= 1 << (7-pixel%8);
        }
      }
      fwrite(outputLine, 1, outSize, stdout);
      free(outputLine);
    }
    
    free(rasterLine);
    
  }
  cupsRasterClose(ras);
  
  return 0;
}
