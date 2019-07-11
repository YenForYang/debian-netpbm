/*--------------------------------------------------------------------------
  This file contains subroutines for use by Jpegtopnm to handle the
  EXIF header.

  The code is adapted from the program Jhead by Matthaias Wandel
  December 1999 - August 2000, and contributed to the public domain.
  Bryan Henderson adapted it to Netpbm in September 2001.

  An EXIF header is a JFIF APP1 marker.  It is generated by some
  digital cameras and contains information about the circumstances of
  the creation of the image (camera settings, etc.).

  The EXIF header uses the TIFF format, only it contains only tag
  values and no actual image.

  Note that the image format called EXIF is simply JFIF with an EXIF
  header, i.e. a subformat of JFIF.

  See the EXIF specs at http://exif.org (2001.09.01).

--------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
    #include <sys/utime.h>
#else
    #include <utime.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <errno.h>
#endif

#include "pm.h"
#include "exif.h"

static double FocalplaneXRes;
static double FocalplaneUnits;
static int ExifImageWidth;
static int MotorolaOrder = 0;

typedef struct {
    unsigned short Tag;
    char * Desc;
}TagTable_t;


/* Describes format descriptor */
static int BytesPerFormat[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};
#define NUM_FORMATS 12

#define FMT_BYTE       1 
#define FMT_STRING     2
#define FMT_USHORT     3
#define FMT_ULONG      4
#define FMT_URATIONAL  5
#define FMT_SBYTE      6
#define FMT_UNDEFINED  7
#define FMT_SSHORT     8
#define FMT_SLONG      9
#define FMT_SRATIONAL 10
#define FMT_SINGLE    11
#define FMT_DOUBLE    12

/* Describes tag values */

#define TAG_EXIF_OFFSET       0x8769
#define TAG_INTEROP_OFFSET    0xa005

#define TAG_MAKE              0x010F
#define TAG_MODEL             0x0110

#define TAG_EXPOSURETIME      0x829A
#define TAG_FNUMBER           0x829D

#define TAG_SHUTTERSPEED      0x9201
#define TAG_APERTURE          0x9202
#define TAG_MAXAPERTURE       0x9205
#define TAG_FOCALLENGTH       0x920A

#define TAG_DATETIME_ORIGINAL 0x9003
#define TAG_USERCOMMENT       0x9286

#define TAG_SUBJECT_DISTANCE  0x9206
#define TAG_FLASH             0x9209

#define TAG_FOCALPLANEXRES    0xa20E
#define TAG_FOCALPLANEUNITS   0xa210
#define TAG_EXIF_IMAGEWIDTH   0xA002
#define TAG_EXIF_IMAGELENGTH  0xA003

/* the following is added 05-jan-2001 vcs */
#define TAG_EXPOSURE_BIAS     0x9204
#define TAG_WHITEBALANCE      0x9208
#define TAG_METERING_MODE     0x9207
#define TAG_EXPOSURE_PROGRAM  0x8822
#define TAG_ISO_EQUIVALENT    0x8827
#define TAG_COMPRESSION_LEVEL 0x9102

#define TAG_THUMBNAIL_OFFSET  0x0201
#define TAG_THUMBNAIL_LENGTH  0x0202

