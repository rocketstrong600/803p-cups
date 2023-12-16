#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include "libdither.h"

// settings struct


typedef enum DitherMode {
  THRESHOLD,
  FSTEINBERG
} DitherMode;

typedef struct PrintSettings {
  cups_bool_t InsertSheet;
  cups_adv_t AdvanceMedia;
  cups_cut_t CutMedia;
  unsigned int AdvanceDistance;
  DitherMode DitherMode;
} PrintSettings;

typedef struct ImageRaster {
  int width; //Columns in Bytes
  int height; //Rows in Bytes
  unsigned char *data; //Data Ptr
  int bpp; //Bits Per Pixel
} ImageRaster;

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

//Print 1bpp image Raster
void printImage(ImageRaster *imageRaster) {
  if (imageRaster->bpp != 1) {
    return;
  }
  if (imageRaster->data == NULL) {
    return;
  }
  uint16_t xsize = imageRaster->width;
  uint16_t ysize = imageRaster->height;
  char buf[] = {0x1d, 0x76, 0x30, 0, xsize&0xff, xsize>>8, ysize&0xff, ysize>>8};
  fwrite(buf, 1, sizeof(buf), stdout);  
  fwrite(&imageRaster->data, 1, imageRaster->width*imageRaster->height, stdout);
}

void feedPixels(int amount) {
  for (int i; i < amount; i++) {
    ImageRaster blankImage;
    blankImage.width = 1;
    blankImage.height = 1;
    blankImage.bpp = 1;
    blankImage.data = &(unsigned char){0x00};
    printImage(&blankImage);
  }
}

void cutPaper() {
  char buf[] = {0x1b, 0x6d};
  fwrite(buf, 1, sizeof(buf), stdout);
}

//HelperFunction to get num of bytes given pixels for a 1bit per pixel raster
uint16_t pixelToByte(uint16_t position) {;
  position = (position+7) & 0xFFFFFFF8;
  position = position >> 3;
  return position;
}

//Converts ImageRaster to 1BPP With Thresholding Algorithym don't forget to free Data After Use
void thresholdImage(ImageRaster *imageRaster, ImageRaster *outImageRaster, unsigned char threshold) {
  outImageRaster->height = imageRaster->height;
  outImageRaster->width = pixelToByte(imageRaster->width);
  outImageRaster->bpp = 1;
  outImageRaster->data = calloc(1,outImageRaster->height*outImageRaster->width);

  if (outImageRaster->data == NULL) {
    return;
  }

  if (imageRaster->data == NULL) {
    return;
  }

  for(unsigned int pixel = 0; pixel < imageRaster->height*imageRaster->width; pixel++) {
      int byteIndex = pixel >> 3;
      int bitIndex = 7-(pixel & 0x07);
      outImageRaster->data[byteIndex] |= (imageRaster->data[pixel] < threshold) << bitIndex;
  }
}


int main(int argc, char *argv[]) {
  cups_raster_t *ras = cupsRasterOpen(0, CUPS_RASTER_READ);
  cups_page_header2_t header;
  int page = 0;
  PrintSettings settings;
  while (cupsRasterReadHeader2(ras, &header)) {
    page++;
    ReadSettings(&header, &settings);
    initPrinter();
    fprintf(stderr, "PAGE: %i/%i\n", page, header.NumCopies);
    fprintf(stderr, "BPP: %u\n", header.cupsBitsPerPixel);
    fprintf(stderr, "BitsPerColour: %u\n", header.cupsBitsPerColor);
    fprintf(stderr, "Width: %u\n", header.cupsWidth);
    fprintf(stderr, "Height: %u\n", header.cupsHeight);

    unsigned char *rasterLine = NULL;
    rasterLine = calloc(1, header.cupsBytesPerLine);

    ImageRaster rasterImage;
    rasterImage.width = header.cupsWidth;
    rasterImage.height = header.cupsHeight;
    rasterImage.bpp = header.cupsBitsPerColor;
    rasterImage.data = calloc(1,rasterImage.width*rasterImage.height);
    
    for (unsigned int y = 0; y < header.cupsHeight; y++) {
      if (cupsRasterReadPixels(ras, rasterLine, header.cupsBytesPerLine) == 0) {
        break;
      }
      //Copy 1 line of the image into the raster image at current height
      memcpy(&rasterImage.data[y*(rasterImage.width)], &rasterLine, rasterImage.width);
    }

    if (settings.DitherMode == THRESHOLD) {
      ImageRaster outputImage;
      thresholdImage(&rasterImage, &outputImage, 127);
      printImage(&outputImage);
      if (outputImage.data != NULL) {
        free(outputImage.data);
      }
    }

    if (rasterLine != NULL) {
      free(rasterLine);
    }

    if (rasterImage.data != NULL) {
      free(rasterImage.data)
    }
    
    //advance every page
    if (settings.AdvanceMedia == CUPS_ADVANCE_PAGE) {
      feedPixels(settings.AdvanceDistance);
    }

    //cut every page
    if (settings.CutMedia == CUPS_CUT_PAGE) {
      cutPaper();
    }
    
  }
  cupsRasterClose(ras);

  //advance every job
  if (settings.AdvanceMedia == CUPS_ADVANCE_JOB) {
    feedPixels(settings.AdvanceDistance);
  }

  //cut every job
  if (settings.CutMedia == CUPS_CUT_JOB) {
    cutPaper();
  }

  return 0;
}
