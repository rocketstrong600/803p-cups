#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include "libdither.h"

// settings


typedef enum DitherMode {
  THRESHOLD = 0,
  FSTEINBERG
} DitherMode;

typedef struct PrintSettings {
  cups_bool_t InsertSheet;
  cups_adv_t AdvanceMedia;
  cups_cut_t CutMedia;
  unsigned int AdvanceDistance;
  DitherMode DitherMode;
} PrintSettings;


//image struct
typedef struct ImageRaster {
  unsigned int width; //Columns in Pixels
  unsigned int height; //Rows in Pixels
  uint8_t *data; //Data Ptr
  unsigned int bpp; //Bits Per Pixel
  unsigned int size; //Image Size in bytes
} ImageRaster;

void ReadSettings(cups_page_header2_t *header, PrintSettings *settings) {
  settings->AdvanceMedia = header->AdvanceMedia;
  settings->CutMedia = header->CutMedia;
  settings->AdvanceDistance = header->AdvanceDistance;
  settings->DitherMode = header->cupsInteger[0];
}

void initPrinter() {
  uint8_t buf[] = {0x1b, 0x40};
  fwrite(buf, 1, sizeof(buf), stdout);
}

//1bpp width to bytes
unsigned int widthToBytes(unsigned int width) {
  width = width + 7;
  width = width/8;
  return width;
}

//Print 1bpp image Raster
void printImage(ImageRaster imageRaster) {
  if (imageRaster.bpp != 1) {
    return;
  }

  if (imageRaster.data == NULL) {
    return;
  }

  uint16_t xsize = widthToBytes(imageRaster.width);
  uint16_t ysize = imageRaster.height;
  
  uint8_t buf[] = {0x1d, 0x76, 0x30, 0, xsize&0xffU, (xsize>>8)&0xffu, ysize&0xffU, (ysize>>8)&0xffu};
  fwrite(buf, 1, sizeof(buf), stdout);
  fwrite(imageRaster.data, 1, imageRaster.size, stdout);
}

void feedPixels(int amount) {
  ImageRaster blankImage;
  blankImage.width = 1;
  blankImage.height = amount;
  blankImage.bpp = 1;
  blankImage.size = blankImage.width*blankImage.height;
  blankImage.data = malloc(blankImage.size);
  if (blankImage.data == NULL) {
    return;
  }
  memset(blankImage.data, 0, amount);
  printImage(blankImage);
  free(blankImage.data);
}

void cutPaper() {
  uint8_t buf[] = {0x1b, 0x6d};
  fwrite(buf, 1, sizeof(buf), stdout);
}


//Converts ImageRaster to 1BPP With Thresholding Algorithym don't forget to free Data After Use
void thresholdImage(ImageRaster imageRaster, ImageRaster *outImageRaster, unsigned char threshold) {
  outImageRaster->height = imageRaster.height;
  outImageRaster->width = imageRaster.width;
  outImageRaster->bpp = 1;
  outImageRaster->size= outImageRaster->height*widthToBytes(outImageRaster->width);
  outImageRaster->data = (uint8_t*)malloc(outImageRaster->size);

  if (outImageRaster->data == NULL) {
    return;
  }

  if (imageRaster.data == NULL) {
    return;
  }

  memset(outImageRaster->data, 0, outImageRaster->size);

  for (unsigned int pixel = 0; pixel < imageRaster.size; pixel++) {
      unsigned int byteIndex = pixel/8;
      uint8_t bitIndex = 7 - (pixel % 8);
      outImageRaster->data[byteIndex] |= (imageRaster.data[pixel] < threshold) << bitIndex;
  }
}

//Converts ImageRaster to 1BPP With Thresholding Algorithym don't forget to free Data After Use
void fSteinbergImage(ImageRaster imageRaster, ImageRaster *outImageRaster) {
  outImageRaster->height = imageRaster.height;
  outImageRaster->width = imageRaster.width;
  outImageRaster->bpp = 1;
  outImageRaster->size= outImageRaster->height*widthToBytes(outImageRaster->width);
  outImageRaster->data = (uint8_t*)malloc(outImageRaster->size);

  if (outImageRaster->data == NULL) {
    return;
  }

  if (imageRaster.data == NULL) {
    return;
  }

  memset(outImageRaster->data, 0, outImageRaster->size);

  DitherImage* dither_image = DitherImage_new(imageRaster.width, imageRaster.height);

  for (int y=0; y < imageRaster.height; y++) {
    for (int x=0; x < imageRaster.width; x++) {
      uint8_t pixel = imageRaster.data[y*imageRaster.width+x];
      DitherImage_set_pixel(dither_image, x, y, pixel, pixel, pixel, false);
    }
  }

  ErrorDiffusionMatrix* error_matrix = get_floyd_steinberg_matrix();
  uint8_t *out_image = (uint8_t*)calloc(dither_image->width * dither_image->height, sizeof(uint8_t));

  if (out_image == NULL) {
    return;
  }
  
  error_diffusion_dither(dither_image, error_matrix, false, 0.0, out_image);

  for(int pixel = 0; pixel < imageRaster.size; pixel++) {    
    unsigned int byteIndex = pixel/8;
    uint8_t bitIndex = 7 - (pixel % 8);
    outImageRaster->data[byteIndex] |= (out_image[pixel] & 0x01) << bitIndex;
  }

  free(out_image);
  ErrorDiffusionMatrix_free(error_matrix);
  DitherImage_free(dither_image);
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
    fprintf(stderr, "BytesPerLine: %u\n", header.cupsBytesPerLine);
    fprintf(stderr, "BPP: %u\n", header.cupsBitsPerPixel);
    fprintf(stderr, "BitsPerColour: %u\n", header.cupsBitsPerColor);
    fprintf(stderr, "Width: %u\n", header.cupsWidth);
    fprintf(stderr, "Height: %u\n", header.cupsHeight);

    ImageRaster rasterImage;
    rasterImage.width = header.cupsWidth;
    rasterImage.height = header.cupsHeight;
    rasterImage.bpp = header.cupsBitsPerPixel;
    rasterImage.size = rasterImage.width*rasterImage.height;
    rasterImage.data = (uint8_t*)malloc(rasterImage.size);

    uint8_t *rasterLine = (uint8_t*)malloc(header.cupsBytesPerLine);

    if (rasterLine == NULL) {
      return 1;
    }
    
    memset(rasterLine, 0, header.cupsBytesPerLine);

    if (rasterImage.data == NULL) {
      return 1;
    }
    
    memset(rasterImage.data, 0, rasterImage.size);

    //read lines into rasterImage data
    for (unsigned int y = 0; y < header.cupsHeight; y++) {
      if (cupsRasterReadPixels(ras, rasterLine, header.cupsBytesPerLine) == 0) { 
        break;
      }
      unsigned char *imageLine = rasterImage.data+(y*rasterImage.width);
      memcpy(imageLine, rasterLine, header.cupsBytesPerLine);
    }

    free(rasterLine);

    if (settings.DitherMode == THRESHOLD) {
      ImageRaster outputImage;
      thresholdImage(rasterImage, &outputImage, 127);
      printImage(outputImage);
      if (outputImage.data != NULL) {
        free(outputImage.data);
      }
    }

    if (settings.DitherMode == FSTEINBERG) {
      ImageRaster outputImage;
      fSteinbergImage(rasterImage, &outputImage);
      printImage(outputImage);
      if (outputImage.data != NULL) {
        free(outputImage.data);
      }
    }

    free(rasterImage.data);
    
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