static TagTable_t TagTable[] = {
  {   0x100,   "ImageWidth"},
  {   0x101,   "ImageLength"},
  {   0x102,   "BitsPerSample"},
  {   0x103,   "Compression"},
  {   0x106,   "PhotometricInterpretation"},
  {   0x10A,   "FillOrder"},
  {   0x10D,   "DocumentName"},
  {   0x10E,   "ImageDescription"},
  {   0x10F,   "Make"},
  {   0x110,   "Model"},
  {   0x111,   "StripOffsets"},
  {   0x112,   "Orientation"},
  {   0x115,   "SamplesPerPixel"},
  {   0x116,   "RowsPerStrip"},
  {   0x117,   "StripByteCounts"},
  {   0x11A,   "XResolution"},
  {   0x11B,   "YResolution"},
  {   0x11C,   "PlanarConfiguration"},
  {   0x128,   "ResolutionUnit"},
  {   0x12D,   "TransferFunction"},
  {   0x131,   "Software"},
  {   0x132,   "DateTime"},
  {   0x13B,   "Artist"},
  {   0x13E,   "WhitePoint"},
  {   0x13F,   "PrimaryChromaticities"},
  {   0x156,   "TransferRange"},
  {   0x200,   "JPEGProc"},
  {   0x201,   "ThumbnailOffset"},
  {   0x202,   "ThumbnailLength"},
  {   0x211,   "YCbCrCoefficients"},
  {   0x212,   "YCbCrSubSampling"},
  {   0x213,   "YCbCrPositioning"},
  {   0x214,   "ReferenceBlackWhite"},
  {   0x828D,  "CFARepeatPatternDim"},
  {   0x828E,  "CFAPattern"},
  {   0x828F,  "BatteryLevel"},
  {   0x8298,  "Copyright"},
  {   0x829A,  "ExposureTime"},
  {   0x829D,  "FNumber"},
  {   0x83BB,  "IPTC/NAA"},
  {   0x8769,  "ExifOffset"},
  {   0x8773,  "InterColorProfile"},
  {   0x8822,  "ExposureProgram"},
  {   0x8824,  "SpectralSensitivity"},
  {   0x8825,  "GPSInfo"},
  {   0x8827,  "ISOSpeedRatings"},
  {   0x8828,  "OECF"},
  {   0x9000,  "ExifVersion"},
  {   0x9003,  "DateTimeOriginal"},
  {   0x9004,  "DateTimeDigitized"},
  {   0x9101,  "ComponentsConfiguration"},
  {   0x9102,  "CompressedBitsPerPixel"},
  {   0x9201,  "ShutterSpeedValue"},
  {   0x9202,  "ApertureValue"},
  {   0x9203,  "BrightnessValue"},
  {   0x9204,  "ExposureBiasValue"},
  {   0x9205,  "MaxApertureValue"},
  {   0x9206,  "SubjectDistance"},
  {   0x9207,  "MeteringMode"},
  {   0x9208,  "LightSource"},
  {   0x9209,  "Flash"},
  {   0x920A,  "FocalLength"},
  {   0x927C,  "MakerNote"},
  {   0x9286,  "UserComment"},
  {   0x9290,  "SubSecTime"},
  {   0x9291,  "SubSecTimeOriginal"},
  {   0x9292,  "SubSecTimeDigitized"},
  {   0xA000,  "FlashPixVersion"},
  {   0xA001,  "ColorSpace"},
  {   0xA002,  "ExifImageWidth"},
  {   0xA003,  "ExifImageLength"},
  {   0xA005,  "InteroperabilityOffset"},
  {   0xA20B,  "FlashEnergy"},                 /* 0x920B in TIFF/EP */
  {   0xA20C,  "SpatialFrequencyResponse"},  /* 0x920C    -  - */
  {   0xA20E,  "FocalPlaneXResolution"},     /* 0x920E    -  - */
  {   0xA20F,  "FocalPlaneYResolution"},      /* 0x920F    -  - */
  {   0xA210,  "FocalPlaneResolutionUnit"},  /* 0x9210    -  - */
  {   0xA214,  "SubjectLocation"},             /* 0x9214    -  - */
  {   0xA215,  "ExposureIndex"},            /* 0x9215    -  - */
  {   0xA217,  "SensingMethod"},            /* 0x9217    -  - */
  {   0xA300,  "FileSource"},
  {   0xA301,  "SceneType"},
  {      0, NULL}
} ;



/*--------------------------------------------------------------------------
   Convert a 16 bit unsigned value from file's native byte order
--------------------------------------------------------------------------*/
static int Get16u(void * Short)
{
    if (MotorolaOrder){
        return (((uchar *)Short)[0] << 8) | ((uchar *)Short)[1];
    }else{
        return (((uchar *)Short)[1] << 8) | ((uchar *)Short)[0];
    }
}

/*--------------------------------------------------------------------------
   Convert a 32 bit signed value from file's native byte order
--------------------------------------------------------------------------*/
static int Get32s(void * Long)
{
    if (MotorolaOrder){
        return  ((( char *)Long)[0] << 24) | (((uchar *)Long)[1] << 16)
              | (((uchar *)Long)[2] << 8 ) | (((uchar *)Long)[3] << 0 );
    }else{
        return  ((( char *)Long)[3] << 24) | (((uchar *)Long)[2] << 16)
              | (((uchar *)Long)[1] << 8 ) | (((uchar *)Long)[0] << 0 );
    }
}

/*--------------------------------------------------------------------------
   Convert a 32 bit unsigned value from file's native byte order
--------------------------------------------------------------------------*/
static unsigned Get32u(void * Long)
{
    return (unsigned)Get32s(Long) & 0xffffffff;
}

/*--------------------------------------------------------------------------
   Display a number as one of its many formats
--------------------------------------------------------------------------*/
static void PrintFormatNumber(FILE * const file, 
                              void * const ValuePtr, 
                              int const Format, int const ByteCount)
{
    int a;
    switch(Format){
    case FMT_SBYTE:
    case FMT_BYTE:      fprintf(file, "%02x ",*(uchar *)ValuePtr); break;
    case FMT_USHORT:    fprintf(file, "%d\n",Get16u(ValuePtr));    break;
    case FMT_ULONG:     
    case FMT_SLONG:     fprintf(file, "%d\n",Get32s(ValuePtr));    break;
    case FMT_SSHORT:    
        fprintf(file, "%hd\n",(signed short)Get16u(ValuePtr));     break;
    case FMT_URATIONAL:
    case FMT_SRATIONAL: 
        fprintf(file, "%d/%d\n",Get32s(ValuePtr), Get32s(4+(char *)ValuePtr));
        break;
    case FMT_SINGLE:    
        fprintf(file, "%f\n",(double)*(float *)ValuePtr);          break;
    case FMT_DOUBLE:    fprintf(file, "%f\n",*(double *)ValuePtr); break;
    default: 
        fprintf(file, "Unknown format %d:", Format);
        for (a=0;a<ByteCount && a<16;a++){
            printf("%02x", ((uchar *)ValuePtr)[a]);
        }
        fprintf(file, "\n");
    }
}


/*--------------------------------------------------------------------------
   Evaluate number, be it int, rational, or float from directory.
--------------------------------------------------------------------------*/
static double ConvertAnyFormat(void * ValuePtr, int Format)
{
    double Value;
    Value = 0;

    switch(Format){
        case FMT_SBYTE:     Value = *(signed char *)ValuePtr;  break;
        case FMT_BYTE:      Value = *(uchar *)ValuePtr;        break;

        case FMT_USHORT:    Value = Get16u(ValuePtr);          break;
        case FMT_ULONG:     Value = Get32u(ValuePtr);          break;

        case FMT_URATIONAL:
        case FMT_SRATIONAL: 
            {
                int Num,Den;
                Num = Get32s(ValuePtr);
                Den = Get32s(4+(char *)ValuePtr);
                if (Den == 0){
                    Value = 0;
                }else{
                    Value = (double)Num/Den;
                }
                break;
            }

        case FMT_SSHORT:    Value = (signed short)Get16u(ValuePtr);  break;
        case FMT_SLONG:     Value = Get32s(ValuePtr);                break;

        /* Not sure if this is correct (never seen float used in Exif format)
         */
        case FMT_SINGLE:    Value = (double)*(float *)ValuePtr;      break;
        case FMT_DOUBLE:    Value = *(double *)ValuePtr;             break;
    }
    return Value;
}

/*--------------------------------------------------------------------------
   Process one of the nested EXIF directories.
--------------------------------------------------------------------------*/
static void ProcessExifDir(unsigned char * DirStart, 
                           unsigned char * OffsetBase, unsigned ExifLength,
                           ImageInfo_t * const ImageInfoP, 
                           int const ShowTags,
                           unsigned char ** const LastExifRefdP )
{
    int de;
    int a;
    int NumDirEntries;
    unsigned ThumbnailOffset = 0;
    unsigned ThumbnailSize = 0;

    NumDirEntries = Get16u(DirStart);

    if ((DirStart+2+NumDirEntries*12) > (OffsetBase+ExifLength)){
        pm_error("Illegally sized directory");
    }

    if (ShowTags){
        pm_message("Directory with %d entries",NumDirEntries);
    }

    for (de=0;de<NumDirEntries;de++){
        int Tag, Format, Components;
        unsigned char * ValuePtr;
            /* This actually can point to a variety of things; it must be
               cast to other types when used.  But we use it as a byte-by-byte
               cursor, so we declare it as a pointer to a generic byte here.
            */
        int ByteCount;
        unsigned char * DirEntry;
        DirEntry = DirStart+2+12*de;

        Tag = Get16u(DirEntry);
        Format = Get16u(DirEntry+2);
        Components = Get32u(DirEntry+4);

        if ((Format-1) >= NUM_FORMATS) {
            /* (-1) catches illegal zero case as unsigned underflows
               to positive large.  
            */
            pm_error("Illegal format code in EXIF dir"); }

        ByteCount = Components * BytesPerFormat[Format];

        if (ByteCount > 4){
            unsigned OffsetVal;
            OffsetVal = Get32u(DirEntry+8);
            /* If its bigger than 4 bytes, the dir entry contains an offset.*/
            if (OffsetVal+ByteCount > ExifLength){
                /* Bogus pointer offset and / or bytecount value */
                pm_error("Illegal pointer offset value in EXIF.  "
                         "Offset %d bytes %d ExifLen %d\n",
                         OffsetVal, ByteCount, ExifLength);
            }
            ValuePtr = OffsetBase+OffsetVal;
        }else{
            /* 4 bytes or less and value is in the dir entry itself */
            ValuePtr = DirEntry+8;
        }

        if (*LastExifRefdP < ValuePtr+ByteCount){
            /* Keep track of last byte in the exif header that was
               actually referenced.  That way, we know where the
               discardable thumbnail data begins.
            */
            *LastExifRefdP = ValuePtr+ByteCount;
        }

        if (ShowTags){
            /* Show tag name */
            for (a=0;;a++){
                if (TagTable[a].Tag == 0){
                    fprintf(stderr, "  Unknown Tag %04x Value = ", Tag);
                    break;
                }
                if (TagTable[a].Tag == Tag){
                    fprintf(stderr, "    %s = ",TagTable[a].Desc);
                    break;
                }
            }

            /* Show tag value. */
            switch(Format){

                case FMT_UNDEFINED:
                    /* Undefined is typically an ascii string. */

                case FMT_STRING:
                    /* String arrays printed without function call
                       (different from int arrays)
                    */
                    fprintf(stderr, "\"");
                    for (a=0;a<ByteCount;a++){
                        if (isprint(((char*)ValuePtr)[a])){
                            putc(((char*)ValuePtr)[a], stderr);
                        }
                    }
                    fprintf(stderr, "\"\n");
                    break;

                default:
                    /* Handle arrays of numbers later (will there ever be?)*/
                    PrintFormatNumber(stderr, ValuePtr, Format, ByteCount);
            }
        }

        /* Extract useful components of tag */
        switch(Tag){

            case TAG_MAKE:
                strncpy(ImageInfoP->CameraMake, (char*)ValuePtr, 31);
                break;

            case TAG_MODEL:
                strncpy(ImageInfoP->CameraModel, (char*)ValuePtr, 39);
                break;

            case TAG_DATETIME_ORIGINAL:
                strncpy(ImageInfoP->DateTime, (char*)ValuePtr, 19);
                break;

            case TAG_USERCOMMENT:
                /* Olympus has this padded with trailing spaces.
                   Remove these first. 
                */
                for (a=ByteCount;;){
                    a--;
                    if (((char*)ValuePtr)[a] == ' '){
                        ((char*)ValuePtr)[a] = '\0';
                    }else{
                        break;
                    }
                    if (a == 0) break;
                }

                /* Copy the comment */
                if (memcmp(ValuePtr, "ASCII",5) == 0){
                    for (a=5;a<10;a++){
                        char c;
                        c = ((char*)ValuePtr)[a];
                        if (c != '\0' && c != ' '){
                            strncpy(ImageInfoP->Comments, (char*)ValuePtr+a, 
                                    199);
                            break;
                        }
                    }
                    
                }else{
                    strncpy(ImageInfoP->Comments, (char*)ValuePtr, 199);
                }
                break;

            case TAG_FNUMBER:
                /* Simplest way of expressing aperture, so I trust it the most.
                   (overwrite previously computd value if there is one)
                   */
                ImageInfoP->ApertureFNumber = 
                    (float)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_APERTURE:
            case TAG_MAXAPERTURE:
                /* More relevant info always comes earlier, so only
                 use this field if we don't have appropriate aperture
                 information yet. 
                */
                if (ImageInfoP->ApertureFNumber == 0){
                    ImageInfoP->ApertureFNumber = (float)
                        exp(ConvertAnyFormat(ValuePtr, Format)*log(2)*0.5);
                }
                break;

            case TAG_FOCALLENGTH:
                /* Nice digital cameras actually save the focal length
                   as a function of how farthey are zoomed in. 
                */

                ImageInfoP->FocalLength = 
                    (float)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_SUBJECT_DISTANCE:
                /* Inidcates the distacne the autofocus camera is focused to.
                   Tends to be less accurate as distance increases.
                */
                ImageInfoP->Distance = 
                    (float)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_EXPOSURETIME:
                /* Simplest way of expressing exposure time, so I
                   trust it most.  (overwrite previously computd value
                   if there is one) 
                */
                ImageInfoP->ExposureTime = 
                    (float)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_SHUTTERSPEED:
                /* More complicated way of expressing exposure time,
                   so only use this value if we don't already have it
                   from somewhere else.  
                */
                if (ImageInfoP->ExposureTime == 0){
                    ImageInfoP->ExposureTime = (float)
                        (1/exp(ConvertAnyFormat(ValuePtr, Format)*log(2)));
                }
                break;

            case TAG_FLASH:
                if (ConvertAnyFormat(ValuePtr, Format)){
                    ImageInfoP->FlashUsed = 1;
                }
                break;

            case TAG_EXIF_IMAGELENGTH:
            case TAG_EXIF_IMAGEWIDTH:
                /* Use largest of height and width to deal with images
                   that have been rotated to portrait format.  
                */
                a = (int)ConvertAnyFormat(ValuePtr, Format);
                if (ExifImageWidth < a) ExifImageWidth = a;
                break;

            case TAG_FOCALPLANEXRES:
                FocalplaneXRes = ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_FOCALPLANEUNITS:
                switch((int)ConvertAnyFormat(ValuePtr, Format)){
                    case 1: FocalplaneUnits = 25.4; break; /* 1 inch */
                    case 2: 
                        /* According to the information I was using, 2
                           means meters.  But looking at the Cannon
                           powershot's files, inches is the only
                           sensible value.  
                        */
                        FocalplaneUnits = 25.4;
                        break;

                    case 3: FocalplaneUnits = 10;   break;  /* 1 centimeter*/
                    case 4: FocalplaneUnits = 1;    break;  /* 1 millimeter*/
                    case 5: FocalplaneUnits = .001; break;  /* 1 micrometer*/
                }
                break;

                /* Remaining cases contributed by: Volker C. Schoech
                   (schoech@gmx.de)
                */

            case TAG_EXPOSURE_BIAS:
                ImageInfoP->ExposureBias = 
                    (float) ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_WHITEBALANCE:
                ImageInfoP->Whitebalance = 
                    (int)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_METERING_MODE:
                ImageInfoP->MeteringMode = 
                    (int)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_EXPOSURE_PROGRAM:
                ImageInfoP->ExposureProgram = 
                    (int)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_ISO_EQUIVALENT:
                ImageInfoP->ISOequivalent = 
                    (int)ConvertAnyFormat(ValuePtr, Format);
                if ( ImageInfoP->ISOequivalent < 50 ) 
                    ImageInfoP->ISOequivalent *= 200;
                break;

            case TAG_COMPRESSION_LEVEL:
                ImageInfoP->CompressionLevel = 
                    (int)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_THUMBNAIL_OFFSET:
                ThumbnailOffset = (unsigned)ConvertAnyFormat(ValuePtr, Format);
                break;

            case TAG_THUMBNAIL_LENGTH:
                ThumbnailSize = (unsigned)ConvertAnyFormat(ValuePtr, Format);
                break;

        }

        if (Tag == TAG_EXIF_OFFSET || Tag == TAG_INTEROP_OFFSET){
            unsigned char * SubdirStart;
            SubdirStart = OffsetBase + Get32u(ValuePtr);
            if (SubdirStart < OffsetBase || 
                SubdirStart > OffsetBase+ExifLength){
                pm_error("Illegal subdirectory link");
            }
            ProcessExifDir(SubdirStart, OffsetBase, ExifLength, ImageInfoP, 
                           ShowTags, LastExifRefdP);
            continue;
        }
    }


    {
        /* In addition to linking to subdirectories via exif tags,
           there's also a potential link to another directory at the end
           of each directory.  This has got to be the result of a
           committee!  
        */
        unsigned char * SubdirStart;
        unsigned Offset;
        Offset = Get16u(DirStart+2+12*NumDirEntries);
        if (Offset){
            SubdirStart = OffsetBase + Offset;
            if (SubdirStart < OffsetBase 
                || SubdirStart > OffsetBase+ExifLength){
                pm_error("Illegal subdirectory link");
            }
            ProcessExifDir(SubdirStart, OffsetBase, ExifLength, ImageInfoP, 
                           ShowTags, LastExifRefdP);
        }
    }


    if (ThumbnailSize && ThumbnailOffset){
        if (ThumbnailSize + ThumbnailOffset <= ExifLength){
            /* The thumbnail pointer appears to be valid.  Store it. */
            ImageInfoP->ThumbnailPointer = OffsetBase + ThumbnailOffset;
            ImageInfoP->ThumbnailSize = ThumbnailSize;

            if (ShowTags){
                fprintf(stderr, "Thumbnail size: %d bytes\n",ThumbnailSize);
            }
        }
    }
}

/*--------------------------------------------------------------------------
   Process a EXIF marker
   Describes all the drivel that most digital cameras include...
--------------------------------------------------------------------------*/
void 
process_EXIF (unsigned char * CharBuf, unsigned int length,
              ImageInfo_t * ImageInfoP, int const ShowTags)
{
    int ExifSettingsLength;
    unsigned char * LastExifRefd;

    ImageInfoP->FlashUsed = 0; 
        /* If it's from a digicam, and it used flash, it says so. */
    ImageInfoP->Comments[0] = '\0';  /* Initial value - null string */

    FocalplaneXRes = 0;
    FocalplaneUnits = 0;
    ExifImageWidth = 0;

    if (ShowTags){
        fprintf(stderr, "Exif header %d bytes long\n",length);
    }

    {   /* Check the EXIF header component */
        static const uchar ExifHeader[] = "Exif\0\0";
        if (memcmp(CharBuf+0, ExifHeader,6)){
            pm_error("Incorrect Exif header");
        }
    }

    if (memcmp(CharBuf+6,"II",2) == 0){
        if (ShowTags) fprintf(stderr, "Exif section in Intel order\n");
        MotorolaOrder = 0;
    }else{
        if (memcmp(CharBuf+6,"MM",2) == 0){
            if (ShowTags) fprintf(stderr, "Exif section in Motorola order\n");
            MotorolaOrder = 1;
        }else{
            pm_error("Invalid Exif alignment marker.");
        }
    }

    /* Check the next two values for correctness. */
    if (Get16u(CharBuf+8) != 0x2a
      || Get32u(CharBuf+10) != 0x08){
        pm_error("Invalid Exif start (1)");
    }

    LastExifRefd = CharBuf;

    /* First directory starts 16 bytes in.  Offsets start at 8 bytes in. */
    ProcessExifDir(CharBuf+14, CharBuf+6, length-6, ImageInfoP, ShowTags,
                   &LastExifRefd);

    /* This is how far the interesting (non thumbnail) part of the exif went.
     */
    ExifSettingsLength = LastExifRefd - CharBuf;

    /* Compute the CCD width, in milimeters. */
    if (FocalplaneXRes != 0){
        ImageInfoP->CCDWidth = 
            (float)(ExifImageWidth * FocalplaneUnits / FocalplaneXRes);
    }

    if (ShowTags){
        fprintf(stderr, 
                "Non settings part of Exif header: %d bytes\n",
                length-ExifSettingsLength);
    }
}

/*--------------------------------------------------------------------------
   Show the collected image info, displaying camera F-stop and shutter
   speed in a consistent and legible fashion.
--------------------------------------------------------------------------*/
void 
ShowImageInfo(ImageInfo_t * const ImageInfoP)
{
    if (ImageInfoP->CameraMake[0]){
        fprintf(stderr, "Camera make  : %s\n",ImageInfoP->CameraMake);
        fprintf(stderr, "Camera model : %s\n",ImageInfoP->CameraModel);
    }
    if (ImageInfoP->DateTime[0]){
        fprintf(stderr, "Date/Time    : %s\n",ImageInfoP->DateTime);
    }
    fprintf(stderr, "Resolution   : %d x %d\n",
            ImageInfoP->Width, ImageInfoP->Height);
    if (ImageInfoP->IsColor == 0){
        fprintf(stderr, "Color/bw     : Black and white\n");
    }
    if (ImageInfoP->FlashUsed >= 0){
        fprintf(stderr, "Flash used   : %s\n",
                ImageInfoP->FlashUsed ? "Yes" :"No");
    }
    if (ImageInfoP->FocalLength){
        fprintf(stderr, "Focal length : %4.1fmm",
                (double)ImageInfoP->FocalLength);
        if (ImageInfoP->CCDWidth){
            fprintf(stderr, "  (35mm equivalent: %dmm)",
                    (int)
                    (ImageInfoP->FocalLength/ImageInfoP->CCDWidth*35 + 0.5));
        }
        fprintf(stderr, "\n");
    }

    if (ImageInfoP->CCDWidth){
        fprintf(stderr, "CCD Width    : %4.2fmm\n",
                (double)ImageInfoP->CCDWidth);
    }

    if (ImageInfoP->ExposureTime){
        fprintf(stderr, "Exposure time:%6.3f s ",
                (double)ImageInfoP->ExposureTime);
        if (ImageInfoP->ExposureTime <= 0.5){
            fprintf(stderr, " (1/%d)",(int)(0.5 + 1/ImageInfoP->ExposureTime));
        }
        printf("\n");
    }
    if (ImageInfoP->ApertureFNumber){
        fprintf(stderr, "Aperture     : f/%3.1f\n",
                (double)ImageInfoP->ApertureFNumber);
    }
    if (ImageInfoP->Distance){
        if (ImageInfoP->Distance < 0){
            fprintf(stderr, "Focus Dist.  : Infinite\n");
        }else{
            fprintf(stderr, "Focus Dist.  :%5.2fm\n",
                    (double)ImageInfoP->Distance);
        }
    }





    if (ImageInfoP->ISOequivalent){ /* 05-jan-2001 vcs */
        fprintf(stderr, "ISO equiv.   : %2d\n",(int)ImageInfoP->ISOequivalent);
    }
    if (ImageInfoP->ExposureBias){ /* 05-jan-2001 vcs */
        fprintf(stderr, "Exposure bias:%4.2f\n",
                (double)ImageInfoP->ExposureBias);
    }
        
    if (ImageInfoP->Whitebalance){ /* 05-jan-2001 vcs */
        switch(ImageInfoP->Whitebalance) {
        case 1:
            fprintf(stderr, "Whitebalance : sunny\n");
            break;
        case 2:
            fprintf(stderr, "Whitebalance : fluorescent\n");
            break;
        case 3:
            fprintf(stderr, "Whitebalance : incandescent\n");
            break;
        default:
            fprintf(stderr, "Whitebalance : cloudy\n");
        }
    }
    if (ImageInfoP->MeteringMode){ /* 05-jan-2001 vcs */
        switch(ImageInfoP->MeteringMode) {
        case 2:
            fprintf(stderr, "Metering Mode: center weight\n");
            break;
        case 3:
            fprintf(stderr, "Metering Mode: spot\n");
            break;
        case 5:
            fprintf(stderr, "Metering Mode: matrix\n");
            break;
        }
    }
    if (ImageInfoP->ExposureProgram){ /* 05-jan-2001 vcs */
        switch(ImageInfoP->ExposureProgram) {
        case 2:
            fprintf(stderr, "Exposure     : program (auto)\n");
            break;
        case 3:
            fprintf(stderr, "Exposure     : aperture priority (semi-auto)\n");
            break;
        case 4:
            fprintf(stderr, "Exposure     : shutter priority (semi-auto)\n");
            break;
        }
    }
    if (ImageInfoP->CompressionLevel){ /* 05-jan-2001 vcs */
        switch(ImageInfoP->CompressionLevel) {
        case 1:
            fprintf(stderr, "JPG Quality  : basic\n");
            break;
        case 2:
            fprintf(stderr, "JPG Quality  : normal\n");
            break;
        case 4:
            fprintf(stderr, "JPG Quality  : fine\n");
            break;
       }
    }

         

    /* Print the comment. Print 'Comment:' for each new line of comment. */
    if (ImageInfoP->Comments[0]){
        int a,c;
        fprintf(stderr, "Comment      : ");
        for (a=0;a<MAX_COMMENT;a++){
            c = ImageInfoP->Comments[a];
            if (c == '\0') break;
            if (c == '\n'){
                fprintf(stderr, "\nComment      : ");
            }else{
                putc(c, stderr);
            }
        }
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "\n");
}



